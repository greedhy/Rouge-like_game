// 调用链：原 Driver / 可替换 TargetSequence、TargetChannel -> 共享目标/LOS/节拍动作 -> 原生命令牌。
// 这些动作不执行伤害、不生成原型子节点；实际 payload 与表现所有权留给调用方。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"

enum class EAbilityPrototypeTargetProfile : uint8
{
    Actor,
    Mixed
};

namespace AbilityPrototypeTargetServices
{
    /** 原 Actor 原始 LOS 动作；只返回遮挡事实，不授予目标权限，调用方先验证原生命。 */
    TEST_25D_API bool ActorLineOfSight(UWorld* World, AActor* Caster, AActor* Target, const FVector& Origin, bool bBeam,
                                       AActor* IgnoredEffect = nullptr);
    /** 纯 LOS 共用入口；Actor Beam 保留其它战斗角色遮挡，Actor Chain 忽略非目标角色。 */
    TEST_25D_API bool HasVisibility(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                    const FVector& Origin, EAbilityPrototypeTargetProfile Profile, bool bBeam);
    /** 原生命/射程与 profile LOS 校验；Beam 从施法者中心检查射程，从中心+30检查遮挡。 */
    TEST_25D_API bool ResolveBeam(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target, float Range,
                                  EAbilityPrototypeTargetProfile Profile, FVector& SourcePoint, FVector& TargetPoint);
    /** 连锁从上一接触前的位置检查当前固定目标，不因等待期间目标失效而自动重新选目标。 */
    TEST_25D_API bool ResolveChainContact(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                          const FVector& Origin, float Reach, EAbilityPrototypeTargetProfile Profile,
                                          FVector& TargetPoint);
    /** payload 已完成后查询完整候选；保留原生命账本、原地址访问排除与等距最后候选规则。 */
    TEST_25D_API bool SelectChainNext(const FAbilitySourceToken& Source, const FVector& Origin, float Radius,
                                      EAbilityPrototypeTargetProfile Profile,
                                      const TArray<FPureMassCombatTarget>& Visited,
                                      AbilityTargetQueryService::FAbilityQueryLifeResolver Resolver,
                                      FAbilityTargetToken& Target, FVector& TargetPoint, FString& Error);
    /** Actor 按整秒、Mixed 按完整 Delta 计算本帧费用；调用方先扣费再检查总寿命截止。 */
    TEST_25D_API float ManaDue(float& WholeSecondClock, float Delta, float ManaPerSecond,
                               EAbilityPrototypeTargetProfile Profile);
    /** Actor 每帧最多一次并 fmod 保留余数；Mixed 每帧最多八次且保留积压。 */
    TEST_25D_API int32 AdvanceBeamClock(float& Clock, float Delta, float Period, EAbilityPrototypeTargetProfile Profile,
                                        TFunctionRef<bool()> Pulse);
    /** 首次接触在第一次 Advance 即可执行并清时钟；Actor 后续清零，Mixed 八次追赶。 */
    TEST_25D_API int32 AdvanceChainClock(float& Clock, float Delta, float Period, bool bFirstContact,
                                         EAbilityPrototypeTargetProfile Profile, TFunctionRef<bool()> Contact);
} // namespace AbilityPrototypeTargetServices
