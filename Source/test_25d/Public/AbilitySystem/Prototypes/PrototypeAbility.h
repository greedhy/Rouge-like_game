/**
 * 框架：GAS 输入壳，不是组合图执行器。输入 -> ActivateAbility -> 目标任务回调 -> Execute/CommitAbility -> Runtime::Spawn。
 * 生命周期：有限 Runtime 可在 GA 结束后独立运行；持续 Runtime 随 EndAbility 取消。客户端目标数据只是请求，服务器校验并扣费。
 */
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "GameplayEffect.h"
#include "PrototypeAbility.generated.h"

class UAbilityPrototype;
class APrototypeRuntime;
class UAbilityGraphDefinition;
class UAbilityGraphInstance;
class UPrototypeCastContext;
struct FPureMassCombatTarget;
enum class EAbilityGraphEndReason : uint8;

UCLASS()
class TEST_25D_API UPrototypeAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    /** 构造阶段设定每 Actor 实例化与本地预测策略；伤害附加参数默认关闭。 */
    UPrototypeAbility();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<UAbilityPrototype> Prototype;
    /** 输入层查询开关语义；仅根 Orbit 作为再次按键关闭的技能。 */
    virtual bool IsInputToggleAbility() const override;
    /** 装备切换策略查询：此能力允许换装流程取消当前施法。 */
    virtual bool ShouldCancelOnEquipChange() const override
    {
        return true;
    }
    /** GAS 激活检查入口：校验配置、伤害 GE、Avatar 与存活状态，再执行父类限制。 */
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;
    /** GAS 激活入口：重置上次状态；Beam 注册松键任务，自身技能直接执行，其余等待鼠标目标数据和超时。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    /** GAS 结束入口：清理目标超时与销毁监听，取消仍由 GA 持有的 Runtime，再交父类释放任务。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
    /** GAS 费用检查回调：读取 ASC 当前 Mana，与根节点一次性费用比较，不扣费。 */
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayTagContainer* Relevant = nullptr) const override;
    /** CommitAbility 的费用应用回调：仅权威端经 SpendMana 扣除根节点费用。 */
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    /** GAS 冷却检查回调：依据根配置 CooldownTag 查询 ASC 当前冷却状态。 */
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayTagContainer* Relevant = nullptr) const override;
    /** CommitAbility 的冷却应用回调：服务器创建持续 GE Spec，写入持续时间与冷却 Tag。 */
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) const override;
    /** 技能 UI 查询入口：按当前节点 Kind 和等级参数生成说明；不执行技能或消耗资源。 */
    virtual FString GetDescription(int32 Level) override;
    /** 技能 UI 下一等级说明入口，复用描述生成逻辑。 */
    virtual FString GetNextLevelDescription(int32 Level) override;

protected:
    /** 目标数据返回后先核对原激活生命，再编译只读图；失败发生在 Commit 之前。 */
    UAbilityGraphDefinition* PrepareUnifiedGraph(FName Profile, FString& Error);
    /** 已提交的根入口共用图启动/世界持有；连续技能保留完成监听，有限技能在 GA 结束后继续。 */
    bool StartUnifiedGraph(UAbilityGraphDefinition* Graph, UPrototypeCastContext* Context, FVector Origin,
                           FVector Direction, const FPureMassCombatTarget& Target, bool bHoldAbility, FString& Error);

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphInstance> UnifiedExecution;
    /** 图自然结束/来源失效回调；先清引用再结束 GAS，避免 EndAbility 重入取消同一图。 */
    void OnUnifiedFinished(EAbilityGraphEndReason Reason);
    FTimerHandle TargetTimeout;
    bool bReceivedTarget = false;
    UFUNCTION()
    /** TargetDataUnderMouse 的异步委托入口：客户端停止等待；服务器验证命中、距离、遮挡和目标类型后进入 Execute。 */
    void OnTargetData(const FGameplayAbilityTargetDataHandle& Data);
    UFUNCTION()
    /** WaitInputRelease 的异步松键回调，经 Abort 取消持续技能。 */
    void OnReleased(float HeldTime);
    /** 超时、非法目标或松键共用的取消入口；仅在 GA 激活时调用 EndAbility。 */
    void Abort();
    /** OnTargetData/自身施法入口：拒绝重复目标，提交费用与冷却，建立共享上下文并生成根 Runtime。有限节点脱离 GA 存活；Beam/Orbit 保留销毁监听。 */
    void Execute(const FVector& Position, AActor* Target);
};

/** Stable native definition so duration effects replicate correctly. */
UCLASS()
class TEST_25D_API UPrototypeCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 构造稳定的原生持续 GE 类型，供 ApplyCooldown 创建可复制的冷却 Spec。 */
    UPrototypeCooldownEffect();
};
