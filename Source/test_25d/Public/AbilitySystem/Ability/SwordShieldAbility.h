// 主要调用链：GAS 激活检查 -> ActivateAbility -> 定时 Advance -> Hit -> EndAbility；消耗与冷却复用本文件定义的 GE。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/SwordShieldConfig.h"
#include "SwordShieldAbility.generated.h"
class ASwordShieldPlayer;
UCLASS()
class TEST_25D_API USwordGuardEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    // 创建无限持续的格挡标签 GE。
    USwordGuardEffect();
    // 返回格挡状态标签，供伤害逻辑识别。
    static FGameplayTag StateTag();
};
UCLASS()
class TEST_25D_API USwordTimedEffect : public UGameplayEffect
{
    GENERATED_BODY()
    // 设置限时 GE 的默认持续策略。
public:
    USwordTimedEffect();
};
UCLASS()
class TEST_25D_API USwordManaEffect : public UGameplayEffect
{
    GENERATED_BODY()
    // 配置瞬时法力加法修正，数值由调用方传入。
public:
    USwordManaEffect();
};
UCLASS(Abstract)
class TEST_25D_API USwordShieldAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    // 设置该技能的默认执行策略、模式和激活约束。
    USwordShieldAbility();
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

protected:
    enum class EMode : uint8
    {
        Combo,
        Guard,
        Spin
    };
    EMode Mode = EMode::Combo;
    FGameplayTag CooldownIdentity;

private:
    // 从当前 Avatar 的角色配置读取对应剑盾数据资产。
    const USwordShieldConfig* Config(const FGameplayAbilityActorInfo*) const;
    // 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
    void Advance();
    // 筛选范围内合法且无遮挡的目标，应用伤害并播放命中表现。
    void Hit();
    TWeakObjectPtr<ASwordShieldPlayer> Player;
    FSwordStrike Strike;
    FTimerHandle Timer;
    FActiveGameplayEffectHandle GuardHandle;
    double Started = 0, LastComboEnd = -100;
    int32 NextCombo = 0, CurrentCombo = 0;
    uint8 PreviousMovement = 0;
    bool bOwnMovement = false, bHit = false;
    FVector LockedForward = FVector::ForwardVector;
    float GuardTime = 1;
};
UCLASS()
class TEST_25D_API USwordComboAbility : public USwordShieldAbility
{
    GENERATED_BODY()
public:
    USwordComboAbility();
};
UCLASS()
class TEST_25D_API USwordGuardAbility : public USwordShieldAbility
{
    GENERATED_BODY()
public:
    USwordGuardAbility();
};
UCLASS()
class TEST_25D_API USwordSpinAbility : public USwordShieldAbility
{
    GENERATED_BODY()
public:
    USwordSpinAbility();
};
