// 调用链：服务端冻结原生图 -> 既有节点发布 LiveTarget -> DamageResolved -> ASC 复制纯表现。
// 施法图拥有波前时钟，命中状态和粒子寿命继续由各自服务管理。
#include "AbilitySystem/Unified/WorkshopPropagationAbilities.h"

#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElementalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostSurge, "Abilities.Mage.FrostSurge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FrostSurgeCooldown, "Cooldown.Mage.FrostSurge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WetArc, "Abilities.Mage.WetArc");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_WetArcCooldown, "Cooldown.Mage.WetArc");

namespace
{
    /** 两个原生默认图共用伤害/表现后继，并保证配置被图快照深拷贝。 */
    void ConfigureImpactRecipe(UAbilityGraphDefinition& Graph, FName RootType, UAbilityNodeConfig* Root,
                               EAbilityElement Element, float Damage, const TCHAR* VisualPath)
    {
        const auto& Initializer = FObjectInitializer::Get();
        auto* DamageConfig = Initializer.CreateDefaultSubobject<UAbilityDamageNodeConfig>(&Graph, TEXT("HitDamage"));
        DamageConfig->Damage = Damage;
        DamageConfig->DamagePerLevel = 0.f;
        DamageConfig->ElementOverride = Element;
        auto* Visual = Initializer.CreateDefaultSubobject<UWorkshopElementImpactConfig>(&Graph, TEXT("HitVisual"));
        Visual->System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(VisualPath));

        const FGuid RootId(0, 0, 0, 1);
        const FGuid DamageId(0, 0, 0, 2);
        const FGuid VisualId(0, 0, 0, 3);
        Graph.EntryNodeId = RootId;
        Graph.Nodes = {{RootId, RootType, Root},
                       {DamageId, TEXT("Effect.Damage"), DamageConfig},
                       {VisualId, TEXT("Presentation.ElementImpact"), Visual}};
        Graph.Edges = {{RootId, DamageId, TEXT("TargetFound")}, {DamageId, VisualId, TEXT("DamageResolved")}};
    }
} // namespace

bool UWorkshopElementImpactConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (System.IsNull() || !FMath::IsFinite(Radius) || Radius < 1.f || Radius > 3000.f)
    {
        Error = TEXT("Element impact needs an explicit Niagara asset and visual radius from 1 to 3000.");
    }
    return Error.IsEmpty();
}

bool UWorkshopElementImpactConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    if (Parameter != EAbilityConfigParameter::Radius)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < 1.f || Value > 3000.f)
    {
        Error = TEXT("Element impact visual radius must be from 1 to 3000.");
        return false;
    }
    Radius = Value;
    return true;
}

bool UWorkshopElementImpactExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Settings = CastChecked<UWorkshopElementImpactConfig>(&Config);
    AActor* Avatar = nullptr;
    if (Input.bDamageAttempted && Input.Impact.IsSet() && Input.ActualDamage.Get(0.f) > 0.f &&
        AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get()))
        {
            ASC->MulticastPlayElementPulse(Settings->System.ToSoftObjectPath(), Input.Impact->Location,
                                           Settings->Radius);
        }
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopFrostSurge::UWorkshopFrostSurge()
{
    SetAssetTags(FGameplayTagContainer(TAG_FrostSurge));
    CooldownTags.AddTag(TAG_FrostSurgeCooldown);
    ManaCost = 22.f;
    Cooldown = 8.f;
    SpellName = NSLOCTEXT("SkillCards", "FrostSurge", "霜锋潮");
    SpellSummary =
        NSLOCTEXT("SkillCards", "FrostSurgeSummary", "从落点向外推进冰霜波前，最远 4.8 米，最多命中 12 个敌人。");

    auto* Wave =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityWavefrontNodeConfig>(Graph, TEXT("FrostWave"));
    Wave->Radius = 480.f;
    Wave->Speed = 900.f;
    Wave->Duration = 1.f;
    Wave->MaxTargets = 12;
    Wave->Profile = EAbilityWaveQueryProfile::Batch6Front3D;
    ConfigureImpactRecipe(*Graph, TEXT("Spatial.Wavefront"), Wave, EAbilityElement::Ice, 25.f,
                          TEXT("/Game/Art/CompositionEffects/NS_Frost_Impact.NS_Frost_Impact"));

    // 起手提示和逐目标命中特效分别消费位置、伤害结果，空场也保留起手提示。
    auto* Surface =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilityElementPulseVisualConfig>(Graph, TEXT("FrostSurface"));
    Surface->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Art/TerrainSurfaces/Effects/NS_Frost_Surface.NS_Frost_Surface")));
    const FGuid SurfaceId(0, 0, 0, 4);
    Graph->Nodes.Add({SurfaceId, TEXT("Presentation.ElementPulse"), Surface});
    Graph->Edges.Add({Graph->EntryNodeId, SurfaceId, TEXT("Started")});
}

UWorkshopWetArc::UWorkshopWetArc()
{
    SetAssetTags(FGameplayTagContainer(TAG_WetArc));
    CooldownTags.AddTag(TAG_WetArcCooldown);
    ManaCost = 14.f;
    Cooldown = 6.f;
    SpellName = NSLOCTEXT("SkillCards", "WetArc", "潮印放电");
    SpellSummary = NSLOCTEXT("SkillCards", "WetArcSummary", "向落点 4.25 米内最近的最多 3 个潮湿敌人放电。");

    auto* Query =
        FObjectInitializer::Get().CreateDefaultSubobject<UAbilitySelectTargetsConfig>(Graph, TEXT("WetTargetQuery"));
    Query->Radius = 425.f;
    Query->MaxTargets = 3;
    Query->TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
    // 已登记条件筛选在 0..3 保留通用条件，之后才按元素状态枚举编号。
    Query->TargetFilter = 3 + static_cast<uint8>(EAbilityExposure::Wet);
    ConfigureImpactRecipe(
        *Graph, TEXT("Query.SelectTargets"), Query, EAbilityElement::Thunder, 22.f,
        TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Lightning.NS_Free_Spells_Buff_Lightning"));
}

void WorkshopPropagationAbilities::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    const FName Type(TEXT("Presentation.ElementImpact"));
    if (Registry.Find(Type))
    {
        return;
    }
    FAbilityNodeDescriptor Impact;
    Impact.TypeId = Type;
    Impact.ConfigClass = UWorkshopElementImpactConfig::StaticClass();
    Impact.ExecutionClass = UWorkshopElementImpactExecution::StaticClass();
    Impact.InputPayload = EAbilityPortPayload::DamageResult;
    Impact.bSupportsActorTargets = Impact.bSupportsMassTargets = Impact.bSupportsSnapshotTargets = true;
    Impact.bRuntimeFiniteSafe = true;
    Impact.Ports = {{TEXT("Completed"), EAbilityPortPayload::DamageResult, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Impact, Error), TEXT("Element impact registration: %s"), *Error);
}
