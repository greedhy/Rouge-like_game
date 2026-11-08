// 主要调用链：状态组件 UpdateEffects 选择 GE 类 → 构造函数调用 Configure → 标签生效；燃烧/中毒通过 IncomingDamage 接入属性伤害。
#include "Terrain/TerrainStatusEffects.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace TerrainTags
{
    UE_DEFINE_GAMEPLAY_TAG(Burning, "Debuff.Terrain.Burning");
    UE_DEFINE_GAMEPLAY_TAG(Frost, "Debuff.Terrain.Frost");
    UE_DEFINE_GAMEPLAY_TAG(Poison, "Debuff.Terrain.Poison");
    UE_DEFINE_GAMEPLAY_TAG(Entangled, "Debuff.Terrain.Entangled");
    UE_DEFINE_GAMEPLAY_TAG(Damage, "Data.Terrain.Damage");
} // namespace TerrainTags

namespace
{
    // 配置无限持续标签效果；伤害类型额外按秒将 SetByCaller 数值写入 IncomingDamage。
    void Configure(UGameplayEffect& Effect, UTargetTagsGameplayEffectComponent& Component, const FGameplayTag& Tag,
                   bool bDamage)
    {
        Effect.DurationPolicy = EGameplayEffectDurationType::Infinite;
        FInheritedTagContainer Tags;
        Tags.AddTag(Tag);
        Component.SetAndApplyTargetTagChanges(Tags);
        if (bDamage)
        {
            Effect.Period = FScalableFloat(1.f);
            Effect.bExecutePeriodicEffectOnApplication = false;
            FSetByCallerFloat Damage;
            Damage.DataTag = TerrainTags::Damage;
            FGameplayModifierInfo Modifier;
            Modifier.Attribute = UMyAttributeSet::GetIncomingDamageAttribute();
            Modifier.ModifierOp = EGameplayModOp::Additive;
            Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Damage);
            Effect.Modifiers.Add(Modifier);
        }
    }
} // namespace
// 创建燃烧标签及周期伤害配置。
UTerrainBurnEffect::UTerrainBurnEffect()
{
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TerrainTags"));
    GEComponents.Add(Tags);
    Configure(*this, *Tags, TerrainTags::Burning, true);
}
// 创建冰霜标签配置，速度倍率由状态组件维护。
UTerrainFrostEffect::UTerrainFrostEffect()
{
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TerrainTags"));
    GEComponents.Add(Tags);
    Configure(*this, *Tags, TerrainTags::Frost, false);
}
// 创建中毒标签及周期伤害配置。
UTerrainPoisonEffect::UTerrainPoisonEffect()
{
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TerrainTags"));
    GEComponents.Add(Tags);
    Configure(*this, *Tags, TerrainTags::Poison, true);
}
// 创建缠绕标签配置，持续和恢复窗口由状态组件管理。
UTerrainEntangleEffect::UTerrainEntangleEffect()
{
    auto* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TerrainTags"));
    GEComponents.Add(Tags);
    Configure(*this, *Tags, TerrainTags::Entangled, false);
}
