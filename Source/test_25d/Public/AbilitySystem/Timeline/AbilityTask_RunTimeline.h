// 调用链：GA -> RunAbilityTimeline/ReadyForActivation -> ASC播放 -> Tick/GameplayEvent -> GA回调。
// 生命周期由所属GA管理；任务不复制。服务器通过ASC复制蒙太奇及倍率，结束先关入口再解绑。
#pragma once

#include "Abilities/Tasks/AbilityTask.h"
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
#include "AbilityTask_RunTimeline.generated.h"

class UAnimMontage;
class UAnimInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityTimelinePosition, float, SourcePosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityTimelineFinished, bool, bInterrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAbilityTimelineEvent, FGameplayTag, EventTag, FGameplayEventData,
                                             Payload);

/** 服务器动画编排任务：不执行伤害，不跳转源位置，不修改引擎内置AbilityTask。 */
UCLASS()
class TEST_25D_API UAbilityTask_RunTimeline : public UAbilityTask
{
    GENERATED_BODY()
public:
    /** 开启每帧更新，实际播放要等委托绑定之后的ReadyForActivation。 */
    UAbilityTask_RunTimeline();
    /** 替代本次蒙太奇的播放任务；不能和PlayMontageAndWait同时播放同一蒙太奇。 */
    UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
              meta = (DisplayName = "Run Ability Timeline", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility",
                      BlueprintInternalUseOnly = "true"))
    static UAbilityTask_RunTimeline* RunAbilityTimeline(UGameplayAbility* OwningAbility, UAnimMontage* Montage,
                                                        UAbilityTimelineDefinition* Definition);

    UPROPERTY(BlueprintAssignable)
    FAbilityTimelinePosition OnPosition;
    UPROPERTY(BlueprintAssignable)
    FAbilityTimelineEvent OnEvent;
    UPROPERTY(BlueprintAssignable)
    FAbilityTimelineFinished OnFinished;

    /** 注册事件监听后只启动一次蒙太奇；失败也通知GA进行资源清理。 */
    virtual void Activate() override;
    /** 根据实际源动画位置调整下一帧倍率，动画位置为唯一命中时钟。 */
    virtual void TickTask(float DeltaTime) override;
    /** 外部取消与GA结束共用幂等清理。 */
    virtual void ExternalCancel() override;
    /** 查询当前阶段的普通打断许可；死亡/销毁等强制取消无需查询此函数。 */
    UFUNCTION(BlueprintPure, Category = "Ability|Timeline")
    bool IsPhaseInterruptible() const;
    /** 只查询阶段策略，不替代新技能资源检查或取消/激活事务。 */
    UFUNCTION(BlueprintPure, Category = "Ability|Timeline")
    bool CanCancelInto(FGameplayTag AbilityTag) const;
    /** 获取本次源区间对应的实际秒数，例如格挡展示时长。 */
    float DurationBetween(float SourceStart, float SourceEnd) const;
    /** 只读快照用于诊断和测试，不能用于客户端权威判定。 */
    const FAbilityTimelineSnapshot& GetSnapshot() const
    {
        return Snapshot;
    }

protected:
    /** 关闭入口、解绑所有委托，只停止仍由本任务占用的播放实例。 */
    virtual void OnDestroy(bool bAbilityEnded) override;

private:
    /** 接收带来源实例的通知；普通无来源GameplayEvent不会通过此入口。 */
    void ReceiveEvent(const FGameplayEventData* Payload);
    /** 动画自然完成与外部打断均收敛到Finish。 */
    void MontageEnded(UAnimMontage* EndedMontage, bool bInterrupted);
    /** 打断开始立即关闭命中入口；自然混出继续观察源时间。 */
    void MontageBlendingOut(UAnimMontage* EndedMontage, bool bInterrupted);
    /** GA主动取消时停止本次任务，不等待下一帧。 */
    void AbilityCancelled();
    /** 广播一次终止原因，允许回调同步EndAbility而不重入。 */
    void Finish(bool bInterrupted);
    /** 确认当前动画实例仍属于本次施法，不能只比较资产指针。 */
    bool OwnsMontage() const;
    UPROPERTY()
    TObjectPtr<UAnimMontage> Montage;
    UPROPERTY()
    TObjectPtr<UAbilityTimelineDefinition> Definition;
    TWeakObjectPtr<UAnimInstance> AnimInstance;
    FAbilityTimelineSnapshot Snapshot;
    TMap<FGameplayTag, FDelegateHandle> EventHandles;
    FDelegateHandle CancelHandle;
    float LastPosition = 0;
    float LastRate = 0;
    bool bFinishing = false;
};
