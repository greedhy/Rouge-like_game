// 调用链：装备表引用暗金 GE → 装备事务校验/应用 → 共享投射物/接触动作读取本次规则。
#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentGameplayEffect.h"
#include "UniqueEquipmentGameplayEffect.generated.h"

class UNiagaraSystem;

/** 单次投射物动作的装备规则；数值读取装备 Spec 等级，不修改技能资产默认值。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentProjectileRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FGameplayTag AbilityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FScalableFloat ExtraProjectiles = FScalableFloat(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FScalableFloat DamageMultiplier = FScalableFloat(1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FScalableFloat MinimumSpread = FScalableFloat(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    bool bDisableHoming = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FGameplayTagContainer RequiredSetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Projectile")
    FScalableFloat SetDamageMultiplier = FScalableFloat(1.f);

    // 验证指定装备等级的规则范围；没有 AbilityTag 的默认结构代表不修改技能。
    bool IsValid(float Level) const;
};

/** 单次近战接触伤害的装备规则；不改变动作时间、碰撞范围或命中预算。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentMeleeRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FGameplayTag AbilityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FScalableFloat DamageMultiplier = FScalableFloat(1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FGameplayTag DamageTypeOverride;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    TObjectPtr<UNiagaraSystem> HitEffect;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    TObjectPtr<UNiagaraSystem> BodyEffect = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FScalableFloat BodyEffectDuration = FScalableFloat(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FGameplayTagContainer RequiredSetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Melee")
    FScalableFloat SetDamageMultiplier = FScalableFloat(1.f);

    // 校验倍率、不同套装部件与已注册的伤害类型；空默认结构表示不修改近战。
    bool IsValid(float Level) const;
};

/** 修改一次法术动作。精确技能与系列二选一，范围和雷链均由对应节点消费。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentSpellRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FGameplayTag AbilityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FGameplayTag FamilyTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FScalableFloat DamageMultiplier = FScalableFloat(1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FScalableFloat RadiusMultiplier = FScalableFloat(1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FScalableFloat ExtraChainTargets = FScalableFloat(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FGameplayTagContainer RequiredSetTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment|Spell")
    FScalableFloat SetDamageMultiplier = FScalableFloat(1.f);

    // 接受实际接入的法术/系列，雷链目标数量只用于引导连锁。
    bool IsValid(float Level) const;
};

/** 固定暗金沿用单实例一个 GE；仅此子类允许受限的 Channel2 复合乘法。 */
UCLASS(Blueprintable)
class TEST_25D_API UUniqueEquipmentGameplayEffect : public UEquipmentGameplayEffect
{
    GENERATED_BODY()

public:
    // 装备事务通过基类入口虚派发，固定暗金不能作为随机 SetByCaller 模板使用。
    virtual bool IsSafeEquipmentEffect(float Level, bool bAllowRolled = false) const override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Unique")
    FEquipmentProjectileRule ProjectileRule;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Unique")
    FEquipmentMeleeRule MeleeRule;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Unique")
    FEquipmentSpellRule SpellRule;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Unique", meta = (MultiLine = "true"))
    FText UniqueDescription;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Unique", meta = (MultiLine = "true"))
    FText SetDescription;
};
