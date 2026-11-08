// 主要调用链：行为树攻击标签 → 选择器 TryChooseAttack → ActivateAbility 建立预警 → Advance 按蒙太奇接触时间结算 → EndAbility 清理。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "GameplayEffect.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "TelegraphedEnemyAbility.generated.h"
class ATelegraphedEnemyCharacter;

UCLASS()
class TEST_25D_API UStoneGolemCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UStoneGolemCooldownEffect();
};

UCLASS()
class TEST_25D_API UTelegraphedEnemyAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UTelegraphedEnemyAbility();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "StoneGolem")
    TObjectPtr<UStoneGolemAttackConfig> AttackConfig;
    // 检查配置、骨骼、生命状态、目标阵营、距离和视线，再交由 GAS 检查激活条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* Source = nullptr,
                                    const FGameplayTagContainer* Target = nullptr,
                                    FGameplayTagContainer* Relevant = nullptr) const override;
    // 以技能独立的冷却标签判断是否仍在冷却。
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayTagContainer* Relevant = nullptr) const override;
    // 仅在权威端把本次快照的冷却时长写入自身 GE。
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) const override;
    // 冻结攻击参数和对象池租约，建立预警并暂停移动，启动阶段定时器。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    // 停定时器和所属蒙太奇，结束预警；仅同一租约的存活角色恢复移动。
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;
    // 先粗查附近存活角色，再按阵营、锁定地面、几何形状和视线筛选实际受击者。
    void GatherTargets(TArray<AActor*>& Out) const;

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    FGameplayTag CooldownIdentity;

protected:
    // 检查攻击归属，推进锁定、动画、唯一命中窗口与后摇结束。
    virtual void Advance();
    // 将异常、中断或失效统一转入取消收尾。
    void Abort();
    // 锁定前更新朝向与地面中心；地面目标使用激活时选定的目标。
    virtual bool UpdateAim();
    // 计算命中特效落点，并将当前合法目标交给伤害结算。
    virtual void ResolveHit();
    // 逐个目标重新检查攻击归属，使用伤害快照与击退参数走统一 GAS 伤害通路。
    void ApplyHitTargets(const TArray<AActor*>& Targets);
    UPROPERTY()
    FStoneGolemAttackTuning Snapshot;
    UPROPERTY()
    TSubclassOf<UGameplayEffect> SnapshotDamageEffect;
    FGameplayTag SnapshotDamageType;
    TWeakObjectPtr<ATelegraphedEnemyCharacter> Golem;
    TWeakObjectPtr<AActor> SelectedTarget;
    FTimerHandle UpdateTimer;
    int32 Serial = 0, Lease = 0;
    double Started = 0, LastWarning = 0;
    FVector Center = FVector::ZeroVector;
    float Yaw = 0, SnapshotDamage = 0;
    TArray<FVector> LockedSurface;
    TArray<int32> LockedTriangles;
    uint8 PreviousMovementMode = 0;
    bool PreviousDesiredRotation = false;
    bool bLocked = false, bMontageStarted = false, bHit = false;
};

UCLASS()
class TEST_25D_API UDemonClawAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UDemonClawAbility();
};
UCLASS()
class TEST_25D_API UDemonTailspinAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UDemonTailspinAbility();
};

// Existing behavior-tree attack tag routes through this server-only selector.
UCLASS()
class TEST_25D_API UTelegraphedSelectAttack : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UTelegraphedSelectAttack();
    // 在服务器转交角色选招，并立即结束选择器。
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
};
