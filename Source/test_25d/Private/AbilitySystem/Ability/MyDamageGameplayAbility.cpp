// 主要调用链：派生攻击GA → 默认伤害参数组装/ApplyDamage → MyAbilitySystemLibrary或ASC效果应用 → MyAttributeSet结算。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Roguelike/DungeonRunDirector.h"

// 缓存攻击目标，已提交时立即报告。
void UMyDamageGameplayAbility::SetOffensiveAttackTarget(AActor* TargetActor)
{
    OffensiveAttackTarget = TargetActor;
    if (bAttackCommitted)
        ReportOffensiveAttack(TargetActor);
}

// 服务器将已提交活动技能的目标通知自定义 ASC。
void UMyDamageGameplayAbility::ReportOffensiveAttack(AActor* TargetActor)
{
    OffensiveAttackTarget = TargetActor;
    if (!bAttackCommitted || !IsActive() || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
        return;
    if (auto* ASC = Cast<UMyAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
        ASC->NotifyOffensiveAttack(this, TargetActor);
}

// 执行父类提交，标记攻击已提交并报告缓存目标。
void UMyDamageGameplayAbility::CommitExecute(const FGameplayAbilitySpecHandle Handle,
                                             const FGameplayAbilityActorInfo* ActorInfo,
                                             const FGameplayAbilityActivationInfo ActivationInfo)
{
    Super::CommitExecute(Handle, ActorInfo, ActivationInfo);
    bAttackCommitted = true;
    ReportOffensiveAttack(OffensiveAttackTarget.Get());
}

// 清除本次攻击状态与弱目标引用，再执行 GAS 结束流程。
void UMyDamageGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                          const FGameplayAbilityActorInfo* ActorInfo,
                                          const FGameplayAbilityActivationInfo ActivationInfo,
                                          bool bReplicateEndAbility, bool bWasCancelled)
{
    bAttackCommitted = false;
    OffensiveAttackTarget.Reset();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 校验双方 ASC 和战斗许可，按技能等级写入伤害并施加 GE。
void UMyDamageGameplayAbility::CauseDamage(AActor* TargetActor)
{
    if (!CurrentActorInfo || !IsValid(TargetActor) || !DamageEffectClass)
        return;
    auto* SourceASC = GetAbilitySystemComponentFromActorInfo();
    auto* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
    if (!IsValid(SourceASC) || !IsValid(TargetASC))
        return;
    const AActor* SourceAvatar = SourceASC->GetAvatarActor();
    const AActor* TargetAvatar = TargetASC->GetAvatarActor();
    if (!IsValid(SourceAvatar) || !IsValid(TargetAvatar) ||
        !ADungeonRunDirector::AllowsCombat(SourceAvatar, TargetAvatar))
        return;
    FGameplayEffectSpecHandle DamageSpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, 1.f);
    if (!DamageSpecHandle.IsValid())
        return;
    ReportOffensiveAttack(TargetActor);
    const float ScaleDamage = Damage.GetValueAtLevel(GetAbilityLevel());
    UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, DamageType, ScaleDamage);
    SourceASC->ApplyGameplayEffectSpecToTarget(*DamageSpecHandle.Data.Get(), TargetASC);
}
FTaggedMontage UMyDamageGameplayAbility::GetRandomTaggedMontageFromArray(
    const TArray<FTaggedMontage>& TaggedMontages) const
{
    if (TaggedMontages.Num() > 0)
    {
        const int32 RandomNum = FMath::RandRange(0, TaggedMontages.Num() - 1);
        return TaggedMontages[RandomNum];
    }
    return FTaggedMontage();
}
float UMyDamageGameplayAbility::GetDamageAtLevel() const
{
    return Damage.GetValueAtLevel(GetAbilityLevel());
}
// 汇总伤害与减益配置，按目标及覆盖参数计算冲量和范围数据。
FDamageEffectParams UMyDamageGameplayAbility::MakeDamageEffectParamsFromClassDefaults(
    AActor* TargetActor, FVector InRadialDamageOrigin, bool bOverrideKnockbackDirection,
    FVector KnockbackDirectionOverride, bool bOverrideDeathImpulse, FVector DeathImpulseDirectionOverride,
    bool bOverridePitch, float PitchOverride) const
{
    FDamageEffectParams Params;
    Params.WorldContextObject = GetAvatarActorFromActorInfo();
    Params.DamageGameplayEffectClass = DamageEffectClass;
    Params.SourceAbilitySystemComponent = GetAbilitySystemComponentFromActorInfo();
    Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
    Params.BaseDamage = Damage.GetValueAtLevel(GetAbilityLevel());
    Params.AbilityLevel = GetAbilityLevel();
    Params.DamageType = DamageType;
    Params.DebuffChance = DebuffChance;
    Params.DebuffDamage = DebuffDamage;
    Params.DebuffDuration = DebuffDuration;
    Params.DebuffFrequency = DebuffFrequency;
    Params.DeathImpulseMagnitude = DeathImpulseMagnitude;
    Params.KnockbackForceMagnitude = KnockbackForceMagnitude;
    Params.KnockbackChance = KnockbackChance;

    if (IsValid(TargetActor))
    {
        FRotator Rotation =
            (TargetActor->GetActorLocation() - GetAvatarActorFromActorInfo()->GetActorLocation()).Rotation();
        if (bOverridePitch)
        {
            Rotation.Pitch = PitchOverride;
        }
        const FVector ToTarget = Rotation.Vector();
        if (!bOverrideKnockbackDirection)
        {
            Params.KnockbackForce = ToTarget * KnockbackForceMagnitude;
        }
        if (!bOverrideDeathImpulse)
        {
            Params.DeathImpulse = ToTarget * DeathImpulseMagnitude;
        }
    }

    if (bOverrideKnockbackDirection)
    {
        KnockbackDirectionOverride.Normalize();
        Params.KnockbackForce = KnockbackDirectionOverride * KnockbackForceMagnitude;
        if (bOverridePitch)
        {
            FRotator KnockbackRotation = KnockbackDirectionOverride.Rotation();
            KnockbackRotation.Pitch = PitchOverride;
            Params.KnockbackForce = KnockbackRotation.Vector() * KnockbackForceMagnitude;
        }
    }

    if (bOverrideDeathImpulse)
    {
        DeathImpulseDirectionOverride.Normalize();
        Params.DeathImpulse = DeathImpulseDirectionOverride * DeathImpulseMagnitude;
        if (bOverridePitch)
        {
            FRotator DeathImpulseRotation = DeathImpulseDirectionOverride.Rotation();
            DeathImpulseRotation.Pitch = PitchOverride;
            Params.DeathImpulse = DeathImpulseRotation.Vector() * DeathImpulseMagnitude;
        }
    }

    if (bIsRadialDamage)
    {
        Params.bIsRadialDamage = bIsRadialDamage;
        Params.RadialDamageOrigin = InRadialDamageOrigin;
        Params.RadialDamageInnerRadius = RadialDamageInnerRadius;
        Params.RadialDamageOuterRadius = RadialDamageOuterRadius;
    }
    return Params;
}
