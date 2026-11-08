/** 主要调用链：背包自动化夹具 -> 构造1秒持续GE -> 物品使用通路 -> 效果与库存消耗断言。 */
#include "Tests/InventoryTestGameplayEffects.h"

UInventoryTestDurationGameplayEffect::UInventoryTestDurationGameplayEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1.f);
}
