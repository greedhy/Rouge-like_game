// 调用链：编辑器 Python/蓝图审计脚本 -> DescribeBlueprint -> 只读 EdGraph/FProperty 快照 -> JSON 报告。
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilityBlueprintAuditLibrary.generated.h"

class UBlueprint;

/** Editor inspection only: never compiles, reparents, modifies or saves the inspected Blueprint. */
UCLASS()
class TEST_25D_API UAbilityBlueprintAuditLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Export stable graph/node/pin identities, topology and reflected member references; non-editor builds report unsupported. */
    UFUNCTION(BlueprintCallable, Category = "UnifiedGA|Audit")
    static FString DescribeBlueprint(UBlueprint* Blueprint);
};
