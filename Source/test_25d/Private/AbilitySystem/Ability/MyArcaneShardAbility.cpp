// 调用链：GAS 原生激活 -> WaitInputPress/TargetData -> PointCollection/蒙太奇 -> Notify -> 注册图策略。
// 原蓝图在首次碎片之后扣费用，在尾延迟之后提交冷却；不把这些时点改为激活时一次提交。
#include "AbilitySystem/Ability/MyArcaneShardAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardFragments.h"
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
#include "MyGameplayTags.h"

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
    const auto AbilityTag = FMyGameplayTags::Get().Abilities_Arcane_ArcaneShards;
    if (GetAssetTags().HasTagExact(AbilityTag))
    {
        Config->EquipmentAbilityTag = AbilityTag;
    }
    // 原 GA 仅消费片段/完成；不为每个原始 Actor 命中制造无消费者事件，避免人群数量耗尽图预算。
    Config->bPublishDamageResults = false;
    return Config;
}

UAbilityGraphDefinition* UMyArcaneShardAbility::BuildRuntimeBaseGraph(UObject* Outer, FString& Error) const
{
    Error.Reset();
    return AbilityArcaneShardFragments::Build(Outer, *MakeShardConfig(GetTransientPackage()));
}

void UMyArcaneShardAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
    bConfirmedTarget = false;
    bShardStarted = false;
    bShardCostStageFinished = false;
    PendingShardEnd.Reset();
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
        const UAbilityArcaneShardNodeConfig* PointConfig = nullptr;
        if (const auto* Frozen = GetRuntimeRecipeSnapshot().Graph.Get())
        {
            // 点的数量与间隔必须来自同一冻结版本；替换为其他行为时不创建无用的旧点集合。
            PointConfig = AbilityArcaneShardFragments::FindSettings(*Frozen);
        }
        else
        {
            PointConfig = MakeShardConfig(this);
        }
        if (!GetRuntimeRecipeError().IsEmpty())
        {
            EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
            return;
        }
        if (PointConfig)
        {
            PreparedPoints = UAbilityArcaneShardNodeExecution::PreparePoints(this, ShardInput, *PointConfig, Error);
            if (!PreparedPoints)
            {
                EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
                return;
            }
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
    if ((!PreparedPoints && !GetRuntimeRecipeSnapshot().Graph) || !WorldOwner)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    FString Error;
    auto* Graph =
        GetRuntimeRecipeSnapshot().Graph ? GetRuntimeRecipeSnapshot().Graph.Get() : BuildRuntimeBaseGraph(this, Error);
    ShardInput.DamageParams = MakeDamageEffectParamsFromClassDefaults(nullptr, ShardInput.Origin);
    ShardInput.Context = PreparedPoints;
    ShardGraph = NewObject<UAbilityGraphInstance>(GetWorld());
    const TWeakObjectPtr<UMyArcaneShardAbility> WeakThis(this);
    const auto Session = GetExecutionSession();
    GraphFinishedHandle = ShardGraph->OnFinished.AddLambda(
        [WeakThis, Session](EAbilityGraphEndReason Reason)
        {
            if (auto* Self = WeakThis.Get(); Self && Self->IsActive() && Self->GetExecutionSession() == Session)
            {
                Self->OnShardGraphFinished(Reason);
            }
        });
    const TStrongObjectPtr<UAbilityGraphInstance> StartingGraph(ShardGraph.Get());
    ObserveFrozenRuntimeGraph(StartingGraph.Get(), ShardInput.SourceToken);
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = Graph->BudgetPolicy;
    Options.OwnerLease = GetRuntimeExecutionOwnerLease();
    Options.CompatibilityProfile = Graph->CompatibilityProfile;
    const bool bStarted = StartingGraph->StartWithInput(Graph, ShardInput, Options, Error);
    if (!IsActive())
    {
        return;
    }
    if (!bStarted || !WorldOwner->Adopt(StartingGraph.Get(), Error))
    {
        ForgetFrozenRuntimeGraph(StartingGraph.Get(), ShardInput.SourceToken);
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    if (IsActive())
    {
        // 原蓝图忽略 CommitCost 返回值；保持首次效果之后的单次费用尝试，不追加第二次完整 Commit。
        CommitAbilityCost(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo);
        if (IsActive() && GetExecutionSession() == Session)
        {
            bShardCostStageFinished = true;
            if (PendingShardEnd.IsSet())
            {
                const auto Reason = PendingShardEnd.GetValue();
                PendingShardEnd.Reset();
                OnShardGraphFinished(Reason);
            }
        }
    }
}

void UMyArcaneShardAbility::OnShardGraphFinished(EAbilityGraphEndReason Reason)
{
    if (!bShardCostStageFinished)
    {
        PendingShardEnd = Reason;
        return;
    }
    const auto Session = GetExecutionSession();
    if (Reason == EAbilityGraphEndReason::Completed)
    {
        CommitAbilityCooldown(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false);
    }
    if (IsActive() && GetExecutionSession() == Session)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true,
                   Reason != EAbilityGraphEndReason::Completed);
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
