// 调用链：ExecuteStrike → Resolve → 本ASC生效暗金GE → 本次伤害类型/数值快照；无授予或清理副作用。
#include "Equipment/EquipmentMeleeModifiers.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "AbilitySystemComponent.h"

// 活动效果按实例参与计算；拒绝多个不同元素转换，而不是依赖GE容器的遍历顺序决定胜者。
bool EquipmentMeleeModifiers::Resolve(UAbilitySystemComponent* ASC, FGameplayTag AbilityTag,
                                      FEquipmentMeleeSnapshot& InOutSnapshot)
{
    if (!AbilityTag.IsValid())
    {
        return true;
    }
    if (!ASC)
    {
        return false;
    }
    FEquipmentMeleeSnapshot Snapshot = InOutSnapshot;
    const FGameplayTagContainer OwnedTags = ASC->GetOwnedGameplayTags();
    FGameplayTag Conversion;
    UNiagaraSystem* ConversionEffect = nullptr;
    for (const FActiveGameplayEffectHandle Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
    {
        const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
        if (!Active || Active->bIsInhibited)
        {
            continue;
        }
        const auto* Unique = Cast<UUniqueEquipmentGameplayEffect>(Active->Spec.Def);
        if (!Unique || Unique->MeleeRule.AbilityTag != AbilityTag)
        {
            continue;
        }
        const auto& Rule = Unique->MeleeRule;
        const float Level = Active->Spec.GetLevel();
        if (!Rule.IsValid(Level) || (Conversion.IsValid() && Conversion != Rule.DamageTypeOverride) ||
            (ConversionEffect && Rule.HitEffect && ConversionEffect != Rule.HitEffect) ||
            (Snapshot.BodyEffect && Rule.BodyEffect && Snapshot.BodyEffect != Rule.BodyEffect))
        {
            return false;
        }
        Conversion = Rule.DamageTypeOverride;
        Snapshot.DamageType = Conversion;
        // 按当前独立Spec等级选择替代倍率，不把套装倍率再次乘到普通倍率上。
        const bool bSetActive = !Rule.RequiredSetTags.IsEmpty() && OwnedTags.HasAllExact(Rule.RequiredSetTags);
        const float Multiplier = (bSetActive ? Rule.SetDamageMultiplier : Rule.DamageMultiplier).GetValueAtLevel(Level);
        Snapshot.Damage *= Multiplier;
        if (!FMath::IsFinite(Snapshot.Damage) || Snapshot.Damage < 0.f)
        {
            return false;
        }
        if (Rule.HitEffect)
        {
            ConversionEffect = Rule.HitEffect;
            Snapshot.HitEffect = ConversionEffect;
        }
        if (Rule.BodyEffect)
        {
            Snapshot.BodyEffect = Rule.BodyEffect;
            Snapshot.BodyEffectDuration =
                FMath::Max(Snapshot.BodyEffectDuration, Rule.BodyEffectDuration.GetValueAtLevel(Level));
        }
    }
    InOutSnapshot = Snapshot;
    return true;
}
