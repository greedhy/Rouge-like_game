/**
 * 框架：共享只读 DataAsset 描述 Kind、参数和 OnHit/OnPeriod/OnExpire 连接；Context 是每次施法共享的运行状态。
 * 调用链：GA Execute 创建 Context -> Runtime Spawn 校验节点并预留预算 -> Emit 复用 Context 生成子节点。不要在施法时修改共享 DA。
 */
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "NativeGameplayTags.h"
#include "Terrain/TerrainSurface.h"

#define PROTOTYPE_TAG(Name)                                                                                            \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Ability##Name, "Abilities.Prototype." #Name);                                        \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Cooldown##Name, "Cooldown.Prototype." #Name);
PROTOTYPE_TAG(Projectile)
PROTOTYPE_TAG(Multishot)
PROTOTYPE_TAG(Burst)
PROTOTYPE_TAG(Beam)
PROTOTYPE_TAG(Surface)
PROTOTYPE_TAG(Orbit)
PROTOTYPE_TAG(Field)
PROTOTYPE_TAG(Trap)
PROTOTYPE_TAG(Homing)
PROTOTYPE_TAG(Piercing)
PROTOTYPE_TAG(Chain)
PROTOTYPE_TAG(Cone)
PROTOTYPE_TAG(Wave)
PROTOTYPE_TAG(Returning)
PROTOTYPE_TAG(Emitter)
PROTOTYPE_TAG(Delay)
PROTOTYPE_TAG(Ring)
PROTOTYPE_TAG(Line)
#undef PROTOTYPE_TAG
#define COMPOSITION_TAG(Name)                                                                                          \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Ability##Name, "Abilities.Composition." #Name);                                      \
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Cooldown##Name, "Cooldown.Composition." #Name);
COMPOSITION_TAG(FireSeed)
COMPOSITION_TAG(FrostFan)
COMPOSITION_TAG(BrambleMine)
COMPOSITION_TAG(StormSeal)
COMPOSITION_TAG(Aftershock)
COMPOSITION_TAG(FrostFault)
COMPOSITION_TAG(RippleMine)
COMPOSITION_TAG(HunterSentry)
#undef COMPOSITION_TAG

/** 激活及生成前的节点校验：限制有限数值、几何范围和行为连接；不是整张组合图的递归拓扑校验。 */
bool UAbilityPrototype::IsValidConfiguration() const
{
    for (float Value :
         {ManaCost, ManaPerSecond, Cooldown, Range, Radius, Duration, Period, Damage, DamagePerLevel, SpreadDegrees,
          Speed, CollisionRadius, ArmDelay, HomingTurnRate, DamageFalloff, InnerRadius, Width, ShapeHalfHeight})
        if (!FMath::IsFinite(Value) || Value < 0.f)
            return false;
    return static_cast<uint8>(Kind) <= static_cast<uint8>(EAbilityPrototypeKind::Line) && HomingTurnRate >= 1.f &&
           HomingTurnRate <= 720.f && DamageFalloff <= 1.f &&
           (Kind < EAbilityPrototypeKind::Homing || Kind == EAbilityPrototypeKind::Emitter || !OnPeriod) &&
           (Kind != EAbilityPrototypeKind::Emitter || (OnPeriod && !OnHit)) &&
           (Kind != EAbilityPrototypeKind::Delay || (OnExpire && !OnHit && ArmDelay >= .1f && ArmDelay <= Duration)) &&
           (Kind != EAbilityPrototypeKind::Ring || InnerRadius < Radius) &&
           (Kind != EAbilityPrototypeKind::Line || (Width > 0.f && Width <= 2000.f)) &&
           ((Kind != EAbilityPrototypeKind::Ring && Kind != EAbilityPrototypeKind::Line) ||
            (ShapeHalfHeight > 0.f && ShapeHalfHeight <= 1000.f)) &&
           (Kind != EAbilityPrototypeKind::Cone || (SpreadDegrees > 0.f && SpreadDegrees <= 180.f)) && Range > 0.f &&
           Range <= 10000.f && Radius > 0.f && Radius <= 3000.f && Duration >= .1f && Duration <= 60.f &&
           Period >= .1f && Speed > 0.f && Speed <= 10000.f && CollisionRadius > 0.f && CollisionRadius <= 200.f &&
           Count >= 1 && Count <= 16 && SpreadDegrees <= 360.f && DamageType.IsValid() &&
           (Cooldown == 0.f || CooldownTag.IsValid()) &&
           (Kind != EAbilityPrototypeKind::Surface || (SurfaceClass && !OnHit)) &&
           (Kind != EAbilityPrototypeKind::Orbit || (OrbitSettings && OrbitSettings->IsValidConfiguration()));
}

/** 伤害与 UI 共用等级计算：基础值加线性成长，成长等级限制在 0..99。 */
float UAbilityPrototype::DamageAtLevel(int32 Level) const
{
    return Damage + DamagePerLevel * FMath::Clamp(Level - 1, 0, 99);
}

/** Runtime::Spawn 的共享预算入口：取消状态、深度超出 0..3 或累计达到 64 时拒绝；成功立即占用一次预算。 */
bool UPrototypeCastContext::ReserveSpawn(int32 Depth)
{
    if (bCancelled || Depth < 0 || Depth > 3 || Spawned >= 64)
        return false;
    ++Spawned;
    return true;
}
