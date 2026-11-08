// 主要调用链：公共预警 GA → Grux 阶段推进 → 连击后续段、冲锋扫掠或怒吼护甲 → 公共收尾。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "GruxEnemyAbility.generated.h"

UENUM(BlueprintType)
enum class EGruxAttackKind : uint8
{
    Combo,
    Charge,
    Roar
};

UCLASS(BlueprintType)
class TEST_25D_API UGruxAttackConfig : public UStoneGolemAttackConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grux")
    EGruxAttackKind Kind = EGruxAttackKind::Combo;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grux")
    FStoneGolemAttackTuning Followup;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grux")
    float ChargeDuration = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grux")
    float RoarArmor = 25;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grux")
    float RoarDuration = 6;
    // 验证共用攻击配置，以及后续段骨骼、冲锋时间窗口或怒吼增益参数。
    bool ValidateGrux(FString& Error) const;
};

UCLASS(Abstract)
class TEST_25D_API UGruxEnemyAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    // 检查攻击种类与专属配置，并防止怒吼护甲重复叠加后再检查公共条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;
    // 重置阶段和受击集合，复制配置与基础伤害后进入公共预警流程。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    // 解除蒙太奇委托，取消时撤回本次护甲，并清空冲锋状态后走公共收尾。
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;

protected:
    EGruxAttackKind Kind = EGruxAttackKind::Combo;
    // 在公共瞄准基础上，按静态障碍、地面与房间边界裁短冲锋走廊。
    virtual bool UpdateAim() override;
    // 在首段后摇完成时切换后续段，再推进公共时序与冲锋运动。
    virtual void Advance() override;
    // 根据攻击类型启动护甲或冲锋；普通命中仍复用父类范围伤害。
    virtual void ResolveHit() override;

private:
    // 结束前段表现，换入冻结的后续段参数，并重新建立独立预警。
    bool BeginFollowup();
    // 沿预警走廊贴地扫掠移动，对实际经过线段中的目标去重结算。
    void AdvanceCharge();
    // 绑定当前阶段蒙太奇的中断回调，序号和租约防止旧回调取消新攻击。
    void ObserveMontage();
    // 解除本技能记录的蒙太奇委托并释放弱引用。
    void ClearMontageDelegates();
    TWeakObjectPtr<UAnimMontage> ObservedMontage;
    UPROPERTY()
    TObjectPtr<UGruxAttackConfig> FrozenConfig;
    int32 Stage = 0, ChargeRoom = INDEX_NONE;
    float BaseDamageSnapshot = 0;
    double ChargeStarted = 0;
    bool bCharging = false;
    FVector ChargeStart = FVector::ZeroVector, PreviousChargePosition = FVector::ZeroVector;
    TSet<TWeakObjectPtr<AActor>> ChargedTargets;
    FActiveGameplayEffectHandle AppliedArmor;
};
UCLASS()
class TEST_25D_API UGruxComboAbility : public UGruxEnemyAbility
{
    GENERATED_BODY()
public:
    UGruxComboAbility();
};
UCLASS()
class TEST_25D_API UGruxChargeAbility : public UGruxEnemyAbility
{
    GENERATED_BODY()
public:
    UGruxChargeAbility();
};
UCLASS()
class TEST_25D_API UGruxRoarAbility : public UGruxEnemyAbility
{
    GENERATED_BODY()
public:
    UGruxRoarAbility();
};
