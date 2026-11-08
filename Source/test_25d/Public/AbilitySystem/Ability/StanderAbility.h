// 主要调用链：GAS 激活检查 -> ActivateAbility -> StartStrike -> 定时 Advance -> MoveDash / Hit；GuardHit 延迟触发反击，EndAbility 统一清理。
#pragma once
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/StanderCombatConfig.h"
#include "StanderAbility.generated.h"
class AStanderPlayer;

UCLASS(Abstract)
class TEST_25D_API UStanderAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    // 设置该技能的默认执行策略、模式和激活约束。
    UStanderAbility();
    // 激活前检查角色状态、配置及伤害依赖，再交给 GAS 检查通用条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                                    const FGameplayTagContainer* = nullptr, const FGameplayTagContainer* = nullptr,
                                    FGameplayTagContainer* = nullptr) const override;
    // 根据冷却标签判断当前技能是否仍处于冷却。
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                               FGameplayTagContainer*) const override;
    // 服务器施加带冷却标签的限时 GE。
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                               FGameplayAbilityActivationInfo) const override;
    // 读取 ASC 的当前法力，判断是否足够支付本次消耗。
    virtual bool CheckCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                           FGameplayTagContainer*) const override;
    // 服务器通过 SetByCaller 法力 GE 扣除消耗。
    virtual void ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                           FGameplayAbilityActivationInfo) const override;
    // 提交技能消耗和冷却，锁定朝向与移动，播放蒙太奇并启动推进定时器。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                                 FGameplayAbilityActivationInfo, const FGameplayEventData*) override;
    // 清除定时器、技能效果和动画占用，按角色状态恢复移动并结束 GAS 激活。
    virtual void EndAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
                            FGameplayAbilityActivationInfo, bool, bool) override;
    // 按技能模式或动作事件生成界面描述。
    virtual FString GetDescription(int32 Level) override;
    // 换装时取消此技能，避免旧武器动作继续生效。
    virtual bool ShouldCancelOnEquipChange() const override
    {
        return true;
    }
    // 额外检查 Mass 伤害 GE 类型和命中上下文中的实体编号。
    static bool IsMassCounterDamage(const FGameplayEffectSpec& Spec);
    // 确认伤害上下文标记了格挡且包含正值伤害类型。
    static bool IsCounterDamage(const FGameplayEffectSpec& Spec);

protected:
    enum class EMode : uint8
    {
        Combo,
        Guard,
        Spin,
        Dash,
        Lift,
        ShieldBash
    };
    EMode Mode = EMode::Combo;
    FGameplayTag CooldownIdentity;

private:
    // 从当前 Avatar 的角色配置读取对应剑盾数据资产。
    const UStanderCombatConfig* Config(const FGameplayAbilityActorInfo*) const;
    // 根据当前攻击模式读取法力消耗。
    float Cost(const UStanderCombatConfig*) const;
    // 根据当前攻击模式读取冷却时长。
    float Cooldown(const UStanderCombatConfig*) const;
    // 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
    void Advance();
    // 筛选范围内合法且无遮挡的目标，应用伤害并播放命中表现。
    void Hit();
    // 重置本段命中状态和起始时间，通过 ASC 播放选定攻击蒙太奇。
    bool StartStrike(const FSwordStrike& NewStrike);
    // 按动画位移进度分步突进，检查房间、地面和碰撞并在受阻时停止。
    void MoveDash(float Position);
    // 监听自身受击 GE，仅对合法格挡伤害标记延迟反击。
    void GuardHit(UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle);
    TWeakObjectPtr<AStanderPlayer> Player;
    FSwordStrike Strike;
    FTimerHandle Timer;
    FDelegateHandle GuardListener;
    FActiveGameplayEffectHandle GuardHandle;
    double Started = 0, LastComboEnd = -100;
    int32 NextCombo = 0, CurrentCombo = 0, OriginRoom = INDEX_NONE;
    uint8 PreviousMovement = 0;
    bool bOwnMovement = false, bHit = false, bCounter = false, bCounterPending = false, bDashBlocked = false;
    FVector LockedForward = FVector::ForwardVector;
    float DashApplied = 0;
};
UCLASS()
class TEST_25D_API UStanderComboAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderComboAbility();
};
UCLASS()
class TEST_25D_API UStanderGuardAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderGuardAbility();
};
UCLASS()
class TEST_25D_API UStanderSpinAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderSpinAbility();
};
UCLASS()
class TEST_25D_API UStanderDashAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderDashAbility();
};

UCLASS()
class TEST_25D_API UStanderLiftAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderLiftAbility();
};
UCLASS()
class TEST_25D_API UStanderShieldBashAbility : public UStanderAbility
{
    GENERATED_BODY()
public:
    UStanderShieldBashAbility();
};
