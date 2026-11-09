// 调用链：原 GA_MeleeAttack 蓝图继承本类 -> 原生动画任务 -> 一次 Contact 图 -> 命中特效 -> GAS 清理任务。
#include "AbilitySystem/Ability/MyMeleeAttack.h"
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Interface/IEnemyInterface.h"
#include "UObject/StrongObjectPtr.h"

void UMyMeleeAttack::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                     FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
    bContactConsumed = false;
    auto* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
    FString Error;
    if (!Avatar || !Avatar->Implements<UCombatInterface>() ||
        (ActorInfo->IsNetAuthority() && !AbilityExecutionIdentity::CaptureSource(Avatar, ContactSource, Error)))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    if (Avatar->Implements<UEnemyInterface>())
    {
        if (auto* Target = IEnemyInterface::Execute_GetCombatTarget(Avatar))
        {
            ICombatInterface::Execute_UpdateFacingTarget(Avatar, Target->GetActorLocation());
        }
    }
    SelectedMontage = GetRandomTaggedMontageFromArray(ICombatInterface::Execute_GetAttackMontages(Avatar));
    if (!SelectedMontage.Montage || !SelectedMontage.MontageTag.IsValid())
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    // 与旧图一致不新增 Commit；普通怪的新预警 GA 继续沿用 TelegraphedEnemyAbility。
    auto* Event =
        UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, SelectedMontage.MontageTag, nullptr, true, true);
    Event->EventReceived.AddDynamic(this, &ThisClass::OnContact);
    Event->ReadyForActivation();
    auto* Montage = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
        this, NAME_None, SelectedMontage.Montage, 1.f, NAME_None, true);
    Montage->OnCompleted.AddDynamic(this, &ThisClass::OnAnimationEnded);
    Montage->OnBlendOut.AddDynamic(this, &ThisClass::OnAnimationEnded);
    Montage->OnCancelled.AddDynamic(this, &ThisClass::OnAnimationInterrupted);
    Montage->OnInterrupted.AddDynamic(this, &ThisClass::OnAnimationInterrupted);
    Montage->ReadyForActivation();
}

void UMyMeleeAttack::OnContact(FGameplayEventData Payload)
{
    const auto Session = GetExecutionSession();
    if (!IsActive() || !Session || !Session->IsSourceCurrent() || bContactConsumed || !CurrentActorInfo ||
        !CurrentActorInfo->IsNetAuthority())
    {
        return;
    }
    bContactConsumed = true;
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(ContactSource, Avatar))
    {
        OnAnimationInterrupted();
        return;
    }
    FAbilityNodeInput Input;
    Input.SourceToken = ContactSource;
    Input.Origin = ICombatInterface::Execute_GetCombatSocketLocation(Avatar, SelectedMontage.SocketTag);
    Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    Input.Level = GetAbilityLevel();
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Melee.Contact");
    auto* Config = NewObject<UAbilityMeleeNodeConfig>(Graph);
    Config->Radius = ContactRadius;
    Node.Config = Config;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    ContactGraph = NewObject<UAbilityGraphInstance>(this);
    bool bHit = false;
    const FDelegateHandle Observation = ContactGraph->OnEvent.AddLambda(
        [&bHit](const FAbilityGraphEvent& Event)
        {
            bHit |= Event.OutputPort == TEXT("DamageResolved");
        });
    FString Error;
    const TStrongObjectPtr<UAbilityGraphInstance> Executing(ContactGraph.Get());
    const bool bStarted = Executing->StartWithInput(Graph, Input, {}, Error);
    Executing->OnEvent.Remove(Observation);
    // 伤害回调可能取消后立即重新激活同一个PerActor实例；旧调用不得结束新会话。
    if (!IsActive() || GetExecutionSession() != Session || !Session->IsSourceCurrent())
    {
        if (IsActive() && GetExecutionSession() == Session)
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        }
        return;
    }
    if (bHit)
    {
        FGameplayCueParameters Cue;
        Cue.Location = Input.Origin;
        Cue.EffectCauser = Avatar;
        if (Avatar->Implements<UEnemyInterface>())
        {
            Cue.SourceObject = IEnemyInterface::Execute_GetCombatTarget(Avatar);
        }
        Cue.AggregatedSourceTags.AddTag(SelectedMontage.MontageTag);
        K2_ExecuteGameplayCueWithParams(FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.MeleeImpact")), Cue);
    }
    if (IsActive() && GetExecutionSession() == Session)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true,
                   !bStarted || !Session->IsSourceCurrent());
    }
}

void UMyMeleeAttack::OnAnimationEnded()
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

void UMyMeleeAttack::OnAnimationInterrupted()
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyMeleeAttack::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                                bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    if (ContactGraph)
    {
        ContactGraph->Cancel();
        ContactGraph = nullptr;
    }
    SelectedMontage = {};
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
