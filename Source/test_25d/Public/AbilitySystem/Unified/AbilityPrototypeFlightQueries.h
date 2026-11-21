// 调用链：原 Actor/Mixed 弹道分段与细图投递 -> 共享原碰撞查询 -> 原生命/墙体接触快照。
// 只提供碰撞事实；准入、伤害、状态、下一步和表现由调用方分别管理。
#pragma once
#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"

/** Candidate 是原始碰撞身份；Life 只可保留原生命，缺失或过期不允许效果重新捕获新生命。 */
struct TEST_25D_API FAbilityPrototypeFlightQueryHit
{
    FHitResult Hit;
    FPureMassCombatTarget Candidate;
    TOptional<FAbilityTargetToken> Life;
};

namespace AbilityPrototypeFlightQueries
{
    /** 原 Actor 多对象扫掠：先忽略友方/失效角色和已访身份，输出排序原始Hit；后置分类须逐步执行。 */
    TEST_25D_API bool GatherActorSegment(const FAbilitySourceToken& Source, const FVector& Start, const FVector& End,
                                         float Radius, const TSet<FPureMassCombatTarget>& Ignored,
                                         AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                                         TArray<FAbilityPrototypeFlightQueryHit>& Out, FString& Error,
                                         AActor* IgnoredEffect = nullptr);
    /** 每个Hit使用当下原生命/组件response分类；先前GE可以开门/换租约，不能提前冻结全部分类。 */
    TEST_25D_API bool ClassifyActorContact(const FAbilitySourceToken& Source, const FHitResult& Hit,
                                           AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                                           FAbilityPrototypeFlightQueryHit& Out, AActor* IgnoredEffect = nullptr);
    /** 原 Mixed 单次重查询：最近敌方必须严格早于墙；保留原始碰撞候选，即使根账本拒绝其新租约。 */
    TEST_25D_API bool SweepMixedSegment(const FAbilitySourceToken& Source, const FVector& Start, const FVector& End,
                                        float Radius, const TSet<FPureMassCombatTarget>& Ignored,
                                        AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                                        FAbilityPrototypeFlightQueryHit& Out, FHitResult& OutWall, FString& Error,
                                        AActor* IgnoredEffect = nullptr);
} // namespace AbilityPrototypeFlightQueries
