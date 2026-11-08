// 主要调用链：哥布林/食尸鬼复用公共预警 GA；投石在接触窗口生成弹体，由弹体负责后续碰撞伤害。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "LegacyMonsterAbility.generated.h"
class AEnemySlingProjectile;
UCLASS()
class TEST_25D_API UGoblinSpearAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UGoblinSpearAbility();
};
UCLASS()
class TEST_25D_API UGhoulLeftAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UGhoulLeftAbility();
};
UCLASS()
class TEST_25D_API UGhoulRightAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UGhoulRightAbility();
};
UCLASS(BlueprintType)
class TEST_25D_API USlingAttackConfig : public UStoneGolemAttackConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
    TSubclassOf<AEnemySlingProjectile> ProjectileClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
    float Speed = 550;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
    float Lifetime = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
    FName MuzzleSocket = TEXT("Hand-L");
    // 检查弹体类型、速度和寿命是否处于允许范围。
    bool ValidateProjectile() const;
};
UCLASS()
class TEST_25D_API UGoblinSlingAbility : public UTelegraphedEnemyAbility
{
    GENERATED_BODY()
public:
    UGoblinSlingAbility();
    // 要求投石参数有效后复用公共激活条件。
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                    const FGameplayTagContainer* S = nullptr, const FGameplayTagContainer* T = nullptr,
                                    FGameplayTagContainer* R = nullptr) const override;
    // 取消时销毁已发射弹体，释放引用后执行公共收尾。
    virtual void EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                            FGameplayAbilityActivationInfo A, bool R, bool C) override;

protected:
    // 在公共瞄准成功后缓存目标位置，供锁定后的发射方向使用。
    virtual bool UpdateAim() override;
    // 检查发射口遮挡，延迟生成弹体并注入伤害快照、速度与寿命。
    virtual void ResolveHit() override;

private:
    FVector LockedAim = FVector::ZeroVector;
    TWeakObjectPtr<AEnemySlingProjectile> Launched;
};
