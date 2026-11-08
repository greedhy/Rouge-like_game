// 主要调用链：派生攻击GA → 默认伤害参数组装/ApplyDamage → MyAbilitySystemLibrary或ASC效果应用 → MyAttributeSet结算。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MyAbilityTypes.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Interface/CombatInterface.h"
#include "MyDamageGameplayAbility.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyDamageGameplayAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    // Target-data tasks can provide the target before or after CommitAbility.
    UFUNCTION(BlueprintCallable, Category = "Combat")
    // 缓存攻击目标，已提交时立即报告。
    void SetOffensiveAttackTarget(AActor* TargetActor);
    // For native attacks that already committed and selected a target.
    UFUNCTION(BlueprintCallable, Category = "Combat")
    // 服务器将已提交活动技能的目标通知自定义 ASC。
    void ReportOffensiveAttack(AActor* TargetActor);
    bool HasCommittedOffensiveAttack() const
    {
        return bAttackCommitted;
    }
    // 执行父类提交，标记攻击已提交并报告缓存目标。
    virtual void CommitExecute(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo) override;
    // 清除本次攻击状态与弱目标引用，再执行 GAS 结束流程。
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;

    UFUNCTION(BlueprintCallable)
    // 校验双方 ASC 和战斗许可，按技能等级写入伤害并施加 GE。
    void CauseDamage(AActor* TargetActor);
    UFUNCTION(BlueprintPure)
    // 汇总伤害与减益配置，按目标及覆盖参数计算冲量和范围数据。
    FDamageEffectParams MakeDamageEffectParamsFromClassDefaults(
        AActor* TargetActor = nullptr, FVector InRadialDamageOrigin = FVector::ZeroVector,
        bool bOverrideKnockbackDirection = false, FVector KnockbackDirectionOverride = FVector::ZeroVector,
        bool bOverrideDeathImpulse = false, FVector DeathImpulseDirectionOverride = FVector::ZeroVector,
        bool bOverridePitch = false, float PitchOverride = 0.f) const;

    UFUNCTION(BlueprintPure)
    float GetDamageAtLevel() const;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    FGameplayTag DamageType;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    FScalableFloat Damage;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float DebuffChance = 20.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float DebuffDamage = 5.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float DebuffFrequency = 1.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float DebuffDuration = 5.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float DeathImpulseMagnitude = 1000.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float KnockbackForceMagnitude = 1000.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    float KnockbackChance = 0.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    bool bIsRadialDamage = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
    float RadialDamageInnerRadius = 0.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
    float RadialDamageOuterRadius = 0.f;

    UPROPERTY(EditDefaultsOnly, Category = "Damage")
    FVector RadialDamageOrigin = FVector::ZeroVector;

    //从角色设置的蒙太奇数组总，随机一个蒙太奇使用
    UFUNCTION(BlueprintPure)
    FTaggedMontage GetRandomTaggedMontageFromArray(const TArray<FTaggedMontage>& TaggedMontages) const;

private:
    TWeakObjectPtr<AActor> OffensiveAttackTarget;
    bool bAttackCommitted = false;
};
