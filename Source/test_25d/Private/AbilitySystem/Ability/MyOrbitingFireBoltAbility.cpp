// 调用链：GAS提交一次激活费用 -> 注册Channel.OrbitingFireBolts策略 -> 图结束/外部取消 -> GA释放桥接引用。
#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

UMyOrbitingFireBoltAbility::UMyOrbitingFireBoltAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    Damage = FScalableFloat(10.f);
}

bool UMyOrbitingFireBoltAbility::CheckCost(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           FGameplayTagContainer* OptionalRelevantTags) const
{
    const auto* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
    return Config && ASC && ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()) &&
           ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= Config->ActivationManaCost &&
           Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
}

void UMyOrbitingFireBoltAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo) const
{
    // CommitAbility invokes this once, after CheckCost has accepted the activation.
    Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
    SpendMana(Config->ActivationManaCost);
}

bool UMyOrbitingFireBoltAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                    const FGameplayAbilityActorInfo* ActorInfo,
                                                    const FGameplayTagContainer* SourceTags,
                                                    const FGameplayTagContainer* TargetTags,
                                                    FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() ||
        !Cast<UMyAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) || !Config ||
        !Config->IsValidConfiguration() || !DamageEffectClass || !DamageType.IsValid())
    {
        return false;
    }
    AActor* Avatar = ActorInfo->AvatarActor.Get();
    return Avatar->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Avatar) &&
           Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

bool UMyOrbitingFireBoltAbility::SpendMana(float Amount) const
{
    return UAbilityOrbitNodeExecution::SpendMana(GetAbilitySystemComponentFromActorInfo(),
                                                 GetAvatarActorFromActorInfo(), GetAbilityLevel(), Amount);
}

void UMyOrbitingFireBoltAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                                 const FGameplayAbilityActorInfo* ActorInfo,
                                                 FGameplayAbilityActivationInfo ActivationInfo,
                                                 const FGameplayEventData* TriggerEventData)
{
    const auto Session = GetExecutionSession();
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !Session || !Session->IsSourceCurrent() ||
        !CanActivateAbility(Handle, ActorInfo) || !CommitAbility(Handle, ActorInfo, ActivationInfo) || !IsActive() ||
        !Session->IsSourceCurrent())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    FAbilityNodeInput Input;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureSource(ActorInfo->AvatarActor.Get(), Input.SourceToken, Error))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Channel.OrbitingFireBolts");
    auto* NodeConfig = NewObject<UAbilityOrbitNodeConfig>(Graph);
    NodeConfig->OrbitSettings = DuplicateObject<UOrbitingFireBoltConfig>(Config, NodeConfig);
    Node.Config = NodeConfig;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    OrbitContext = NewObject<UAbilityOrbitContext>(this);
    OrbitContext->SourceAbility = this;
    OrbitContext->AbilityTags = GetAssetTags();
    Input.Context = OrbitContext;
    Input.Level = GetAbilityLevel();
    Input.Origin = ActorInfo->AvatarActor->GetActorLocation();
    Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    OrbitGraph = NewObject<UAbilityGraphInstance>(GetWorld());
    const TWeakObjectPtr<UMyOrbitingFireBoltAbility> WeakThis(this);
    GraphFinishedHandle = OrbitGraph->OnFinished.AddLambda(
        [WeakThis](EAbilityGraphEndReason Reason)
        {
            if (auto* Self = WeakThis.Get(); Self && Self->IsActive())
            {
                Self->EndAbility(Self->CurrentSpecHandle, Self->CurrentActorInfo, Self->CurrentActivationInfo, true,
                                 Reason != EAbilityGraphEndReason::Completed);
            }
        });
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    const TStrongObjectPtr<UAbilityGraphInstance> Starting(OrbitGraph.Get());
    auto* WorldOwner = GetWorld()->GetSubsystem<UAbilityGraphWorldSubsystem>();
    if (!Starting->StartWithInput(Graph, Input, Options, Error) || !WorldOwner ||
        !WorldOwner->Adopt(Starting.Get(), Error))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
    }
}

void UMyOrbitingFireBoltAbility::EndAbility(FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                                            bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    if (OrbitGraph)
    {
        OrbitGraph->OnFinished.Remove(GraphFinishedHandle);
        OrbitGraph->Cancel();
        OrbitGraph = nullptr;
    }
    OrbitContext = nullptr;
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UMyOrbitingFireBoltAbility::GetLiveFireBoltCount() const
{
    const auto* Execution = OrbitContext ? OrbitContext->Execution.Get() : nullptr;
    return Execution ? Execution->GetLiveCount() : 0;
}

int32 UMyOrbitingFireBoltAbility::GetLaunchedFireBoltCount() const
{
    const auto* Execution = OrbitContext ? OrbitContext->Execution.Get() : nullptr;
    return Execution ? Execution->GetLaunchedCount() : 0;
}

AActor* UMyOrbitingFireBoltAbility::GetAttackTarget() const
{
    const auto* Execution = OrbitContext ? OrbitContext->Execution.Get() : nullptr;
    return Execution ? Execution->GetAttackTarget() : nullptr;
}

FString UMyOrbitingFireBoltAbility::GetDescription(int32 Level)
{
    return FString::Printf(
        TEXT("<Title>ORBITING FIRE BOLTS</>\n"
             "<Small>Level: </><Level>%d</><Small> | MP: </><ManaCost>%.0f</><Small> + </><ManaCost>%.0f/s</>\n"
             "<Default>Toggle: 1 bolt every %.1fs, max %d.\n"
             "Attacks launch bolts for </><Damage>%.0f</><Default> fire damage.</>"),
        Level, Config ? Config->ActivationManaCost : 10.f, Config ? Config->ManaPerSecond : 1.f,
        Config ? Config->SpawnInterval : 3.f, Config ? Config->MaxFireBolts : 3, Damage.GetValueAtLevel(Level));
}

FString UMyOrbitingFireBoltAbility::GetNextLevelDescription(int32 Level)
{
    return FString::Printf(TEXT("<Title>NEXT LEVEL</>\n"
                                "<Small>Level: </><Level>%d</>\n"
                                "<Small>Fire damage: </><Damage>%.0f</>"),
                           Level, Damage.GetValueAtLevel(Level));
}
