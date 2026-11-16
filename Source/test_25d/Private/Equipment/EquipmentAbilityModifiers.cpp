// 调用链：共享投射物动作 -> 原来源生命/活跃GE校验 -> 组合本次局部参数 -> 原Actor发射通路。
#include "Equipment/EquipmentAbilityModifiers.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentArcaneArea, "Equipment.AbilityFamily.ArcaneArea");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentLightningChain, "Equipment.AbilityFamily.LightningChain");

FGameplayTag EquipmentAbilityModifiers::ArcaneAreaFamily()
{
    return TAG_EquipmentArcaneArea;
}

FGameplayTag EquipmentAbilityModifiers::LightningChainFamily()
{
    return TAG_EquipmentLightningChain;
}

// 分类只包含前端已传真实身份、共享节点确实消费这些字段的技能。
FGameplayTag EquipmentAbilityModifiers::GetSpellFamily(FGameplayTag AbilityTag)
{
    if (!AbilityTag.IsValid())
    {
        return {};
    }
    const auto& Tags = FMyGameplayTags::Get();
    if (AbilityTag == Tags.Abilities_Arcane_ArcaneNova || AbilityTag == Tags.Abilities_Arcane_ArcaneShards)
    {
        return ArcaneAreaFamily();
    }
    if (AbilityTag == Tags.Abilities_Thunder_Electrocute)
    {
        return LightningChainFamily();
    }
    return {};
}

// 每个装备 Spec 只参与一次，套装倍率替换本件普通倍率。所有参数均为本次动作的局部值。
bool EquipmentAbilityModifiers::ApplySpellRules(const FAbilitySourceToken& Source, FGameplayTag AbilityTag,
                                                float& Radius, FDamageEffectParams& Damage, int32& ExtraChainTargets)
{
    const FGameplayTag Family = GetSpellFamily(AbilityTag);
    if (!Family.IsValid())
    {
        return true;
    }
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return false;
    }

    const auto* ASC = Source.ASC.Get();
    const FGameplayTagContainer OwnedTags = ASC->GetOwnedGameplayTags();
    double DamageFactor = 1.0;
    double RadiusFactor = 1.0;
    int32 Extra = 0;
    bool bMatched = false;
    for (const auto& Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
    {
        const auto* Active = ASC->GetActiveGameplayEffect(Handle);
        if (!Active || Active->bIsInhibited)
        {
            continue;
        }
        const auto* Effect = Cast<UUniqueEquipmentGameplayEffect>(Active->Spec.Def);
        if (!Effect)
        {
            continue;
        }
        const auto& Rule = Effect->SpellRule;
        const float Level = Active->Spec.GetLevel();
        if ((Rule.AbilityTag != AbilityTag && Rule.FamilyTag != Family) || !Rule.IsValid(Level))
        {
            continue;
        }
        bMatched = true;
        const bool bSetActive = !Rule.RequiredSetTags.IsEmpty() && OwnedTags.HasAllExact(Rule.RequiredSetTags);
        DamageFactor *= (bSetActive ? Rule.SetDamageMultiplier : Rule.DamageMultiplier).GetValueAtLevel(Level);
        RadiusFactor *= Rule.RadiusMultiplier.GetValueAtLevel(Level);
        Extra = FMath::Min(8, Extra + FMath::FloorToInt(Rule.ExtraChainTargets.GetValueAtLevel(Level)));
    }
    if (!bMatched)
    {
        return true;
    }
    const float ResolvedRadius = static_cast<float>(Radius * FMath::Clamp(RadiusFactor, .5, 2.0));
    const float BaseDamage = static_cast<float>(Damage.BaseDamage * DamageFactor);
    const float DebuffDamage = static_cast<float>(Damage.DebuffDamage * DamageFactor);
    if (!FMath::IsFinite(ResolvedRadius) || !FMath::IsFinite(BaseDamage) || !FMath::IsFinite(DebuffDamage))
    {
        return false;
    }
    Radius = ResolvedRadius;
    Damage.BaseDamage = BaseDamage;
    Damage.DebuffDamage = DebuffDamage;
    ExtraChainTargets = Extra;
    return true;
}

bool EquipmentAbilityModifiers::ApplyProjectileRules(const FAbilitySourceToken& Source,
                                                     FAbilityActorProjectileSettings& Settings,
                                                     FDamageEffectParams& Damage, int32& Count)
{
    if (!Settings.EquipmentAbilityTag.IsValid())
    {
        return true;
    }
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return false;
    }
    const auto* ASC = Source.ASC.Get();
    const FGameplayTagContainer OwnedTags = ASC->GetOwnedGameplayTags();
    const auto Handles = ASC->GetActiveEffects(FGameplayEffectQuery());
    auto ResolvedSettings = Settings;
    auto ResolvedDamage = Damage;
    int32 ResolvedCount = Count;
    bool bMatched = false;
    for (const auto& Handle : Handles)
    {
        const auto* Active = ASC->GetActiveGameplayEffect(Handle);
        if (!Active || Active->bIsInhibited)
        {
            continue;
        }
        const auto* Effect = Cast<UUniqueEquipmentGameplayEffect>(Active->Spec.Def);
        if (!Effect)
        {
            continue;
        }
        const auto& Rule = Effect->ProjectileRule;
        const float Level = Active->Spec.GetLevel();
        if (Rule.AbilityTag != Settings.EquipmentAbilityTag || !Rule.IsValid(Level))
        {
            continue;
        }
        // 每个独立装备Spec应用一次；标签只用于判定不同套装件，不根据标签计数重复应用。
        bMatched = true;
        ResolvedCount = FMath::Clamp(ResolvedCount, 0, 32);
        ResolvedCount = FMath::Min(32, ResolvedCount + FMath::FloorToInt(Rule.ExtraProjectiles.GetValueAtLevel(Level)));
        const bool bSetActive = !Rule.RequiredSetTags.IsEmpty() && OwnedTags.HasAllExact(Rule.RequiredSetTags);
        const float Multiplier = (bSetActive ? Rule.SetDamageMultiplier : Rule.DamageMultiplier).GetValueAtLevel(Level);
        ResolvedDamage.BaseDamage *= Multiplier;
        ResolvedDamage.DebuffDamage *= Multiplier;
        ResolvedSettings.Spread = FMath::Max(ResolvedSettings.Spread, Rule.MinimumSpread.GetValueAtLevel(Level));
        if (Rule.bDisableHoming)
        {
            // 即使图未配置追踪，也覆盖投射物类自身的默认追踪开关。
            ResolvedSettings.bConfigureHoming = true;
            ResolvedSettings.bLaunchHoming = false;
        }
        if (!FMath::IsFinite(ResolvedDamage.BaseDamage) || !FMath::IsFinite(ResolvedDamage.DebuffDamage))
        {
            return false;
        }
    }
    if (bMatched)
    {
        Settings = ResolvedSettings;
        Damage = ResolvedDamage;
        Count = ResolvedCount;
    }
    return true;
}
