// 调用链：GA创建并绑定任务 -> Activate -> ASC一次播放/监听 -> Tick变速/Notify校验 -> Finish -> GA收尾。
#include "AbilitySystem/Timeline/AbilityTask_RunTimeline.h"
#include "AbilitySystem/Timeline/AnimNotify_AbilityTimelineEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

// 任务由GameplayTasks组件逐帧更新，无独立全局ticker或持久单例。
UAbilityTask_RunTimeline::UAbilityTask_RunTimeline()
{
    bTickingTask = true;
}

// 仅保存GC可追踪的资产引用；实际播放发生于Activate，留出绑定回调的时机。
UAbilityTask_RunTimeline* UAbilityTask_RunTimeline::RunAbilityTimeline(UGameplayAbility* OwningAbility,
                                                                       UAnimMontage* InMontage,
                                                                       UAbilityTimelineDefinition* InDefinition)
{
    auto* Task = NewAbilityTask<UAbilityTask_RunTimeline>(OwningAbility);
    Task->Montage = InMontage;
    Task->Definition = InDefinition;
    return Task;
}

// 服务端解析值快照并在播放之前安装监听，零时刻通知也不会因监听晚建立而丢失。
void UAbilityTask_RunTimeline::Activate()
{
    auto* ASC = AbilitySystemComponent.Get();
    const auto* Info = Ability ? Ability->GetCurrentActorInfo() : nullptr;
    FString Error;
    if (!Info || !Info->IsNetAuthority() || !ASC || !Definition ||
        !Definition->ResolveMontage(Montage, Snapshot, Error))
    {
        Finish(true);
        return;
    }
    AnimInstance = Info->GetAnimInstance();
    if (!AnimInstance.IsValid())
    {
        Finish(true);
        return;
    }
    for (const auto& Event : Snapshot.Events)
    {
        EventHandles.Add(
            Event.EventTag,
            ASC->GenericGameplayEventCallbacks.FindOrAdd(Event.EventTag).AddUObject(this, &ThisClass::ReceiveEvent));
    }
    CancelHandle = Ability->OnGameplayAbilityCancelled.AddUObject(this, &ThisClass::AbilityCancelled);
    LastRate = Snapshot.RateAt(0);
    if (ASC->PlayMontage(Ability, Ability->GetCurrentActivationInfo(), Montage, LastRate) <= 0)
    {
        Finish(true);
        return;
    }
    // 播放时可能同步触发其他业务并结束GA；禁止继续绑定已结束任务。
    if (bFinishing || !ShouldBroadcastAbilityTaskDelegates())
    {
        return;
    }
    const auto* Instance = AnimInstance->GetActiveInstanceForMontage(Montage);
    if (!Instance)
    {
        Finish(true);
        return;
    }
    Snapshot.MontageInstanceId = Instance->GetInstanceID();
    FOnMontageEnded Ended;
    Ended.BindUObject(this, &ThisClass::MontageEnded);
    AnimInstance->Montage_SetEndDelegate(Ended, Montage);
    FOnMontageBlendingOutStarted BlendOut;
    BlendOut.BindUObject(this, &ThisClass::MontageBlendingOut);
    AnimInstance->Montage_SetBlendingOutDelegate(BlendOut, Montage);
    SetWaitingOnAvatar();
    OnPosition.Broadcast(0);
}

// 同一资产可能在下一次施法重播，因此实例ID与ASC拥有者必须同时匹配。
bool UAbilityTask_RunTimeline::OwnsMontage() const
{
    const auto* ASC = AbilitySystemComponent.Get();
    const auto* Instance =
        AnimInstance.IsValid() ? AnimInstance->GetMontageInstanceForID(Snapshot.MontageInstanceId) : nullptr;
    if (!ASC || !Instance || Instance->Montage != Montage || ASC->GetAnimatingAbility() != Ability)
    {
        return false;
    }
    // GetCurrentMontage在自然混出时返回空，但旧实例仍在推进，不能把它当中断。
    // 若同一GA另播了同资产，也必须比较实例，避免旧任务停止下一次播放。
    const auto* Active = AnimInstance->GetActiveInstanceForMontage(Montage);
    const auto* Current = ASC->GetCurrentMontage();
    return (!Current || Current == Montage) && (!Active || Active->GetInstanceID() == Snapshot.MontageInstanceId);
}

