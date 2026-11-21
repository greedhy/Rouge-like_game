// 调用链：真实ASC激活/原SpawnProjectiles通知 -> 生产默认配方；测试只固定资产无关的原技能参数。
#pragma once
#include "AbilityRuntimeMageBridgeTestTypes.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "AbilityNativeFireBoltProfileTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class UAbilityNativeFireBoltProfileTest : public UAbilityRuntimeFireBoltBridgeTest
{
    GENERATED_BODY()
public:
    UAbilityNativeFireBoltProfileTest()
    {
        NumProjectiles = 5;
        ProjectileSpread = 20.f;
        HomingAccelerationMin = 1600.f;
        HomingAccelerationMax = 3200.f;
        Damage = FScalableFloat(25.f);
        DamageEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        DebuffDamage = 8.f;
        DebuffChance = 0.f;
        DeathImpulseMagnitude = 900.f;
        KnockbackChance = 0.f;
    }
    /** 测试同一生产Notify在两种真实弹体class下的组合，不修改任何资产/CDO。 */
    void ConfigureNativePropagationClass(TSubclassOf<AMyProjectile> Class)
    {
        ProjectileClass = Class;
        NumProjectiles = 1;
        ProjectileSpread = 0.f;
    }
};
