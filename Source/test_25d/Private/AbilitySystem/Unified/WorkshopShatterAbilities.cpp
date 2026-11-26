// 调用链：Ground 目标验证 -> 原生 Schema 2 图 -> 正伤害判定 -> 元素消耗服务 -> Actor/Mass 恢复。
// 伤害回执只作条件和表现；消耗节点必须拿到原生命的 LiveTarget 令牌。
#include "AbilitySystem/Unified/WorkshopShatterAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/WorkshopPropagationAbilities.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IceShatter, "Abilities.Mage.IceShatter");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IceShatterCooldown, "Cooldown.Mage.IceShatter");

bool UAbilityConsumeExposureNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Expected < EAbilityExposure::Burning || Expected > EAbilityExposure::Frozen)
    {
        Error = TEXT("Exposure consumption requires one supported nonempty elemental status.");
    }
    return Error.IsEmpty();
}

bool UAbilityConsumeExposureNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                              FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ElementalStatus)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < static_cast<float>(EAbilityExposure::Burning) ||
        Value > static_cast<float>(EAbilityExposure::Frozen) || Value != FMath::RoundToFloat(Value))
    {
        Error = TEXT("Exposure consumption requires an elemental status integer from 1 to 8.");
        return false;
    }
    Expected = static_cast<EAbilityExposure>(static_cast<uint8>(Value));
    return true;
}

bool UAbilityConsumeExposureNodeConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port,
                                                                   FString& Error) const
{
    return Type == TEXT("Status.ConsumeExposure") && Port == EAbilityPortPayload::LiveTarget && Validate(Error);
}

bool UAbilityConsumeExposureNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Config.Validate(Error))
    {
        return false;
    }
    if (!Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Exposure consumption requires the original live target.");
        return false;
    }
    const auto* Settings = CastChecked<UAbilityConsumeExposureNodeConfig>(&Config);
    const bool bConsumed = AbilityElements::Consume(Input.SourceToken, *Input.LiveTargetToken, Settings->Expected);
    // 解冻可能同步恢复 Actor 的行动。只给仍然有效的图执行实例发布后续动作。
    if (bConsumed && !bStopped && !Owner.IsFinished())
    {
        Owner.Publish(Id, TEXT("Consumed"), Input);
    }
    if (!bStopped && !Owner.IsFinished())
    {
        return Owner.Publish(Id, TEXT("Completed"), Input);
    }
    return true;
}

UWorkshopIceShatter::UWorkshopIceShatter()
{
    SetAssetTags(FGameplayTagContainer(TAG_IceShatter));
    CooldownTags.AddTag(TAG_IceShatterCooldown);
    ManaCost = 18.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "IceShatter", "狭线裂霜");
    SpellSummary = NSLOCTEXT("SkillCards", "IceShatterSummary", "沿准星击裂冻结敌人，实际命中后消耗冻结。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityAnchorNodeConfig>(Graph, TEXT("ShatterSourceAnchor"));
    Anchor->Anchor = EAbilitySpatialAnchor::Source;
    auto* Query = Initializer.CreateDefaultSubobject<UAbilityQueryNodeConfig>(Graph, TEXT("ShatterLineQuery"));
    Query->Query.Geometry = EAbilityQueryGeometry::Line;
    Query->Query.Range = 700.f;
    Query->Query.Width = 160.f;
    Query->Query.HalfHeight = 120.f;
    Query->Query.MaxTargets = 8;
    auto* Frozen = Initializer.CreateDefaultSubobject<UAbilityCombatRuleConfig>(Graph, TEXT("ShatterStatusGate"));
    Frozen->Rule = EAbilityCombatRule::ElementalStatus;
    Frozen->Value = static_cast<float>(EAbilityExposure::Frozen);
    auto* DamageNode = Initializer.CreateDefaultSubobject<UAbilityDamageNodeConfig>(Graph, TEXT("ShatterDamage"));
    // 碎冰物理伤害沿用原标签，让冻结与冻僵都可由末端消耗节点明确处理。
    DamageNode->DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
    DamageNode->Damage = 36.f;
    DamageNode->DamagePerLevel = 0.f;
    auto* Applied = Initializer.CreateDefaultSubobject<UAbilityCombatRuleConfig>(Graph, TEXT("ShatterDamageGate"));
    Applied->Rule = EAbilityCombatRule::DamageApplied;
    Applied->Value = 0.f;
    auto* Resolve =
        Initializer.CreateDefaultSubobject<UAbilityResolveImpactNodeConfig>(Graph, TEXT("ShatterResolveTarget"));
    auto* Consume =
        Initializer.CreateDefaultSubobject<UAbilityConsumeExposureNodeConfig>(Graph, TEXT("ShatterConsumeExposure"));
    auto* Visual = Initializer.CreateDefaultSubobject<UWorkshopElementImpactConfig>(Graph, TEXT("ShatterVisual"));
    Visual->System = TSoftObjectPtr<UNiagaraSystem>(
        FSoftObjectPath(TEXT("/Game/Art/CompositionEffects/NS_Frost_Impact.NS_Frost_Impact")));
    Visual->Radius = 130.f;

    const FGuid AnchorId(0, 0, 0, 1);
    const FGuid QueryId(0, 0, 0, 2);
    const FGuid FrozenId(0, 0, 0, 3);
    const FGuid DamageId(0, 0, 0, 4);
    const FGuid AppliedId(0, 0, 0, 5);
    const FGuid ResolveId(0, 0, 0, 6);
    const FGuid ConsumeId(0, 0, 0, 7);
    const FGuid VisualId(0, 0, 0, 8);
    Graph->EntryNodeId = AnchorId;
    Graph->Nodes = {{AnchorId, TEXT("Spatial.Anchor"), Anchor},
                    {QueryId, TEXT("Query.Shape"), Query},
                    {FrozenId, TEXT("Condition.ElementalStatus"), Frozen},
                    {DamageId, TEXT("Effect.Damage"), DamageNode},
                    {AppliedId, TEXT("Condition.DamageApplied"), Applied},
                    {ResolveId, TEXT("Target.ResolveDamageTarget"), Resolve},
                    {ConsumeId, TEXT("Status.ConsumeExposure"), Consume},
                    {VisualId, TEXT("Presentation.ElementImpact"), Visual}};
    Graph->Edges = {{AnchorId, QueryId, TEXT("Completed")},      {QueryId, FrozenId, TEXT("TargetFound")},
                    {FrozenId, DamageId, TEXT("Passed")},        {DamageId, AppliedId, TEXT("DamageResolved")},
                    {AppliedId, ResolveId, TEXT("Passed")},      {ResolveId, ConsumeId, TEXT("TargetFound")},
                    {DamageId, VisualId, TEXT("DamageResolved")}};
}

void WorkshopShatterAbilities::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Status.ConsumeExposure");
    if (Registry.Find(Descriptor.TypeId))
    {
        return;
    }
    Descriptor.ConfigClass = UAbilityConsumeExposureNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityConsumeExposureNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::LiveTarget;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = true;
    Descriptor.bRuntimeFiniteSafe = Descriptor.bRuntimePurePayload = true;
    Descriptor.bPrototypeCompatibilitySafe = Descriptor.bSustainedSafe = true;
    Descriptor.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Descriptor.Ports = {{TEXT("Consumed"), EAbilityPortPayload::LiveTarget, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Exposure consumption registration: %s"), *Error);
}
