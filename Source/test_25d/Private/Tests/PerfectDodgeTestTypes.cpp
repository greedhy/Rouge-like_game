// 测试链：ApplyDamageEffect -> 固定Health修饰与可查询Debuff标签；不加载用户资产。
#include "PerfectDodgeTestTypes.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DodgeTestDebuff, "Test.PerfectDodge.HitDebuff");

UPerfectDodgeHitTestEffect::UPerfectDodgeHitTestEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(5.f);
    auto& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-10.f);
    auto* Component = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("HitDebuff"));
    FInheritedTagContainer Tags;
    Tags.AddTag(TAG_DodgeTestDebuff);
    Component->SetAndApplyTargetTagChanges(Tags);
    GEComponents.Add(Component);
}
