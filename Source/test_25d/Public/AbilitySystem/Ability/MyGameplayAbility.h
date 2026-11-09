// 主要调用链：GAS PreActivate → 独立执行会话 → 输入/注册动作/一次Commit → 派生GA收尾与会话关闭；描述查询供技能UI复用。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionSession.h"
#include "MyGameplayAbility.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** GAS激活前建立独立会话；不依赖派生类调用Super::ActivateAbility。 */
    virtual void PreActivate(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                             FGameplayAbilityActivationInfo ActivationInfo,
                             FOnGameplayAbilityEnded::FDelegate* EndedDelegate,
                             const FGameplayEventData* TriggerEventData = nullptr) override;
    /** 所有原生/蓝图完整提交共用一次费用与冷却事务，禁止同步重入重复扣费。 */
    virtual bool CommitAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               FGameplayAbilityActivationInfo ActivationInfo,
                               FGameplayTagContainer* OptionalRelevantTags = nullptr) override;
    /** 结束前关闭公共事件入口和会话资源；各派生类仍释放自身专属资源。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    /** 任务保留本次会话弱引用即可拒绝上次激活的迟到输入/通知。 */
    TSharedPtr<FAbilityExecutionSession> GetExecutionSession() const
    {
        return ExecutionSession;
    }
    /** 原生动作注册后由公共入口按事件序号执行；不改变其原有目标/伤害通路。 */
    bool DispatchExecutionAction(FName Action, int32 Sequence, TFunction<void(int32)> Execute);
    /** 派生EndAbility在停止动画/撤销GE之前关闭入口，避免清理期间的委托重新命中。 */
    void CloseExecutionSession(bool bCancelled);
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    FGameplayTag StartupInputTag;
    // Toggle abilities use a single press routed to authority, never held activation.
    virtual bool IsInputToggleAbility() const
    {
        return false;
    }
    virtual bool ShouldCancelOnEquipChange() const
    {
        return IsInputToggleAbility();
    }
    virtual bool IsEquipmentGrantedOnly() const
    {
        return false;
    }

    // 调用父类并为活动技能转发带预测键的按下事件。
    virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                              const FGameplayAbilityActivationInfo ActivationInfo) override;
    // 调用父类并转发带预测键的松开事件。
    virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo) override;

    // 生成当前等级的默认富文本描述，供派生技能覆盖。
    virtual FString GetDescription(int32 Level);
    // 生成下一等级的默认描述，供派生技能覆盖。
    virtual FString GetNextLevelDescription(int32 Level);
    // 生成包含解锁等级的提示文本。
    static FString GetLockedDescription(int32 Level);

protected:
    /**
     * 默认会话属于一次 Run，延迟动作不能继承后续局身份。常驻基础监听器可显式覆盖订阅寿命，
     * 但它发起的每个实际动作仍必须独立捕获并校验当前 Run 来源；此选项不改变图/动作令牌。
     */
    virtual bool IsExecutionSessionBoundToRun() const;
    // 从费用 GE 的法力修饰器尝试求指定等级静态消耗。
    float GetManaCost(float InLevel = 1.f) const;
    // 从冷却 GE 尝试求指定等级静态持续时间。
    float GetCooldown(float InLevel = 1.f) const;

private:
    TSharedPtr<FAbilityExecutionSession> ExecutionSession;
};
