// 调用链：GAS 原生激活 -> WaitInputPress/TargetData -> PointCollection/蒙太奇 -> Notify -> 注册图策略。
// 原蓝图在首次碎片之后扣费用，在尾延迟之后提交冷却；不把这些时点改为激活时一次提交。
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Actor/PointCollection.h"
#include "Interface/PlayerInterface.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

UMyArcaneShardAbility::UMyArcaneShardAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

UAbilityArcaneShardNodeConfig* UMyArcaneShardAbility::MakeShardConfig(UObject* Outer) const
{
    auto* Config = NewObject<UAbilityArcaneShardNodeConfig>(Outer);
    Config->PointCollectionClass = ShardPointCollectionClass;
    Config->MaxPoints = MaxShardPoints;
    Config->Interval = ShardInterval;
    Config->CueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.ArcaneShards"));
    // 原 GA 仅消费片段/完成；不为每个原始 Actor 命中制造无消费者事件，避免人群数量耗尽图预算。
    Config->bPublishDamageResults = false;
    return Config;
}

void UMyArcaneShardAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
    bConfirmedTarget = false;
    bShardStarted = false;
    if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !ShardMontage || !ShardPointCollectionClass ||
        !CheckCost(Handle, ActorInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    auto* Avatar = ActorInfo->AvatarActor.Get();
    if (ActorInfo->IsLocallyControlled() && Avatar->Implements<UPlayerInterface>())
    {
        IPlayerInterface::Execute_ShowMagicCircle(Avatar, nullptr);
    }
    auto* Press = UAbilityTask_WaitInputPress::WaitInputPress(this, false);
    Press->OnPress.AddDynamic(this, &ThisClass::OnConfirmPress);
    Press->ReadyForActivation();
}

void UMyArcaneShardAbility::OnConfirmPress(float TimeWaited)
{
    if (!IsActive() || bConfirmedTarget)
    {
        return;
    }
    auto* TargetTask = UTargetDataUnderMouse::CreateTargetDataUnderMouse(this);
    TargetTask->ValidData.AddDynamic(this, &ThisClass::OnShardTargetData);
    TargetTask->ReadyForActivation();
}

void UMyArcaneShardAbility::OnShardTargetData(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bConfirmedTarget || Data.Num() == 0 || !Data.Get(0) || !Data.Get(0)->GetHitResult())
    {
        return;
    }
    const FVector Location = Data.Get(0)->GetHitResult()->ImpactPoint;
    if (Location.ContainsNaN())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    bConfirmedTarget = true;
    auto* Avatar = GetAvatarActorFromActorInfo();
    if (CurrentActorInfo->IsNetAuthority())
    {
        FString Error;
        ShardInput.Origin = Location;
        ShardInput.Level = GetAbilityLevel();
        if (!AbilityExecutionIdentity::CaptureSource(Avatar, ShardInput.SourceToken, Error))
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
            return;
        }
        PreparedPoints =
            UAbilityArcaneShardNodeExecution::PreparePoints(this, ShardInput, *MakeShardConfig(this), Error);
        if (!PreparedPoints)
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
            return;
        }
    }
    if (Avatar && Avatar->Implements<UCombatInterface>())
    {
        ICombatInterface::Execute_UpdateFacingTarget(Avatar, Location);
    }
    // 先绑定一次性 Notify，避免非常靠前的动画事件在创建等待任务之前丢失。
    auto* Wait = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
        this, FGameplayTag::RequestGameplayTag(TEXT("Event.Montage.AracaneShards")), nullptr, true, true);
    Wait->EventReceived.AddDynamic(this, &ThisClass::OnShardNotify);
    Wait->ReadyForActivation();
    auto* Montage = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, ShardMontage, 1.f,
                                                                                   NAME_None, false);
    Montage->OnCancelled.AddDynamic(this, &ThisClass::OnMontageInterrupted);
    Montage->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
    Montage->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
    Montage->ReadyForActivation();
}

void UMyArcaneShardAbility::OnShardNotify(FGameplayEventData Payload)
{
    if (!IsActive() || bShardStarted)
    {
        return;
    }
    bShardStarted = true;
    HideCircle();
    if (!CurrentActorInfo->IsNetAuthority())
    {
        return;
    }
    auto* WorldOwner = GetWorld()->GetSubsystem<UAbilityGraphWorldSubsystem>();
    if (!PreparedPoints || !WorldOwner)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Shape.ArcaneShards");
    Node.Config = MakeShardConfig(Graph);
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    ShardInput.DamageParams = MakeDamageEffectParamsFromClassDefaults(nullptr, ShardInput.Origin);
    ShardInput.Context = PreparedPoints;
    ShardGraph = NewObject<UAbilityGraphInstance>(GetWorld());
    const TWeakObjectPtr<UMyArcaneShardAbility> WeakThis(this);
    GraphFinishedHandle = ShardGraph->OnFinished.AddLambda(
        [WeakThis](EAbilityGraphEndReason Reason)
        {
            if (auto* Self = WeakThis.Get(); Self && Self->IsActive())
            {
                if (Reason == EAbilityGraphEndReason::Completed)
                {
                    Self->CommitAbilityCooldown(Self->CurrentSpecHandle, Self->CurrentActorInfo,
                                                Self->CurrentActivationInfo, false);
                }
                Self->EndAbility(Self->CurrentSpecHandle, Self->CurrentActorInfo, Self->CurrentActivationInfo, true,
                                 Reason != EAbilityGraphEndReason::Completed);
            }
        });
    FString Error;
    const TStrongObjectPtr<UAbilityGraphInstance> StartingGraph(ShardGraph.Get());
    const bool bStarted = StartingGraph->StartWithInput(Graph, ShardInput, {}, Error);
    if (!IsActive())
    {
        return;
    }
    if (!bStarted || !WorldOwner->Adopt(StartingGraph.Get(), Error))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    if (IsActive())
    {
        // 原蓝图忽略 CommitCost 返回值；保持首次效果之后的单次费用尝试，不追加第二次完整 Commit。
        CommitAbilityCost(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo);
    }
}

void UMyArcaneShardAbility::HideCircle()
{
    if (!CurrentActorInfo)
    {
        return;
    }
    auto* Avatar = GetAvatarActorFromActorInfo();
    if (Avatar && Avatar->Implements<UPlayerInterface>())
    {
        IPlayerInterface::Execute_HideMagicCircle(Avatar);
    }
}

void UMyArcaneShardAbility::OnMontageInterrupted()
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyArcaneShardAbility::OnMontageFinished()
{
    if (IsActive() && !bShardStarted)
    {
        // 原图缺失 Notify 会永久挂起；未生效时关闭等待和预览，不补伤害或费用。
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}

void UMyArcaneShardAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                       FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                                       bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    if (ShardGraph)
    {
        ShardGraph->OnFinished.Remove(GraphFinishedHandle);
        ShardGraph->Cancel();
        ShardGraph = nullptr;
    }
    if (PreparedPoints && IsValid(PreparedPoints->PointCollection))
    {
        PreparedPoints->PointCollection->Destroy();
    }
    PreparedPoints = nullptr;
    HideCircle();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
