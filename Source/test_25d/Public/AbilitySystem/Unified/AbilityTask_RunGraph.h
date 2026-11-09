// 调用链：UnifiedGraphAbility -> 工厂/ReadyForActivation -> GraphInstance -> OnFinished -> GA EndAbility。
// 任务由 GAS 管理，以 UPROPERTY 持有图实例；任务销毁先关闭图再释放引用，不复制图对象。
#pragma once

#include "Abilities/Tasks/AbilityTask.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilityTask_RunGraph.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityGraphTaskFinished, bool, bCancelled);

UCLASS()
class TEST_25D_API UAbilityTask_RunGraph : public UAbilityTask
{
    GENERATED_BODY()
public:
    /** 开启任务 Tick，实际图启动延迟到委托绑定完成后的 Activate。 */
    UAbilityTask_RunGraph();
    /** 原生工厂：令牌由服务器捕获，不作为蓝图或客户端可伪造的网络参数暴露。 */
    static UAbilityTask_RunGraph* RunAbilityGraph(UGameplayAbility* Owner, UAbilityGraphDefinition* Graph,
                                                  FAbilitySourceToken Source, FAbilityTargetToken Target);
    /** 新制作入口传完整类型化输入，复用同一执行器；本任务只接受绑定GA的寿命。 */
    static UAbilityTask_RunGraph* RunAbilityGraphWithInput(UGameplayAbility* Owner, UAbilityGraphDefinition* Graph,
                                                           const FAbilityNodeInput& Input,
                                                           const FAbilityExecutionOptions& Options);
    UPROPERTY(BlueprintAssignable)
    FAbilityGraphTaskFinished OnFinished;
    /** 权威端启动独立实例；验证失败也广播取消，让 GA 释放占用。 */
    virtual void Activate() override;
    /** 使用服务器游戏 Delta 驱动，无额外世界定时器。 */
    virtual void TickTask(float DeltaTime) override;
    /** 外部取消统一经过图终止路径。 */
    virtual void ExternalCancel() override;
    /** 只读诊断，不向调用者转移图所有权。 */
    UAbilityGraphInstance* GetInstance() const
    {
        return Instance;
    }

protected:
    /** 先禁用回调、解绑，再取消图，防止 EndAbility 重入广播。 */
    virtual void OnDestroy(bool bAbilityEnded) override;

private:
    /** 收敛到一次任务广播；委托可以同步结束 GA。 */
    void GraphFinished(EAbilityGraphEndReason Reason);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphDefinition> Definition;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphInstance> Instance;
    UPROPERTY(Transient)
    TObjectPtr<UObject> RetainedContext;
    UPROPERTY(Transient)
    FDamageEffectParams RetainedDamageParams;
    FAbilityNodeInput NodeInput;
    FAbilityExecutionOptions ExecutionOptions;
    bool bClosing = false;
};
