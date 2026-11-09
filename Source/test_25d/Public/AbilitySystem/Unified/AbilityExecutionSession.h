// 调用链：MyGameplayAbility::PreActivate -> 本次会话 -> 输入/动画/注册动作 -> Close清理 -> 终态通知。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FAbilityExecutionSignal, FName, FGameplayTag);

/** 每次GAS激活的共同执行合同；只管理调用身份、事件和资源，不拥有ASC、目标选择或伤害实现。 */
class TEST_25D_API FAbilityExecutionSession
{
public:
    /** 可选生命验证器由真实GA入口捕获，不持有角色；纯编排测试可省略。 */
    explicit FAbilityExecutionSession(TFunction<bool()> InSourceValidator = nullptr);
    /** 动作或异步任务每次恢复前验证原ASC/Avatar/生命，禁止跟随新角色重绑定。 */
    bool IsSourceCurrent() const;
    /** 配置动作实现；同名动作只允许注册一次，序号由动作的实际阶段/事件索引提供。 */
    bool RegisterAction(FName Action, TFunction<void(int32)> Execute);
    /** 执行已注册动作；每种动作的序号须严格递增，重复/迟到事件失败关闭而不保留无界历史。 */
    bool ExecuteAction(FName Action, int32 Sequence);
    /** 将本次持有的委托/任务资源交给会话；关闭后的迟到注册立即清理。 */
    void AddCleanup(TFunction<void()> Cleanup);
    /** 唯一GAS提交门；失败、成功和同步重入均不能第二次执行消耗与冷却。 */
    bool TryCommit(TFunctionRef<bool()> Commit);
    /** 输入及已校验Notify通知；通知本身不施加伤害。关闭后拒绝业务事件。 */
    bool PublishSignal(FName Signal, FGameplayTag Tag = FGameplayTag());
    /** 先关闭动作/输入，再清理资源，最后发布一次终态。 */
    void Close(bool bCancelled);
    bool IsOpen() const
    {
        return bOpen;
    }
    const FGuid& GetId() const
    {
        return Id;
    }

    FAbilityExecutionSignal OnSignal;

private:
    TFunction<bool()> SourceValidator;
    FGuid Id = FGuid::NewGuid();
    TMap<FName, TFunction<void(int32)>> Actions;
    TMap<FName, int32> LastExecuted;
    TArray<TFunction<void()>> Cleanups;
    bool bOpen = true;
    bool bCommitAttempted = false;
    bool bCommitted = false;
};
