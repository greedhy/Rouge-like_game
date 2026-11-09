// 调用链：测试 GE 默认对象构造 -> Arcane 原伤害通路施加 -> Health 加法修饰。
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/MyAttributeSet.h"

UArcaneShardTestDamageEffect::UArcaneShardTestDamageEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    auto& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-10.f);
}