// 不设置播放位置。阶段切换量化到游戏帧，源动画经过Notify时仍由引擎正常派发。
void UAbilityTask_RunTimeline::TickTask(float DeltaTime)
{
    Super::TickTask(DeltaTime);
    if (bFinishing)
    {
        return;
    }
    if (!OwnsMontage())
    {
        Finish(true);
        return;
    }
    const auto* Instance = AnimInstance->GetMontageInstanceForID(Snapshot.MontageInstanceId);
    const float Position = Instance->GetPosition();
    if (!FMath::IsFinite(Position) || Position + KINDA_SMALL_NUMBER < LastPosition ||
        Snapshot.PhaseAt(Position) == INDEX_NONE)
    {
        Finish(true);
        return;
    }
    LastPosition = Position;
    const float Rate = Snapshot.RateAt(Position);
    if (!FMath::IsNearlyEqual(Rate, LastRate))
    {
        LastRate = Rate;
        // 走ASC而非直接AnimInstance调用，权威端会更新RepAnimMontageInfo中的倍率。
        AbilitySystemComponent->CurrentMontageSetPlayRate(Rate);
    }
    if (ShouldBroadcastAbilityTaskDelegates())
    {
        OnPosition.Broadcast(Position);
    }
}

// 不接管原有WaitGameplayEvent；选择迁移的GA只把业务接到OnEvent，避免两处同时伤害。
void UAbilityTask_RunTimeline::ReceiveEvent(const FGameplayEventData* Payload)
{
    const auto* Context = Payload ? Cast<UAbilityTimelineNotifyContext>(Payload->OptionalObject) : nullptr;
    const auto* ASC = AbilitySystemComponent.Get();
    const auto* Active = AnimInstance.IsValid() ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
    // 通知队列先于MontageEnded委托派发，但尾帧的实例可能已经删除。保留原队列ID验证，
    // 并排除已转交给其他能力/实例的播放，不因查询不到旧实例而丢弃合法尾帧事件。
    if (bFinishing || !Context || !ASC || ASC->GetAnimatingAbility() != Ability ||
        (ASC->GetCurrentMontage() && ASC->GetCurrentMontage() != Montage) ||
        (Active && Active->GetInstanceID() != Snapshot.MontageInstanceId) ||
        Context->AnimInstance != AnimInstance.Get() || Context->Montage != Montage ||
        !Snapshot.AcceptEvent(Payload->EventTag, Snapshot.CastId, Context->MontageInstanceId, Context->SourcePosition))
    {
        return;
    }
    if (ShouldBroadcastAbilityTaskDelegates())
    {
        OnEvent.Broadcast(Payload->EventTag, *Payload);
    }
}

// 查询当前真实位置，不依赖上一帧记录，供服务器普通打断判定使用。
bool UAbilityTask_RunTimeline::IsPhaseInterruptible() const
{
    if (bFinishing || !OwnsMontage())
    {
        return false;
    }
    const int32 Phase =
        Snapshot.PhaseAt(AnimInstance->GetMontageInstanceForID(Snapshot.MontageInstanceId)->GetPosition());
    return Phase != INDEX_NONE && Snapshot.Phases[Phase].bInterruptible;
}

// 能取消到的标签为空时不允许技能主动取消；强制死亡等仍调用GA取消。
bool UAbilityTask_RunTimeline::CanCancelInto(FGameplayTag AbilityTag) const
{
    if (bFinishing || !OwnsMontage() || !AbilityTag.IsValid())
    {
        return false;
    }
    const int32 Phase =
        Snapshot.PhaseAt(AnimInstance->GetMontageInstanceForID(Snapshot.MontageInstanceId)->GetPosition());
    return Phase != INDEX_NONE && AbilityTag.MatchesAny(Snapshot.Phases[Phase].CancelInto);
}

