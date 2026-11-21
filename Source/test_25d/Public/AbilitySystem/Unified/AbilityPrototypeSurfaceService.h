// 调用链：原Actor/Mixed驱动或细图资源节点 -> 同一真实地表工厂；Phase/原驱动共用旧周期策略。
#pragma once
#include "CoreMinimal.h"
class ATerrainSurface;
class UAbilityPrototype;
class AActor;
enum class EAbilityPrototypeSurfaceProfile : uint8
{
    Actor,
    Mixed
};
namespace AbilityPrototypeSurfaceService
{
    /** 参数已冻结/坐标已投射；只创建准确配置Actor，不再预留逻辑生成、启动第二套伤害或场时钟。 */
    TEST_25D_API ATerrainSurface* Spawn(AActor& Caster, const UAbilityPrototype& Parameters, FVector Position,
                                        EAbilityPrototypeSurfaceProfile Profile);
    /** Actor单次fmod；Mixed保留原辅助/尾部两个max8周期，拒绝可选孩子不终止父时钟。 */
    TEST_25D_API void AdvanceClock(float& Clock, float Delta, float Period, EAbilityPrototypeSurfaceProfile Profile,
                                   TFunctionRef<bool()> Pulse);
} // namespace AbilityPrototypeSurfaceService
