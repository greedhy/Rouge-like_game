/**
 * 混合原型输入层：ASC -> ActivateAbility -> UPureMassTargetData.Ready -> TargetReady -> ValidateCombatTarget -> ExecuteMass/Commit -> APureMassRuntime::Spawn。客户端只提交目标；服务器重解 Actor/实体身份和可见性。有限 Runtime 脱离 GA 存活，Beam/开关技能监听 Runtime 销毁并随 EndAbility 清理。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMassAbility.generated.h"
class APureMassRuntime;

/** Player GAS ability with a common target path for Mass entities and Actor/ASC combatants. */
UCLASS()
class TEST_25D_API UPureMassAbility : public UPrototypeAbility
{
    GENERATED_BODY()
public:
    /** 清空本次目标状态，检查 Mass 子系统；Beam 注册松键回调，自身/开关技能直接执行，其余创建目标任务与超时。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    /** 清除目标超时，解绑 Runtime 销毁回调后取消仍持有节点，清引用并交父类清理。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
    /** CommitAbility 费用回调，只由服务器调用 SpendMana 扣根节点费用。 */
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    /** 服务器检查施法者/ASC/余额及有限非负费用，使用原生 GE Spec 扣蓝；零费用直接成功。 */
    static bool SpendMana(AActor* Caster, float Amount);
    /** Validates a client request against authoritative entity state and world geometry. */
    /** 兼容实体 ID 的入口，将 ID 包装为混合目标后委托 ValidateCombatTarget。 */
    static bool ValidateTarget(AActor* Caster, const UAbilityPrototype* Config, int32 Id, const FVector& Requested,
                               FVector& Resolved);
    /** 服务器拒绝非法/冲突身份，重解真实目标位置并限制请求误差；检查射程、必需目标与地面或目标遮挡。 */
    static bool ValidateCombatTarget(AActor* Caster, const UAbilityPrototype* Config,
                                     const FPureMassCombatTarget& Target, const FVector& Requested, FVector& Resolved);

private:
    FTimerHandle MassTargetTimeout;
    bool bTargetConsumed = false;
    UFUNCTION()
    /** 目标任务异步回调：客户端停止等待；服务器解析 Actor/Hit.Item 实体 ID，验证后调用 ExecuteMass。 */
    void TargetReady(const FGameplayAbilityTargetDataHandle& Data);
    UFUNCTION()
    /** Beam 松键任务回调，进入 AbortMass 取消持续技能。 */
    void Released(float HeldTime);
    /** 超时或失败统一入口，对仍激活技能调用取消式 EndAbility。 */
    void AbortMass();
    /** 防重复提交，必要时先投影地表；Commit 后建立 Context、计算各行为起点并生成 Runtime，成功后转向并通知环绕攻击。 */
    void ExecuteMass(const FVector& Point, FPureMassCombatTarget Target);
};

/** Native definition remains network-addressable; each application carries its own cost magnitude. */
UCLASS()
class TEST_25D_API UPureMassManaEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 构造瞬时 Mana 加法 GE，其数值来自 SetByCaller；每次施法创建独立 Spec。 */
    UPureMassManaEffect();
    /** 返回稳定原生 SetByCaller 键；此 GE 内复用 Damage.Physics 只作扣蓝数值键，不表示物理伤害。 */
    static FGameplayTag GetManaTag();
};
