// 调用链：ASC激活 -> Enemy/Ground目标任务或Self -> 服务器校验/Commit -> RunGraph/世界持有 -> EndAbility。
// LocalPredicted 仅让本地目标任务发请求；图、费用和控制只在服务器执行，沿用 GAS 的结束复制。
#pragma once

#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "UnifiedGraphAbility.generated.h"

class UAbilityGraphDefinition;
class UAbilityTask_RunGraph;
class UPureMassTargetData;
class UPrototypeCastContext;
class UAbilityGraphInstance;
class UAbilityTask_WaitInputRelease;

/** Enemy保持旧样例目标协议；Ground仅输出位置，Self不创建鼠标目标任务。 */
UENUM(BlueprintType)
enum class EUnifiedAbilityTargetMode : uint8
{
    Enemy,
    Ground,
    Self
};

UCLASS()
class TEST_25D_API UUnifiedGraphAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    /** 每 Avatar 实例化，本地预测仅用于 TargetData 采集。 */
    UUnifiedGraphAbility();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph")
    TObjectPtr<UAbilityGraphDefinition> Graph;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph", meta = (ClampMin = "1"))
    float CastRange = 1200.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph")
    EUnifiedAbilityTargetMode TargetMode = EUnifiedAbilityTargetMode::Enemy;
    /** Bound随GA取消；Independent移交世界，正常GA结束后继续，来源生命失效仍会关闭。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph",
              meta = (ValidEnumValues = "BoundToAbility,IndependentCastEffect"))
    EAbilityExecutionLifetime ExecutionLifetime = EAbilityExecutionLifetime::BoundToAbility;
    /** 通道技能松键关闭；使用GAS输入任务接收远端请求，只允许绑定GA的图。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph|Input")
    bool bStopOnInputRelease = false;
    /** 复用ASC现有按键开关RPC；与松键通道互斥，只允许绑定GA的图。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Unified Graph|Input")
    bool bToggleInput = false;
    virtual bool IsInputToggleAbility() const override
    {
        return bToggleInput;
    }
    /** 保留父类输入事件；权威端松键后关闭明确配置的通道图。 */
    virtual void InputReleased(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) override;

    /** 建立超时和 TargetData 监听；这里不执行 Blueprint 激活图以避免第二个玩法入口。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation,
                                 const FGameplayEventData* TriggerEventData) override;
    /** 正常、失败、取消均清除计时器与任务；先解绑以防清理回调重入。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
    /** 验证资源所有权是否已清空，供诊断/自动化使用。 */
    bool HasPendingGraphWork() const;

protected:
    /** 目标任务回调：客户端只停止等待；服务器只接受一次载荷并重新验证位置和生命身份。 */
    UFUNCTION()
    void TargetReady(const FGameplayAbilityTargetDataHandle& Data);
    /** 运行任务终止后结束 GA，已施加的控制按组件自己的到期时间结束。 */
    UFUNCTION()
    void GraphFinished(bool bCancelled);
    /** 超时/验证失败取消当前施法，不扣费用。Commit 后运行失败不自动退款。 */
    void AbortCast();
    /** 验证本次只读图与输入端口后唯一Commit，再按显式寿命域启动。 */
    void ExecutePreparedInput(FAbilityNodeInput Input);
    UFUNCTION()
    void ChannelReleased(float HeldSeconds);

    UPROPERTY(Transient)
    TObjectPtr<UPureMassTargetData> TargetTask;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_RunGraph> GraphTask;
    UPROPERTY(Transient)
    TObjectPtr<UPrototypeCastContext> CastContext;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphInstance> IndependentExecution;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_WaitInputRelease> ReleaseTask;
    FTimerHandle TargetTimeout;
    FAbilitySourceToken ActivationSource;
    bool bTargetConsumed = false;
    bool bEnding = false;
    bool bIndependentTransferred = false;
};
