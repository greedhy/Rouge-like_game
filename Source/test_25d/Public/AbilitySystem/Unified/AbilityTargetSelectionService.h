// 调用链：传播或Query.SelectTargets → 共享候选筛选/排序 → 原Actor/Mass生命令牌；不造成伤害。
#pragma once
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilityTargetSelectionService.generated.h"

/** Original保留具体宿主原默认；显式策略可在同一来源内更换，不改变敌对/房间/存活过滤。 */
UENUM(BlueprintType)
enum class EAbilityTargetSelectionPolicy : uint8
{
    Original,
    Nearest,
    Farthest,
    LowestHealthRatio
};

struct FAbilitySelectedCandidate
{
    FAbilityTargetToken Target;
    FVector Position = FVector::ZeroVector;
};

namespace AbilityTargetSelectionService
{
    /** 服务器同步选择至多64个目标；候选超过4096则失败，不能用截断前缀冒充最低血/最远。 */
    TEST_25D_API bool Select(const FAbilitySourceToken& Source, FVector Center, float Radius, int32 MaxTargets,
                             EAbilityTargetSelectionPolicy Policy,
                             TFunctionRef<bool(const FAbilityTargetToken&)> Exclude,
                             TArray<FAbilitySelectedCandidate>& Out, FString& Error);
} // namespace AbilityTargetSelectionService
