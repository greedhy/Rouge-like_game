// 调用链：GAS激活 -> 原召唤点/VFX/蒙太奇 -> Notify启动统一召唤节点 -> 正常结束或取消清理任务。
#include "AbilitySystem/Ability/SummonAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

USummonAbility::USummonAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    // 资产迁移显式设置既有AM/NS引用；构造CDO时不得提前加载蓝图并抢在原生标签注册之前反序列化。
}

TArray<FVector> USummonAbility::GetSpawnLocations()
{
    FAbilitySourceToken Source;
    FString Error;
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!AbilityExecutionIdentity::CaptureSource(Avatar, Source, Error))
    {
        return {};
    }
    return AbilityTraditionalActions::GatherSummonLocations(Source, Avatar->GetActorLocation(),
                                                            Avatar->GetActorForwardVector(), NumMinions,
                                                            MinSpawnDistance, MaxSpawnDistance, SpawnSpread);
}

TSubclassOf<APawn> USummonAbility::GetRandomMinionClass()
{
    return AbilityTraditionalActions::ChooseMinionClass(MinionClasses);
}

APawn* USummonAbility::SpawnSummonedPawn(TSubclassOf<APawn> Class, const FTransform& SpawnTransform)
{
    const auto Session = GetExecutionSession();
    FAbilitySourceToken Source;
    FString Error;
    if (!IsActive() || !Session || !Session->IsSourceCurrent() ||
        !AbilityExecutionIdentity::CaptureSource(GetAvatarActorFromActorInfo(), Source, Error))
    {
        return nullptr;
    }
    return AbilityTraditionalActions::SpawnSummonedPawn(Source, Class, SpawnTransform);
}

void USummonAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                     FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    // GAS PreActivate已建立会话。不能调用父Activate再次运行仍保留在资产中的旧K2事件图。
    bSummonEnding = false;
    bSummonNotifyConsumed = false;
    SummonSource = {};
    if (!Info || !Info->IsNetAuthority())
    {
        return;
    }
    FString Error;
    if (!AbilityExecutionIdentity::CaptureSource(Info->AvatarActor.Get(), SummonSource, Error))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    SummonSnapshot = NewObject<UAbilitySummonNodeConfig>(this);
    SummonSnapshot->MinionClasses = MinionClasses;
    SummonSnapshot->Count = NumMinions;
    SummonSnapshot->MinDistance = MinSpawnDistance;
    SummonSnapshot->MaxDistance = MaxSpawnDistance;
    SummonSnapshot->Spread = SpawnSpread;
    SummonSnapshot->Interval = SummonSpawnInterval;
    SummonSnapshot->SpawnHeight = SummonSpawnHeight;
    if (!SummonSnapshot->Validate(Error) || !SummonMontage ||
        (bCommitOnActivation && !CommitAbility(Handle, Info, Activation)))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    AActor* Avatar = nullptr;
    if (!IsActive() || !AbilityExecutionIdentity::ResolveSource(SummonSource, Avatar))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    SummonSnapshot->PreparedLocations = AbilityTraditionalActions::GatherSummonLocations(
        SummonSource, Avatar->GetActorLocation(), Avatar->GetActorForwardVector(), NumMinions, MinSpawnDistance,
        MaxSpawnDistance, SpawnSpread);
    for (const FVector& Location : SummonSnapshot->PreparedLocations)
    {
        if (auto* Warning = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SummonGroundEffect, Location))
        {
            SummonWarnings.Add(Warning);
        }
    }
    const FGameplayTag Notify =
        SummonEventTag.IsValid() ? SummonEventTag : FGameplayTag::RequestGameplayTag(TEXT("Montage.Attack.2"));
    SummonEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Notify, nullptr, true, true);
    SummonEventTask->EventReceived.AddDynamic(this, &ThisClass::OnSummonNotify);
    SummonEventTask->ReadyForActivation();
    SummonMontageTask =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, TEXT("SummonMontage"), SummonMontage);
    SummonMontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnSummonMontageEnded);
    SummonMontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnSummonMontageEnded);
    SummonMontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnSummonMontageCancelled);
    SummonMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnSummonMontageCancelled);
    SummonMontageTask->ReadyForActivation();
}

void USummonAbility::OnSummonNotify(FGameplayEventData Payload)
{
    AActor* Avatar = nullptr;
    if (!IsActive() || bSummonEnding || bSummonNotifyConsumed || !SummonSnapshot ||
        !AbilityExecutionIdentity::ResolveSource(SummonSource, Avatar))
    {
        return;
    }
    bSummonNotifyConsumed = true;
    const auto Session = GetExecutionSession();
    if (!Session || !Session->PublishSignal(TEXT("Notify"), Payload.EventTag))
    {
        return;
    }
    if (!IsActive() || bSummonEnding || !SummonSnapshot || !Session->IsSourceCurrent())
    {
        return;
    }
    auto* Definition = NewObject<UAbilityGraphDefinition>(this);
    Definition->SchemaVersion = 2;
    Definition->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Spawn.Summon");
    Node.Config = SummonSnapshot;
    Definition->Nodes.Add(Node);
    Definition->EntryNodeId = Node.NodeId;
    FAbilityNodeInput Input;
    Input.SourceToken = SummonSource;
    Input.Origin = Avatar->GetActorLocation();
    Input.Direction = Avatar->GetActorForwardVector();
    Input.Level = GetAbilityLevel();
    SummonGraphTask = UAbilityTask_RunGraph::RunAbilityGraphWithInput(this, Definition, Input, {});
    SummonGraphTask->OnFinished.AddDynamic(this, &ThisClass::OnSummonGraphFinished);
    SummonGraphTask->ReadyForActivation();
}

void USummonAbility::OnSummonMontageEnded()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void USummonAbility::OnSummonMontageCancelled()
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void USummonAbility::OnSummonGraphFinished(bool bCancelled)
{
    // 正常生成结束仍保留原蒙太奇后摇；图失败必须结束宿主，不能继续接受通知。
    if (bCancelled)
    {
        OnSummonMontageCancelled();
    }
}

void USummonAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (bSummonEnding)
    {
        return;
    }
    bSummonEnding = true;
    CloseExecutionSession(bCancelled);
    if (SummonEventTask)
    {
        SummonEventTask->EventReceived.RemoveAll(this);
        SummonEventTask->EndTask();
        SummonEventTask = nullptr;
    }
    if (SummonGraphTask)
    {
        SummonGraphTask->OnFinished.RemoveAll(this);
        SummonGraphTask->EndTask();
        SummonGraphTask = nullptr;
    }
    if (SummonMontageTask)
    {
        SummonMontageTask->OnCompleted.RemoveAll(this);
        SummonMontageTask->OnBlendOut.RemoveAll(this);
        SummonMontageTask->OnCancelled.RemoveAll(this);
        SummonMontageTask->OnInterrupted.RemoveAll(this);
        // 主动EndTask不带bAbilityEnded，须显式停止本GA仍持有的蒙太奇。
        if (Info && Info->AbilitySystemComponent.IsValid() &&
            Info->AbilitySystemComponent->GetAnimatingAbility() == this)
        {
            Info->AbilitySystemComponent->CurrentMontageStop(.1f);
        }
        SummonMontageTask->EndTask();
        SummonMontageTask = nullptr;
    }
    for (UNiagaraComponent* Warning : SummonWarnings)
    {
        if (IsValid(Warning))
        {
            Warning->DestroyComponent();
        }
    }
    SummonWarnings.Reset();
    SummonSnapshot = nullptr;
    SummonSource = {};
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
