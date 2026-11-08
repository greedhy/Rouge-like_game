// 混合目标入口：控制器和技能运行时共用身份解析、范围收集与扫掠；角色走敌对接口，实体走子系统，墙体优先阻断命中。
#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

/** A cast-local reference. Mass IDs and weak Actor identities never share a namespace.
 * Network requests use SingleTargetHit: Actor + INDEX_NONE, or no Actor + Mass ID.
 */
struct TEST_25D_API FPureMassCombatTarget
{
    int32 EntityId = INDEX_NONE;
    TWeakObjectPtr<AActor> Actor;

    FPureMassCombatTarget() = default;
    FPureMassCombatTarget(int32 InEntityId) : EntityId(InEntityId)
    {
    }
    // 构造角色弱引用目标并保留实体标识为空。
    static FPureMassCombatTarget FromActor(AActor* InActor)
    {
        FPureMassCombatTarget Result;
        Result.Actor = InActor;
        return Result;
    }
    // 区分角色目标与实体目标，失效弱引用仍属于角色身份。
    bool IsActor() const
    {
        return !Actor.IsExplicitlyNull();
    }
    // 判断目标是否携带角色身份或正数实体标识。
    bool IsSet() const
    {
        return IsActor() || EntityId > 0;
    }
    bool operator==(const FPureMassCombatTarget& Other) const
    {
        return EntityId == Other.EntityId && Actor == Other.Actor;
    }
    bool operator!=(const FPureMassCombatTarget& Other) const
    {
        return !(*this == Other);
    }
    // 组合两种身份的哈希以支持命中去重集合。
    friend uint32 GetTypeHash(const FPureMassCombatTarget& Target)
    {
        return HashCombine(::GetTypeHash(Target.EntityId), ::GetTypeHash(Target.Actor));
    }
};

/** Shared filtering, geometry and lookup for player abilities against both enemy representations. */
namespace PureMassCombat
{
    // 校验目标身份与存活状态并返回当前位置或快照，失败时清空输出。
    TEST_25D_API bool Resolve(UWorld* World, AActor* Caster, const FPureMassCombatTarget& Target, FVector& OutLocation);
    // 合并实体与角色候选，角色需通过敌对、存活及遭遇规则校验。
    TEST_25D_API TArray<FPureMassCombatTarget> Gather(UWorld* World, AActor* Caster, FVector Origin, float Radius);
    // 通过统一遮挡查询判断目标是否可见。
    TEST_25D_API bool Visible(UWorld* World, AActor* Caster, FVector Origin, const FPureMassCombatTarget& Target,
                              AActor* IgnoredEffect = nullptr);
    // 检查地面目标可见性，允许射线命中目标附近地面。
    TEST_25D_API bool GroundVisible(UWorld* World, AActor* Caster, FVector Origin, FVector Point);
    /** Returns the earliest hostile hit only when strictly before world obstruction. Allies are transparent. */
    // 沿线段检测目标胶囊，选择最早有效命中并返回命中时间。
    TEST_25D_API FPureMassCombatTarget Sweep(UWorld* World, AActor* Caster, FVector Start, FVector End, float Radius,
                                             float& OutTime, FHitResult& OutWall,
                                             const TSet<FPureMassCombatTarget>* Ignored = nullptr,
                                             AActor* IgnoredEffect = nullptr);
} // namespace PureMassCombat
