// 主要调用链：状态组件 UpdateEffects 选择 GE 类 → 构造函数调用 Configure → 标签生效；燃烧/中毒通过 IncomingDamage 接入属性伤害。
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "NativeGameplayTags.h"
#include "TerrainStatusEffects.generated.h"

namespace TerrainTags
{
    TEST_25D_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Burning);
    TEST_25D_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Frost);
    TEST_25D_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Poison);
    TEST_25D_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Entangled);
    TEST_25D_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage);
} // namespace TerrainTags

UCLASS()
class TEST_25D_API UTerrainBurnEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    // 创建燃烧标签及周期伤害配置。
    UTerrainBurnEffect();
};
UCLASS()
class TEST_25D_API UTerrainFrostEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    // 创建冰霜标签配置，速度倍率由状态组件维护。
    UTerrainFrostEffect();
};
UCLASS()
class TEST_25D_API UTerrainPoisonEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    // 创建中毒标签及周期伤害配置。
    UTerrainPoisonEffect();
};
UCLASS()
class TEST_25D_API UTerrainEntangleEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    // 创建缠绕标签配置，持续和恢复窗口由状态组件管理。
    UTerrainEntangleEffect();
};
