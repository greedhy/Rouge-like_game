// 调用链：暗金自动化夹具构造测试效果 → 原装备事务按实例应用/卸除 → 验证实际属性。
#include "Tests/UniqueEquipmentTestEffects.h"
#include "AbilitySystem/MyAttributeSet.h"

namespace
{
    // 追加末级复合倍率，包含可独立卸除的正向增益与资源上限代价。
    void AddMultiplier(UGameplayEffect& Effect, FGameplayAttribute Attribute, float Multiplier)
    {
        FGameplayModifierInfo Modifier;
        Modifier.Attribute = Attribute;
        Modifier.ModifierOp = EGameplayModOp::MultiplyCompound;
        Modifier.EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel2);
        Modifier.ModifierMagnitude = FScalableFloat(Multiplier);
        Effect.Modifiers.Add(Modifier);
    }
} // namespace

// 两倍护甲伴随最大生命降低，检验正负倍率的共同生命周期。
UUniqueEquipmentTestOffense::UUniqueEquipmentTestOffense()
{
    AddMultiplier(*this, UMyAttributeSet::GetArmorAttribute(), 2.f);
    AddMultiplier(*this, UMyAttributeSet::GetMaxHealthAttribute(), 0.7f);
    UniqueDescription = FText::FromString(TEXT("测试暗金机制"));
}

// 第二件独立倍率用于区分复合相乘和传统加性乘法。
UUniqueEquipmentTestDefense::UUniqueEquipmentTestDefense()
{
    AddMultiplier(*this, UMyAttributeSet::GetArmorAttribute(), 1.5f);
    AddMultiplier(*this, UMyAttributeSet::GetMaxManaAttribute(), 0.5f);
}
