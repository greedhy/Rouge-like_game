// 调用链：有限 Repeat.Pulse → FindOwnedSurface → 共享同层地表租约 → Query.Shape 和原伤害/元素状态。
#include "AbilitySystem/Unified/WorkshopTerrainCatalystAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "NativeGameplayTags.h"
#include "Terrain/TerrainSurface.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StormCatalyst, "Abilities.Mage.StormCatalyst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StormCatalystCooldown, "Cooldown.Mage.StormCatalyst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostCatalyst, "Abilities.Mage.FrostCatalyst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostCatalystCooldown, "Cooldown.Mage.FrostCatalyst");

namespace
{
    /** 每次 Pulse 重新读取最近合法地块。预算届满及取消仍由原有限时序节点管理。 */
    void PrependTerrainRelay(UAbilityGraphDefinition& Graph, ETerrainSurfaceType Kind)
    {
        const auto& Initializer = FObjectInitializer::Get();
        auto* Repeat = Initializer.CreateDefaultSubobject<UAbilityRepeatNodeConfig>(&Graph, TEXT("CatalystCadence"));
        Repeat->Count = 3;
        Repeat->Period = .6f;
        Repeat->Duration = 1.6f;
        Repeat->bEmitImmediately = true;
        auto* Search =
            Initializer.CreateDefaultSubobject<UAbilityFindOwnedSurfaceNodeConfig>(&Graph, TEXT("CatalystTerrain"));
        Search->Kind = Kind;

        const FGuid RepeatId(0, 0, 0, 4);
        const FGuid SearchId(0, 0, 0, 5);
        Graph.Nodes.Append(
            {{RepeatId, TEXT("Control.Repeat"), Repeat}, {SearchId, TEXT("World.FindOwnedSurface"), Search}});
        Graph.Edges.Append({{RepeatId, SearchId, TEXT("Pulse")}, {SearchId, Graph.EntryNodeId, TEXT("Found")}});
        Graph.EntryNodeId = RepeatId;
    }
} // namespace

bool UAbilityFindOwnedSurfaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!AbilityWorkshopTerrainNodes::IsValidSearchRadius(Radius) || Kind > ETerrainSurfaceType::Conductive)
    {
        Error = TEXT("Terrain relay requires a radius of 25–1200 cm and a known ground kind.");
    }
    return Error.IsEmpty();
}

bool UAbilityFindOwnedSurfaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                               FString& Error)
{
    Error.Reset();
    if (Parameter == EAbilityConfigParameter::Radius && AbilityWorkshopTerrainNodes::IsValidSearchRadius(Value))
    {
        Radius = Value;
        return true;
    }
    if (Parameter == EAbilityConfigParameter::TerrainInputKind && FMath::IsFinite(Value) && Value >= 0.f &&
        Value <= static_cast<float>(ETerrainSurfaceType::Conductive) && Value == FMath::RoundToFloat(Value))
    {
        Kind = static_cast<ETerrainSurfaceType>(static_cast<uint8>(Value));
        return true;
    }
    Error = TEXT("Terrain relay only accepts a bounded radius or a supported ground kind.");
    return false;
}

bool UAbilityFindOwnedSurfaceNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    FString Error;
    return Parameter == EAbilityConfigScalar::Radius &&
           TrySetRecipeParameter(EAbilityConfigParameter::Radius, Radius * Multiplier, Error);
}

