/**
 * 扩展 GA 输入层：ASC 激活 -> ActivateAbility -> UPureMassTargetData.Ready 异步回调 -> TargetReady -> Execute/Commit -> AExtensionRuntime::Spawn。正常结束释放 GA 引用，但子图和子系统记录继续持有 Context；取消标记 Context。费用由服务器结算，客户端只提交目标请求。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "PureMass/PureMassCombatTarget.h"
#include "ExtensionAbility.generated.h"
UCLASS()
class TEST_25D_API UExtensionAbility : public UPrototypeAbility
{
    GENERATED_BODY()
public:
    /** GAS 激活检查：要求扩展节点 ValidateGraph 成功，再执行父类费用/冷却等检查。 */
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;
    /** 重置每次施法状态；自身目标直接执行，否则创建混合目标任务并设置三秒超时；不直接结算伤害。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    /** 清除目标超时；取消才标记 Context，正常结束不撤销已生成的持久效果；清空本地引用并调用父类。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
    /** CommitAbility 回调：服务器通过 UPureMassAbility::SpendMana 应用稳定原生扣蓝 GE。 */
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    /** UI 查询当前扩展配置名称、等级、费用、冷却和 Summary；不运行子图。 */
    virtual FString GetDescription(int32 Level) override;
    /** UI 下一等级查询直接复用 GetDescription，传入的等级由调用者决定。 */
    virtual FString GetNextLevelDescription(int32 Level) override
    {
        /** UI 查询当前扩展配置名称、等级、费用、冷却和 Summary；不运行子图。 */
        return GetDescription(Level);
    }

private:
    bool bConsumed = false;
    FTimerHandle Timeout;
    UPROPERTY()
    TObjectPtr<class UExtensionCastContext> CastContext;
    UFUNCTION()
    /** 目标任务异步回调：拒绝 Actor/实体 ID 混装数据；服务器校验射程、遮挡与 Attach/Chain 必需目标后调用 Execute。 */
    void TargetReady(const FGameplayAbilityTargetDataHandle& Data);
    /** 去重并提交施法，创建扩展 Context，按行为计算起点，生成根节点并通知攻击目标；成功后正常结束 GA。 */
    void Execute(FVector Position, FPureMassCombatTarget Target);
    /** 超时或校验失败入口：仅对仍激活的 GA 调用取消式 EndAbility。 */
    void Abort();
};
