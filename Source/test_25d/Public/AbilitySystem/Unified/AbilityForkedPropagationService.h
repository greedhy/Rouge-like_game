// 调用链：旧ForkedChain同步遍历/新真实投递BFS -> 本共享选敌动作 -> 原查询/可见性/原生命捕获。
#pragma once
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

/** 纯选择参数；不保存队列、世界对象或伤害权限。旧夹取规则由共享动作原样保留。 */
struct TEST_25D_API FAbilityForkedSelectionSettings
{
    int32 MaxTargets = 12;
    int32 MaxDepth = 3;
    int32 BranchesPerTarget = 2;
    float Radius = 400.f;
};

struct TEST_25D_API FAbilityForkedSelection
{
    FAbilityTargetToken Target;
    FVector Position = FVector::ZeroVector;
};

namespace AbilityForkedPropagationService
{
    /** 保留Gather候选次序、半径表达式、LOS和入队预算；两个显式排除器分别支持旧地址/新生命合同。 */
    TEST_25D_API TArray<FAbilityForkedSelection> SelectChildren(
        const FAbilitySourceToken& Source, FVector Center, int32 NextDepth, int32 AlreadyReserved,
        const FAbilityForkedSelectionSettings& Settings,
        TFunctionRef<bool(const FPureMassCombatTarget&)> ExcludeIdentity,
        TFunctionRef<bool(const FAbilityTargetToken&)> ExcludeLife);
} // namespace AbilityForkedPropagationService