bool UAbilityFindOwnedSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
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
        Error = TEXT("The terrain relay source is no longer current.");
        return false;
    }

    const auto* Settings = CastChecked<UAbilityFindOwnedSurfaceNodeConfig>(&Config);
    const FVector Origin = AbilityWorkshopTerrainNodes::ResolveTerrainOrigin(*Caster, Input.Origin);
    const auto Surfaces =
        AbilityWorkshopTerrainNodes::FindOwnedSurfaces(*Caster, Origin, Settings->Radius, Settings->Kind, 1);
    const auto* Surface = Surfaces.IsEmpty() ? nullptr : Surfaces[0].Get();
    const auto* Lease = Surface ? Surface->FindComponentByClass<UAbilityWorkshopTerrainLease>() : nullptr;
    if (Lease && Lease->IsActiveFor(*Caster))
    {
        // 只发布纯位置值，不把查找地块时接收到的旧活目标权限重新授予后继。
        FAbilityNodeInput Found;
        Found.SourceToken = Input.SourceToken;
        Found.Origin = Found.EventCenter = Surface->GetActorLocation();
        Found.Direction = Input.Direction;
        Found.ShapeRadius = static_cast<float>(FMath::Max(Surface->SurfaceSize.X, Surface->SurfaceSize.Y) * .5);
        Found.DamageParams = Input.DamageParams;
        Found.DamageParams.TargetAbilitySystemComponent = nullptr;
        Found.DamageScale = Input.DamageScale;
        Found.DebuffDamageScale = Input.DebuffDamageScale;
        Found.Level = Input.Level;
        Found.Context = Input.Context;
        Found.ParameterFrame = Input.ParameterFrame;
        if (!Owner.Publish(Id, TEXT("Found"), Found))
        {
            return false;
        }
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopStormCatalyst::UWorkshopStormCatalyst()
{
    SetAssetTags(FGameplayTagContainer(TAG_StormCatalyst));
    CooldownTags.AddTag(TAG_StormCatalystCooldown);
    ManaCost = 18.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "StormCatalyst", "雷地借势");
    SpellSummary =
        NSLOCTEXT("SkillCards", "StormCatalystSummary",
                  "每次脉冲在落点 4.2 米内查找最近的同层自有通电地块，并以该地块为中心释放 2.2 米半径的雷击。");
    ConfigureArea(220.f, 12.f, EAbilityElement::Thunder,
                  TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Lightning.NS_Free_Spells_Buff_Lightning"));
    PrependTerrainRelay(*Graph, ETerrainSurfaceType::Conductive);
}

UWorkshopFrostCatalyst::UWorkshopFrostCatalyst()
{
    SetAssetTags(FGameplayTagContainer(TAG_FrostCatalyst));
    CooldownTags.AddTag(TAG_FrostCatalystCooldown);
    ManaCost = 16.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostCatalyst", "霜地借势");
    SpellSummary = NSLOCTEXT("SkillCards", "FrostCatalystSummary",
                             "每次脉冲查找落点 4.2 米内最近的同层自有霜地，在地块周围施加冻僵与减速。");
    ConfigureArea(220.f, 0.f, EAbilityElement::Ice,
                  TEXT("/Game/Art/CompositionEffects/NS_Frost_Impact.NS_Frost_Impact"));
    const FGuid SlowId(0, 0, 0, 6);
    auto* Slow = FObjectInitializer::Get().CreateDefaultSubobject<UAbilitySlowNodeConfig>(Graph, TEXT("CatalystSlow"));
    Slow->Multiplier = .65f;
    Slow->Duration = 1.2f;
    Graph->Nodes.Add({SlowId, TEXT("Status.Slow"), Slow});
    Graph->Edges.Add({Graph->EntryNodeId, SlowId, TEXT("TargetFound")});
    PrependTerrainRelay(*Graph, ETerrainSurfaceType::Frost);
}

void AbilityWorkshopTerrainCatalyst::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Find;
    Find.TypeId = TEXT("World.FindOwnedSurface");
    Find.ConfigClass = UAbilityFindOwnedSurfaceNodeConfig::StaticClass();
    Find.ExecutionClass = UAbilityFindOwnedSurfaceNodeExecution::StaticClass();
    Find.InputPayload = EAbilityPortPayload::Origin;
    Find.bSupportsSnapshotTargets = true;
    Find.bRuntimeFiniteSafe = true;
    Find.Ports = {{TEXT("Found"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Find.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Find, Error), TEXT("Terrain relay registration: %s"), *Error);
    }
}
