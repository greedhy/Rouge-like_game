// 主要调用链：GAS PreActivate → 独立执行会话 → 输入/注册动作/一次Commit → 派生GA收尾与会话关闭；描述查询供技能UI复用。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionSession.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "MyGameplayAbility.generated.h"

class UAbilityGraphInstance;
struct FAbilityNodeInput;
struct FAbilitySourceToken;

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 权威端拒绝已经关闭的开发临时Spec；正式授予继续走原GAS门禁，客户端预测仍由原协议处理。 */
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayTagContainer* SourceTags = nullptr,
                                    const FGameplayTagContainer* TargetTags = nullptr,
                                    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    /** GAS激活前建立独立会话；不依赖派生类调用Super::ActivateAbility。 */
    virtual void PreActivate(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                             FGameplayAbilityActivationInfo ActivationInfo,
                             FOnGameplayAbilityEnded::FDelegate* EndedDelegate,
                             const FGameplayEventData* TriggerEventData = nullptr) override;
    /** 所有原生/蓝图完整提交共用一次费用与冷却事务，禁止同步重入重复扣费。 */
    virtual bool CommitAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               FGameplayAbilityActivationInfo ActivationInfo,
                               FGameplayTagContainer* OptionalRelevantTags = nullptr) override;
    /** 原分阶段费用/冷却保留原时点；开发观察只记录实际提交结果。 */
    virtual bool CommitAbilityCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   FGameplayAbilityActivationInfo ActivationInfo,
                                   FGameplayTagContainer* OptionalRelevantTags = nullptr) override;
    virtual bool CommitAbilityCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                       FGameplayAbilityActivationInfo ActivationInfo, bool ForceCooldown,
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
    /** 本次激活冻结的动态配方；空图表示沿用原兼容入口，不从共享 CDO 读取可变状态。 */
    const FAbilityRuntimeRecipeSnapshot& GetRuntimeRecipeSnapshot() const
    {
        return RuntimeRecipeSnapshot;
    }
    /** 配方冻结失败诊断；有动态来源却没有有效快照时，前端应在费用与实际动作前失败关闭。 */
    const FString& GetRuntimeRecipeError() const
    {
        return RuntimeRecipeError;
    }
    /** 激活开始冻结的运行归属；正常结束后独立图仍持有该值，不在迟到通知中回读当前预览。 */
    FGuid GetRuntimeExecutionOwnerLease() const
    {
        return RuntimeExecutionOwnerLease;
    }
    /** 专用前端导出默认行为定义；由派生类读取自己的配置，不通过反射改写共享 CDO。 */
    virtual UAbilityGraphDefinition* BuildRuntimeBaseGraph(UObject* Outer, FString& Error) const;
    /** 一次激活只接收一个运行时根；重复通知不能重置根预算。 */
    bool HasStartedRuntimeRecipe() const
    {
        return bRuntimeRecipeStarted;
    }
    /** 在原实际动作时点启动已冻结图；复用现有世界持有者，独立效果只在显式取消时撤销。 */
    UAbilityGraphInstance* StartFrozenRuntimeRecipe(const FAbilityNodeInput& Input, EAbilityExecutionLifetime Lifetime,
                                                    FString& Error);
    /** Start 之前绑定纯观察，避免同步根事件丢失；记录失败不改变技能行为。 */
    void ObserveFrozenRuntimeGraph(UAbilityGraphInstance* Run, const FAbilitySourceToken& Source);
    /** Start 失败时撤销尚未开始的弱订阅，不取消其他执行或回滚费用。 */
    void ForgetFrozenRuntimeGraph(UAbilityGraphInstance* Run, const FAbilitySourceToken& Source);
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    FGameplayTag StartupInputTag;
    /** 固定基础能力由角色配置授予，不参与学习、技能槽替换或普通技能存档。 */
    virtual bool IsIntrinsicAbility() const
    {
        return false;
    }
    /** 按下仅发起一次；Held不能在冷却结束后自动重启。旧技能默认保留Held行为。 */
    virtual bool IsPressOnlyAbility() const
    {
        return false;
    }
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
    UPROPERTY(Transient)
    FAbilityRuntimeRecipeSnapshot RuntimeRecipeSnapshot;
    FString RuntimeRecipeError;
    bool bRuntimeRecipeStarted = false;
    FGuid RuntimeExecutionOwnerLease;
    TSharedPtr<FAbilityExecutionSession> ExecutionSession;
};
