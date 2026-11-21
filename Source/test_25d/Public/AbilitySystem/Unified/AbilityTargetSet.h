// 调用链：同根查询捕获 -> 不可变批目标快照 -> 独立效果策略逐生命验证；数组不复制到客户端。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilityTargetSet.generated.h"

UENUM(BlueprintType)
enum class EAbilityQueryLifePolicy : uint8
{
    FreshQuery,
    OriginalRootLife
};

struct FAbilitySelectionReceipt;

/** 单次查询的只读集合；只有弱生命令牌，不持有目标或授予跨世界权限。允许空集合。 */
struct TEST_25D_API FAbilityTargetSetSnapshot
{
    FAbilitySourceToken Source;
    TArray<FAbilityTargetToken> Targets;
    /** 可选的不可变空间接触凭据；普通查询为空，继续保留仅复核原生命的既有合同。 */
    TSharedPtr<const FAbilitySelectionReceipt> SelectionReceipt;
};