// 计算跨阶段动画窗口的目标时长，仅供显示；实际关闭窗口仍依赖动画位置。
float UAbilityTask_RunTimeline::DurationBetween(float SourceStart, float SourceEnd) const
{
    return FMath::Max(0.f, Snapshot.ElapsedAt(SourceEnd) - Snapshot.ElapsedAt(SourceStart));
}

// 只有此任务绑定的实例能够调用这个单播委托；自然结束与中断均通知GA。
void UAbilityTask_RunTimeline::MontageEnded(UAnimMontage* EndedMontage, bool bInterrupted)
{
    if (EndedMontage == Montage)
    {
        // 自然结束可能先于本帧Task Tick。首版只允许默认自动混出和无跳段的完整播放，
        // 因而正常结束可推进到终点；长帧跨过过多内容则失败关闭，不能集中补发旧伤害。
        if (!bInterrupted && !bFinishing)
        {
            const float End = Montage->GetPlayLength();
            if (DurationBetween(LastPosition, End) > .15f)
            {
                Finish(true);
                return;
            }
            LastPosition = End;
            if (ShouldBroadcastAbilityTaskDelegates())
            {
                OnPosition.Broadcast(End);
            }
        }
        Finish(bInterrupted);
    }
}

// 自然混出期间仍可推进后摇；中断则当场关闭入口，不能继续补发伤害。
void UAbilityTask_RunTimeline::MontageBlendingOut(UAnimMontage* EndedMontage, bool bInterrupted)
{
    if (EndedMontage == Montage && bInterrupted)
    {
        Finish(true);
    }
}

void UAbilityTask_RunTimeline::AbilityCancelled()
{
    Finish(true);
}

void UAbilityTask_RunTimeline::ExternalCancel()
{
    Finish(true);
}

// 先锁定结束状态再广播；GA回调可以同步EndAbility/EndTask，不会重复广播。
void UAbilityTask_RunTimeline::Finish(bool bInterrupted)
{
    if (bFinishing)
    {
        return;
    }
    bFinishing = true;
    Snapshot.Close();
    if (ShouldBroadcastAbilityTaskDelegates())
    {
        OnFinished.Broadcast(bInterrupted);
    }
    EndTask();
}

// 显式移除ASC监听和GA取消委托；仅解绑本实例上的自身委托，绝不触碰下一次播放。
void UAbilityTask_RunTimeline::OnDestroy(bool bAbilityEnded)
{
    bFinishing = true;
    Snapshot.Close();
    auto* ASC = AbilitySystemComponent.Get();
    if (Ability)
    {
        Ability->OnGameplayAbilityCancelled.Remove(CancelHandle);
    }
    if (ASC)
    {
        for (const auto& Entry : EventHandles)
        {
            if (auto* Delegate = ASC->GenericGameplayEventCallbacks.Find(Entry.Key))
            {
                Delegate->Remove(Entry.Value);
            }
        }
    }
    EventHandles.Reset();
    const bool bOwned = OwnsMontage();
    if (AnimInstance.IsValid())
    {
        if (auto* Instance = AnimInstance->GetMontageInstanceForID(Snapshot.MontageInstanceId))
        {
            if (Instance->OnMontageEnded.IsBoundToObject(this))
            {
                Instance->OnMontageEnded.Unbind();
            }
            if (Instance->OnMontageBlendingOutStarted.IsBoundToObject(this))
            {
                Instance->OnMontageBlendingOutStarted.Unbind();
            }
        }
    }
    if (bOwned)
    {
        ASC->CurrentMontageStop(.08f);
        ASC->ClearAnimatingAbility(Ability);
    }
    else if (ASC && ASC->GetAnimatingAbility() == Ability && !ASC->GetCurrentMontage())
    {
        // 自然结束委托派发前实例可能已被引擎移除。GA可继续等待弹体，不能依赖
        // NotifyAbilityEnded才释放动画所有权；有后续活跃蒙太奇时则保留新占用。
        ASC->ClearAnimatingAbility(Ability);
    }
    Super::OnDestroy(bAbilityEnded);
}
