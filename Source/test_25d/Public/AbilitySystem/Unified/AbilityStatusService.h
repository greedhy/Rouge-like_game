// 调用链：旧范围石化 GA / 统一图石化节点 -> ApplyPetrify -> 原 Actor 石化组件或 Mass 状态；调用者拥有 Commit 与目标查询。
#pragma once

#include "CoreMinimal.h"
#include "PureMass/PureMassCombatTarget.h"

class UMaterialInterface;

namespace AbilityStatusService
{
    /** 仅服务器对有效敌对活目标施加石化；失败不创建控制状态，不负责费用、范围查询或表现特效。 */
    TEST_25D_API bool ApplyPetrify(AActor* Source, const FPureMassCombatTarget& Target, float Duration,
                                   UMaterialInterface* Material);
} // namespace AbilityStatusService
