// 调用链：测试Spec -> SetByCaller Health/Infinite Armor -> ASC真实应用与按句柄撤回。
#include "AbilityGameplayEffectTestTypes.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UtilityValue, "Tests.UnifiedGA.Utility.Value");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UtilityOther, "Tests.UnifiedGA.Utility.Other");

UUtilityInstantTestEffect::UUtilityInstantTestEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    auto& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Magnitude;
    Magnitude.DataTag = TAG_UtilityValue;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
}

UUtilityOwnedTestEffect::UUtilityOwnedTestEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    auto& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetArmorAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(20.f);
}

UUtilityHitReactTestAbility::UUtilityHitReactTestAbility()
{
    HitReactEffectClass = UUtilityOwnedTestEffect::StaticClass();
}

UUtilityClientListenerTestAbility::UUtilityClientListenerTestAbility()
{
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}
