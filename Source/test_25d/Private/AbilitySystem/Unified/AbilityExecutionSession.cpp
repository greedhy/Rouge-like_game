// 调用链：GA/Timeline输入 -> ExecuteAction/TryCommit -> 原有动作实现；GA结束 -> Close -> 所属资源清理。
#include "AbilitySystem/Unified/AbilityExecutionSession.h"

FAbilityExecutionSession::FAbilityExecutionSession(TFunction<bool()> InSourceValidator)
    : SourceValidator(MoveTemp(InSourceValidator))
{
}

bool FAbilityExecutionSession::IsSourceCurrent() const
{
    return bOpen && (!SourceValidator || SourceValidator());
}

bool FAbilityExecutionSession::RegisterAction(FName Action, TFunction<void(int32)> Execute)
{
    if (!bOpen || Action.IsNone() || !Execute || Actions.Contains(Action))
    {
        return false;
    }
    Actions.Add(Action, MoveTemp(Execute));
    return true;
}

bool FAbilityExecutionSession::ExecuteAction(FName Action, int32 Sequence)
{
    if (!IsSourceCurrent())
    {
        Close(true);
        return false;
    }
    const auto* Registered = Actions.Find(Action);
    const int32* LastSequence = LastExecuted.Find(Action);
    if (!bOpen || !Registered || (LastSequence && Sequence <= *LastSequence))
    {
        return false;
    }
    // 执行之前记账，防止伤害委托重入同一个Notify。复制回调，允许回调同步关闭并清空注册表。
    const TFunction<void(int32)> Execute = *Registered;
    LastExecuted.Add(Action, Sequence);
    Execute(Sequence);
    return true;
}

void FAbilityExecutionSession::AddCleanup(TFunction<void()> Cleanup)
{
    if (!Cleanup)
    {
        return;
    }
    if (!bOpen)
    {
        Cleanup();
        return;
    }
    Cleanups.Add(MoveTemp(Cleanup));
}

bool FAbilityExecutionSession::TryCommit(TFunctionRef<bool()> Commit)
{
    if (!IsSourceCurrent())
    {
        Close(true);
        return false;
    }
    if (bCommitAttempted)
    {
        return bCommitted;
    }
    bCommitAttempted = true;
    bCommitted = Commit();
    // 费用/冷却委托可以同步结束技能或替换 Avatar，成功提交不授权后续动作跨越生命边界。
    if (!IsSourceCurrent())
    {
        Close(true);
        return false;
    }
    return bCommitted;
}

bool FAbilityExecutionSession::PublishSignal(FName Signal, FGameplayTag Tag)
{
    if (!IsSourceCurrent())
    {
        Close(true);
        return false;
    }
    if (Signal.IsNone())
    {
        return false;
    }
    OnSignal.Broadcast(Signal, Tag);
    return IsSourceCurrent();
}

void FAbilityExecutionSession::Close(bool bCancelled)
{
    if (!bOpen)
    {
        return;
    }
    bOpen = false;
    Actions.Reset();
    LastExecuted.Reset();
    auto OwnedCleanups = MoveTemp(Cleanups);
    for (auto& Cleanup : OwnedCleanups)
    {
        Cleanup();
    }
    OnSignal.Broadcast(bCancelled ? TEXT("Cancelled") : TEXT("Completed"), FGameplayTag());
    OnSignal.Clear();
}
