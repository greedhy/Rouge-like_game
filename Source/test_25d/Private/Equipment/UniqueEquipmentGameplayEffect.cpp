// 调用链：装备事务/共享动作规则读取 → 暗金配置校验 → 现有装备 GE 与发射/接触动作。
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Equipment/EquipmentAbilityModifiers.h"
#include "MyGameplayTags.h"

namespace
{
    // 所有技能倍率与属性倍率采用相同有限区间，避免不受控或不可逆的配置。
    bool IsSafeMultiplier(float Value)
    {
        return FMath::IsFinite(Value) && Value >= 0.05f && Value <= 10.f;
    }
} // namespace

// 专用暗金拒绝随机模板语义，再组合通用 GE 安全约束和可选技能规则。
bool UUniqueEquipmentGameplayEffect::IsSafeEquipmentEffect(float Level, bool bAllowRolled) const
{
    return !bAllowRolled && ValidateEquipmentEffect(Level, false, true) && ProjectileRule.IsValid(Level) &&
           MeleeRule.IsValid(Level) && SpellRule.IsValid(Level);
}

// 空规则必须保持默认值；启用时解析当前装备等级，超出边界的规则拒绝装备而非静默截断。
bool FEquipmentProjectileRule::IsValid(float Level) const
{
    if (!FMath::IsFinite(Level) || Level < 1.f)
    {
        return false;
    }
    const float Extra = ExtraProjectiles.GetValueAtLevel(Level);
    const float Damage = DamageMultiplier.GetValueAtLevel(Level);
    const float Spread = MinimumSpread.GetValueAtLevel(Level);
    const float SetDamage = SetDamageMultiplier.GetValueAtLevel(Level);
    if (!FMath::IsFinite(Extra) || Extra < 0.f || Extra > 16.f || !IsSafeMultiplier(Damage) ||
        !FMath::IsFinite(Spread) || Spread < 0.f || Spread > 360.f || !IsSafeMultiplier(SetDamage))
    {
        return false;
    }
    if (!AbilityTag.IsValid())
    {
        return Extra == 0.f && Damage == 1.f && Spread == 0.f && !bDisableHoming && RequiredSetTags.IsEmpty() &&
               SetDamage == 1.f;
    }
    // 套装要求至少两种不同部件标签，无法通过同件堆叠或单个标签冒充两件套。
    return RequiredSetTags.IsEmpty() ? SetDamage == 1.f : RequiredSetTags.Num() >= 2;
}

// 近战转换仅接受既有伤害类型；两件套需要不同部件标签，未配置套装时保留旧暗金语义。
bool FEquipmentMeleeRule::IsValid(float Level) const
{
    if (!FMath::IsFinite(Level) || Level < 1.f)
    {
        return false;
    }
    const float Damage = DamageMultiplier.GetValueAtLevel(Level);
    const float SetDamage = SetDamageMultiplier.GetValueAtLevel(Level);
    const float Duration = BodyEffectDuration.GetValueAtLevel(Level);
    if (!IsSafeMultiplier(Damage) || !IsSafeMultiplier(SetDamage) || !FMath::IsFinite(Duration) ||
        (BodyEffect ? (Duration < 0.1f || Duration > 3.f) : Duration != 0.f))
    {
        return false;
    }
    if (!AbilityTag.IsValid())
    {
        return Damage == 1.f && !DamageTypeOverride.IsValid() && !HitEffect && !BodyEffect &&
               RequiredSetTags.IsEmpty() &&
               SetDamage == 1.f;
    }
    return FMyGameplayTags::Get().DamageTypesToResistance.Contains(DamageTypeOverride) &&
           (RequiredSetTags.IsEmpty() ? SetDamage == 1.f : RequiredSetTags.Num() >= 2);
}

// 只支持明确接入的动作系列，避免把一个空或过宽的选择器应用到旧技能。
bool FEquipmentSpellRule::IsValid(float Level) const
{
    if (!FMath::IsFinite(Level) || Level < 1.f)
    {
        return false;
    }
    const float Damage = DamageMultiplier.GetValueAtLevel(Level);
    const float SetDamage = SetDamageMultiplier.GetValueAtLevel(Level);
    const float Radius = RadiusMultiplier.GetValueAtLevel(Level);
    const float Extra = ExtraChainTargets.GetValueAtLevel(Level);
    if (!IsSafeMultiplier(Damage) || !IsSafeMultiplier(SetDamage) || !FMath::IsFinite(Radius) || Radius < .5f ||
        Radius > 2.f || !FMath::IsFinite(Extra) || Extra < 0.f || Extra > 8.f)
    {
        return false;
    }
    if (!AbilityTag.IsValid() && !FamilyTag.IsValid())
    {
        return Damage == 1.f && Radius == 1.f && Extra == 0.f && RequiredSetTags.IsEmpty() && SetDamage == 1.f;
    }
    if (AbilityTag.IsValid() && FamilyTag.IsValid())
    {
        return false;
    }
    const FGameplayTag Family = AbilityTag.IsValid() ? EquipmentAbilityModifiers::GetSpellFamily(AbilityTag) : FamilyTag;
    if (Family != EquipmentAbilityModifiers::LightningChainFamily() &&
        (Family != EquipmentAbilityModifiers::ArcaneAreaFamily() || Extra != 0.f))
    {
        return false;
    }
    return RequiredSetTags.IsEmpty() ? SetDamage == 1.f : RequiredSetTags.Num() >= 2;
}
