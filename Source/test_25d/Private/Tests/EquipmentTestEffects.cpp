/** 主要调用链：装备自动化夹具 -> 构造测试GE -> Channel1加法Modifier -> EquipmentComponent应用/卸除 -> 属性断言。 */
#include "Tests/EquipmentTestEffects.h"
#include "AbilitySystem/MyAttributeSet.h"
namespace
{
    // 向装备计算通道追加一个固定数值的属性加法项。
    void Add(UGameplayEffect& GE, FGameplayAttribute Attribute, float Value)
    {
        FGameplayModifierInfo M;
        M.Attribute = Attribute;
        M.ModifierOp = EGameplayModOp::Additive;
        M.EvaluationChannelSettings.SetEvaluationChannel(EGameplayModEvaluationChannel::Channel1);
        M.ModifierMagnitude = FScalableFloat(Value);
        GE.Modifiers.Add(M);
    }
} // namespace
// 创建+5护甲的装备效果，用于穿脱还原验证。
UEquipmentTestArmor::UEquipmentTestArmor()
{
    Add(*this, UMyAttributeSet::GetArmorAttribute(), 5.f);
}
// 创建+7护甲的替换效果，用于区分新旧装备。
UEquipmentTestArmorSeven::UEquipmentTestArmorSeven()
{
    Add(*this, UMyAttributeSet::GetArmorAttribute(), 7.f);
}
// 创建+50最大生命效果，用于资源上限变更验证。
UEquipmentTestHealth::UEquipmentTestHealth()
{
    Add(*this, UMyAttributeSet::GetMaxHealthAttribute(), 50.f);
}
