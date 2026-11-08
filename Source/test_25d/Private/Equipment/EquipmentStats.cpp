// 调用链：随机生成校验和装备效果构建查询 All/Find → 获取 Tag、GAS 属性与显示名称的统一映射。
#include "Equipment/EquipmentStats.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentStrength, "Equipment.Stat.Strength");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentIntelligence, "Equipment.Stat.Intelligence");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentResilience, "Equipment.Stat.Resilience");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentVigor, "Equipment.Stat.Vigor");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentArmor, "Equipment.Stat.Armor");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentArmorPene, "Equipment.Stat.ArmorPene");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentBlockChance, "Equipment.Stat.BlockChance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentCriticalHitChance, "Equipment.Stat.CriticalHitChance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentCriticalHitDamage, "Equipment.Stat.CriticalHitDamage");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentCriticalHitResistence, "Equipment.Stat.CriticalHitResistence");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentHealthRegeneration, "Equipment.Stat.HealthRegeneration");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentManaRegeneration, "Equipment.Stat.ManaRegeneration");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentMaxHealth, "Equipment.Stat.MaxHealth");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentMaxMana, "Equipment.Stat.MaxMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentFireResistance, "Equipment.Stat.FireResistance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentThunderResistance, "Equipment.Stat.ThunderResistance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentWaterResistance, "Equipment.Stat.WaterResistance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentMagicResistance, "Equipment.Stat.MagicResistance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentPhysicsResistance, "Equipment.Stat.PhysicsResistance");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EquipmentPureResistance, "Equipment.Stat.PureResistance");
// 返回静态装备属性白名单，供生成、效果映射和界面共用。
const TArray<FEquipmentStatDefinition>& EquipmentStats::All()
{
    static const TArray<FEquipmentStatDefinition> Stats = {
        {TAG_EquipmentStrength, UMyAttributeSet::GetStrengthAttribute(), TEXT("力量")},
        {TAG_EquipmentIntelligence, UMyAttributeSet::GetIntelligenceAttribute(), TEXT("智力")},
        {TAG_EquipmentResilience, UMyAttributeSet::GetResilienceAttribute(), TEXT("韧性")},
        {TAG_EquipmentVigor, UMyAttributeSet::GetVigorAttribute(), TEXT("活力")},
        {TAG_EquipmentArmor, UMyAttributeSet::GetArmorAttribute(), TEXT("护甲")},
        {TAG_EquipmentArmorPene, UMyAttributeSet::GetArmorPeneAttribute(), TEXT("护甲穿透")},
        {TAG_EquipmentBlockChance, UMyAttributeSet::GetBlockChanceAttribute(), TEXT("格挡率")},
        {TAG_EquipmentCriticalHitChance, UMyAttributeSet::GetCriticalHitChanceAttribute(), TEXT("暴击率")},
        {TAG_EquipmentCriticalHitDamage, UMyAttributeSet::GetCriticalHitDamageAttribute(), TEXT("暴击伤害")},
        {TAG_EquipmentCriticalHitResistence, UMyAttributeSet::GetCriticalHitResistenceAttribute(), TEXT("暴击抗性")},
        {TAG_EquipmentHealthRegeneration, UMyAttributeSet::GetHealthRegenerationAttribute(), TEXT("生命恢复")},
        {TAG_EquipmentManaRegeneration, UMyAttributeSet::GetManaRegenerationAttribute(), TEXT("法力恢复")},
        {TAG_EquipmentMaxHealth, UMyAttributeSet::GetMaxHealthAttribute(), TEXT("最大生命")},
        {TAG_EquipmentMaxMana, UMyAttributeSet::GetMaxManaAttribute(), TEXT("最大法力")},
        {TAG_EquipmentFireResistance, UMyAttributeSet::GetFireResistanceAttribute(), TEXT("火焰抗性")},
        {TAG_EquipmentThunderResistance, UMyAttributeSet::GetThunderResistanceAttribute(), TEXT("雷电抗性")},
        {TAG_EquipmentWaterResistance, UMyAttributeSet::GetWaterResistanceAttribute(), TEXT("水系抗性")},
        {TAG_EquipmentMagicResistance, UMyAttributeSet::GetMagicResistanceAttribute(), TEXT("魔法抗性")},
        {TAG_EquipmentPhysicsResistance, UMyAttributeSet::GetPhysicsResistanceAttribute(), TEXT("物理抗性")},
        {TAG_EquipmentPureResistance, UMyAttributeSet::GetPureResistanceAttribute(), TEXT("纯粹抗性")},
    };
    return Stats;
}
// 按精确 Tag 查找注册项；未注册时返回空指针。
const FEquipmentStatDefinition* EquipmentStats::Find(FGameplayTag Tag)
{
    return All().FindByPredicate(
        [Tag](const auto& Stat)
        {
            return Stat.Tag == Tag;
        });
}
