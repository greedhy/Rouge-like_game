#include "Equipment/EquipmentGameplayEffect.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "Equipment/EquipmentStats.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UEquipmentGameplayEffect::UEquipmentGameplayEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    Period = FScalableFloat(0.f);
    StackingType = EGameplayEffectStackingType::None;
}

bool UEquipmentGameplayEffect::IsSafeEquipmentEffect(float Level, bool bAllowRolled) const
{
    if (!GetDefault<UGameplayAbilitiesDeveloperSettings>()->bAllowGameplayModEvaluationChannels ||
        !FMath::IsFinite(Level) || Level < 1.f || DurationPolicy != EGameplayEffectDurationType::Infinite ||
        Period.GetValueAtLevel(Level) != 0.f || GetStackingType() != EGameplayEffectStackingType::None ||
        !Executions.IsEmpty())
        return false;
    for (const auto& Component : GEComponents)
        if (Component && Component->GetClass() != UTargetTagsGameplayEffectComponent::StaticClass())
            return false;
    if (bAllowRolled && Modifiers.IsEmpty())
        return false;
    TSet<FGameplayAttribute> SeenAttributes;
    for (const auto& Modifier : Modifiers)
    {
        if (bAllowRolled && SeenAttributes.Contains(Modifier.Attribute))
            return false;
        SeenAttributes.Add(Modifier.Attribute);
        float Value = 0.f;
        if (!Modifier.Attribute.IsValid() ||
            Modifier.Attribute.GetAttributeSetClass() != UMyAttributeSet::StaticClass() ||
            Modifier.Attribute == UMyAttributeSet::GetHealthAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetManaAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetIncomingDamageAttribute() ||
            Modifier.Attribute == UMyAttributeSet::GetIncomingXPAttribute() ||
            Modifier.EvaluationChannelSettings.GetEvaluationChannel() != EGameplayModEvaluationChannel::Channel1 ||
            Modifier.ModifierOp != EGameplayModOp::Additive)
            return false;
        if (bAllowRolled)
        {
            if (Modifier.ModifierMagnitude.GetMagnitudeCalculationType() !=
                EGameplayEffectMagnitudeCalculation::SetByCaller)
                return false;
            const auto Tag = Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag;
            const auto* Stat = EquipmentStats::Find(Tag);
            const bool bMatches = Stat && Stat->Attribute == Modifier.Attribute;
            if (!bMatches)
                return false;
        }
        else if (!Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(Level, Value) || !FMath::IsFinite(Value))
            return false;
    }
    return true;
}

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
