// 调用链：EquipmentComponent 校验物品 GE → 共享约束与数值检查 → 服务器构造独立 Spec。
#include "Equipment/EquipmentGameplayEffect.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "Equipment/EquipmentStats.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

// 所有装备使用可按实例卸除的非周期、非堆叠持续效果。
UEquipmentGameplayEffect::UEquipmentGameplayEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    Period = FScalableFloat(0.f);
    StackingType = EGameplayEffectStackingType::None;
}

// 普通与随机装备维持原有加法权限，不能通过数据配置获得暗金的乘法能力。
bool UEquipmentGameplayEffect::IsSafeEquipmentEffect(float Level, bool bAllowRolled) const
{
    return ValidateEquipmentEffect(Level, bAllowRolled, false);
}

// 统一限制资源/元属性、任意副作用组件和计算操作；不修改共享模板或当前生效实例。
bool UEquipmentGameplayEffect::ValidateEquipmentEffect(float Level, bool bAllowRolled,
                                                       bool bAllowCompoundMultipliers) const
{
    if (!GetDefault<UGameplayAbilitiesDeveloperSettings>()->bAllowGameplayModEvaluationChannels ||
        !FMath::IsFinite(Level) || Level < 1.f || DurationPolicy != EGameplayEffectDurationType::Infinite ||
        Period.GetValueAtLevel(Level) != 0.f || GetStackingType() != EGameplayEffectStackingType::None ||
        !Executions.IsEmpty())
    {
        return false;
    }
    for (const auto& Component : GEComponents)
    {
        if (Component && Component->GetClass() != UTargetTagsGameplayEffectComponent::StaticClass())
        {
            return false;
        }
    }
    if (bAllowRolled && Modifiers.IsEmpty())
    {
        return false;
    }
    TSet<FGameplayAttribute> SeenAttributes;
    for (const auto& Modifier : Modifiers)
    {
        if (bAllowRolled && SeenAttributes.Contains(Modifier.Attribute))
        {
            return false;
        }
        SeenAttributes.Add(Modifier.Attribute);
        float Value = 0.f;
        if (!Modifier.Attribute.IsValid() ||
            Modifier.Attribute.GetAttributeSetClass() != UMyAttributeSet::StaticClass() ||
            Modifier.Attribute == UMyAttributeSet::GetHealthAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetManaAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetIncomingDamageAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetIncomingXPAttribute())
        {
            return false;
        }
        const bool bAdditive =
            Modifier.ModifierOp == EGameplayModOp::Additive &&
            Modifier.EvaluationChannelSettings.GetEvaluationChannel() == EGameplayModEvaluationChannel::Channel1;
        const bool bCompound =
            bAllowCompoundMultipliers && !bAllowRolled && Modifier.ModifierOp == EGameplayModOp::MultiplyCompound &&
            Modifier.EvaluationChannelSettings.GetEvaluationChannel() == EGameplayModEvaluationChannel::Channel2;
        if (!bAdditive && !bCompound)
        {
            return false;
        }
        if (bAllowRolled)
        {
            if (Modifier.ModifierMagnitude.GetMagnitudeCalculationType() !=
                EGameplayEffectMagnitudeCalculation::SetByCaller)
            {
                return false;
            }
            const auto Tag = Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag;
            const auto* Stat = EquipmentStats::Find(Tag);
            const bool bMatches = Stat && Stat->Attribute == Modifier.Attribute;
            if (!bMatches)
            {
                return false;
            }
        }
        else if (!Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(Level, Value) || !FMath::IsFinite(Value))
        {
            return false;
        }
        // 0 或负倍率会破坏资源上限与逆运算；范围也限制错误资产造成的极端倍率。
        else if (bCompound && (Value < 0.05f || Value > 10.f))
        {
            return false;
        }
    }
    return true;
}

// 随机装备继续使用逐属性 SetByCaller 加法模板，实际值由服务器保存的 RollData 注入。
URandomEquipmentGameplayEffect::URandomEquipmentGameplayEffect()
{
    for (const auto& Stat : EquipmentStats::All())
    {
        FGameplayModifierInfo M;
        M.Attribute = Stat.Attribute;
        M.ModifierOp = EGameplayModOp::Additive;
        M.EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel1);
        FSetByCallerFloat Magnitude;
        Magnitude.DataTag = Stat.Tag;
        M.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
        Modifiers.Add(M);
    }
}
