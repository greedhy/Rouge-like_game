// 调用链：工作台默认图 -> 服务器落点 -> 共享同层租约查找 -> Removed -> 原元素多播表现。
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"
#include "Terrain/TerrainSurface.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TerrainDispel, "Abilities.Mage.TerrainDispel");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_TerrainDispelCooldown, "Cooldown.Mage.TerrainDispel");

namespace
{
    constexpr int32 MaxDispelTargets = 4;
}

bool UAbilityDispelSurfaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityWorkshopTerrainNodes::IsValidSearchRadius(Radius) || MaxTargets < 1 || MaxTargets > MaxDispelTargets ||
        Kind > ETerrainSurfaceType::ConductiveCloud ||
        (bFilterByKind && TerrainSurfaceTypes::IsCloud(Kind) != bCloudLayer))
    {
        Error = TEXT("Terrain dispel requires radius 25–1200 cm, a count from 1 to 4, and a kind within its layer.");
    }
    return Error.IsEmpty();
}

bool UAbilityDispelSurfaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                            FString& Error)
{
    Error.Reset();
    if (Parameter == EAbilityConfigParameter::Radius && AbilityWorkshopTerrainNodes::IsValidSearchRadius(Value))
    {
        Radius = Value;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::Count && FMath::IsFinite(Value) && Value >= 1.f &&
        Value <= MaxDispelTargets && Value == FMath::RoundToFloat(Value))
    {
        MaxTargets = static_cast<int32>(Value);
        return true;
    }
    if (Parameter == EAbilityConfigParameter::TerrainSurfaceKind && FMath::IsFinite(Value) && Value >= 0.f &&
        Value <= static_cast<float>(ETerrainSurfaceType::ConductiveCloud) && Value == FMath::RoundToFloat(Value))
    {
        const auto RequestedKind = static_cast<ETerrainSurfaceType>(static_cast<uint8>(Value));
        if (TerrainSurfaceTypes::IsCloud(RequestedKind) != bCloudLayer)
        {
            Error = TEXT("The dispel kind must belong to the selected ground or cloud layer.");
            return false;
        }
        Kind = RequestedKind;
        bFilterByKind = true;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::TerrainLayer && (Value == 0.f || Value == 1.f))
    {
        const bool bNewCloudLayer = Value == 1.f;
        if (bCloudLayer != bNewCloudLayer)
        {
            bCloudLayer = bNewCloudLayer;
            if (TerrainSurfaceTypes::IsCloud(Kind) != bCloudLayer)
            {
                // 保留显式过滤或不限类别，由新层的安全默认类别承接需要的筛选。
                Kind = bCloudLayer ? ETerrainSurfaceType::MistCloud : ETerrainSurfaceType::Toxic;
            }
        }
        return true;
    }
    Error = TEXT("This terrain-dispel parameter or value is outside its supported range.");
    return false;
}

bool UAbilityDispelSurfaceNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    FString Error;
    return Parameter == EAbilityConfigScalar::Radius &&
           TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
}

bool UAbilityDispelSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    const auto* Settings = CastChecked<UAbilityDispelSurfaceNodeConfig>(&Config);
    const FVector Origin = AbilityWorkshopTerrainNodes::ResolveTerrainOrigin(*Caster, Input.Origin);
    const TOptional<ETerrainSurfaceType> Kind =
        Settings->bFilterByKind ? TOptional<ETerrainSurfaceType>(Settings->Kind) : TOptional<ETerrainSurfaceType>();
    const auto Targets = AbilityWorkshopTerrainNodes::FindOwnedSurfaces(
        *Caster, Origin, Settings->Radius, Kind, Settings->MaxTargets, Settings->bCloudLayer);
    for (const auto& Target : Targets)
    {
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
        {
            break;
        }
        auto* Surface = Target.Get();
        auto* Lease = Surface ? Surface->FindComponentByClass<UAbilityWorkshopTerrainLease>() : nullptr;
        if (!Lease || !Surface->Config ||
            TerrainSurfaceTypes::IsCloud(Surface->Config->SurfaceType) != Settings->bCloudLayer ||
            (Kind.IsSet() && Surface->Config->SurfaceType != Kind.GetValue()))
        {
            continue;
        }
        // 销毁前捕获纯值。回调可能结束图或清除其他地块，后续循环重新验证弱引用与租约。
        auto Payload = Input;
        Payload.Origin = Surface->GetActorLocation();
        Payload.EventCenter = Payload.Origin;
        Payload.ShapeRadius = static_cast<float>(FMath::Max(Surface->SurfaceSize.X, Surface->SurfaceSize.Y) * .5);
        if (Lease->TryDispelFor(*Caster) && !bStopped && !Owner.IsFinished())
        {
            Owner.Publish(Id, TEXT("Removed"), Payload);
        }
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopTerrainDispel::UWorkshopTerrainDispel()
{
    SetAssetTags(FGameplayTagContainer(TAG_TerrainDispel));
    CooldownTags.AddTag(TAG_TerrainDispelCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    ManaCost = 10.f;
    Cooldown = 5.f;
    SpellName = NSLOCTEXT("SkillCards", "TerrainDispel", "涡流涤地");
    SpellSummary = NSLOCTEXT("SkillCards", "TerrainDispelSummary", "清除落点 3.6 米内最近的最多 2 块自有临时地表。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Dispel = Initializer.CreateDefaultSubobject<UAbilityDispelSurfaceNodeConfig>(Graph, TEXT("TerrainDispel"));
    auto* Visual = Initializer.CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("DispelVisual"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air")));

    const FGuid VisualId(0, 0, 0, 2);
    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("World.DispelSurface"), Dispel},
                    {VisualId, TEXT("Presentation.ElementPulse"), Visual}};
    Graph->Edges = {{Graph->EntryNodeId, VisualId, TEXT("Removed")}};
}

void AbilityWorkshopDispel::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Dispel;
    Dispel.TypeId = TEXT("World.DispelSurface");
    Dispel.ConfigClass = UAbilityDispelSurfaceNodeConfig::StaticClass();
    Dispel.ExecutionClass = UAbilityDispelSurfaceNodeExecution::StaticClass();
    Dispel.InputPayload = EAbilityPortPayload::Origin;
    Dispel.bSupportsActorTargets = Dispel.bSupportsMassTargets = Dispel.bSupportsSnapshotTargets = true;
    Dispel.bRuntimeFiniteSafe = true;
    Dispel.Ports = {{TEXT("Removed"), EAbilityPortPayload::Origin, false},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Dispel.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Dispel, Error), TEXT("Ground dispel registration: %s"), *Error);
    }
}
