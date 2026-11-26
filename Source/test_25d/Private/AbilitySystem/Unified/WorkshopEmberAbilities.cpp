// 调用链：试装 T 键 -> 统一 Commit -> 等待/选敌 -> 活目标位置 -> 原混合查询与伤害服务。
// 图拥有引信，关闭时停止后续爆裂；客户端从同一位置查询的结果播放 Niagara。
#include "AbilitySystem/Unified/WorkshopEmberAbilities.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberDetonation, "Abilities.Mage.EmberDetonation");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberDetonationCooldown, "Cooldown.Mage.EmberDetonation");

bool UAbilityTargetOriginConfig::SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port, FString& Error) const
{
    Error.Reset();
    return Type == TEXT("Spatial.TargetOrigin") && Port == EAbilityPortPayload::LiveTarget;
}

bool UAbilityTargetOriginExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto Location = Input;
    if (Input.LiveTargetToken.IsSet() &&
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *Input.LiveTargetToken, Location.Origin))
    {
        Location.EventCenter = Location.Origin;
        if (CastChecked<UAbilityTargetOriginConfig>(&Config)->bRememberExcludedTarget)
        {
            Location.ExcludedAnchorTarget = Input.LiveTargetToken->Target;
        }
        Location.LiveTargetToken.Reset();
        Location.Impact.Reset();
        Location.ShapeRadius.Reset();
        Owner.Publish(Id, TEXT("Located"), Location);
    }
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

UWorkshopEmberDetonation::UWorkshopEmberDetonation()
{
    SetAssetTags(FGameplayTagContainer(TAG_EmberDetonation));
    CooldownTags.AddTag(TAG_EmberDetonationCooldown);
    ManaCost = 22.f;
    Cooldown = 8.f;
    SpellName = NSLOCTEXT("SkillCards", "EmberDetonation", "灼印迸火");
    SpellSummary = NSLOCTEXT("SkillCards", "EmberDetonationSummary", "短暂延迟后以附近最多两名灼热敌人为中心爆裂。");
    ConfigureArea(180.f, 20.f, EAbilityElement::Fire,
                  TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
    const FGuid BlastId = Graph->EntryNodeId;
    CastChecked<UAbilityQueryNodeConfig>(Graph->Nodes[0].Config)->Query.MaxTargets = 5;

    const auto& Initializer = FObjectInitializer::Get();
    auto* Ignite = Initializer.CreateDefaultSubobject<UAbilityDelayNodeConfig>(Graph, TEXT("EmberFuse"));
    Ignite->DelaySeconds = .32f;
    auto* Candidates = Initializer.CreateDefaultSubobject<UAbilitySelectTargetsConfig>(Graph, TEXT("EmberCarriers"));
    Candidates->Radius = 500.f;
    Candidates->MaxTargets = 2;
    Candidates->TargetSelection = EAbilityTargetSelectionPolicy::Nearest;
    Candidates->TargetFilter = 3 + static_cast<uint8>(EAbilityExposure::Burning);
    auto* Anchor = Initializer.CreateDefaultSubobject<UAbilityTargetOriginConfig>(Graph, TEXT("EmberLocation"));

    const FGuid FuseId(0, 0, 0, 4);
    const FGuid CandidatesId(0, 0, 0, 5);
    const FGuid AnchorId(0, 0, 0, 6);
    Graph->EntryNodeId = FuseId;
    Graph->Nodes.Append({{FuseId, TEXT("Control.DelayOrigin"), Ignite},
                         {CandidatesId, TEXT("Query.SelectTargets"), Candidates},
                         {AnchorId, TEXT("Spatial.TargetOrigin"), Anchor}});
    Graph->Edges.Append({{FuseId, CandidatesId, TEXT("Completed")},
                         {CandidatesId, AnchorId, TEXT("TargetFound")},
                         {AnchorId, BlastId, TEXT("Located")}});
}

void WorkshopEmberAbilities::RegisterNodes(FAbilityNodeRegistry& Registry)
{
    const FName Type(TEXT("Spatial.TargetOrigin"));
    if (Registry.Find(Type))
    {
        return;
    }
    FAbilityNodeDescriptor Node;
    Node.TypeId = Type;
    Node.ConfigClass = UAbilityTargetOriginConfig::StaticClass();
    Node.ExecutionClass = UAbilityTargetOriginExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::LiveTarget;
    Node.bSupportsActorTargets = Node.bSupportsMassTargets = true;
    Node.bRuntimeFiniteSafe = Node.bRuntimePurePayload = true;
    Node.bSustainedSafe = Node.bPrototypeCompatibilitySafe = true;
    Node.Ports = {{TEXT("Located"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("Target origin registration: %s"), *Error);
}
