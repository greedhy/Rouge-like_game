// 调用链：GraphDefinition静态校验 -> 原图预算、入口、主寿命与端口证明；不启动运行资源。
#pragma once
#include "CoreMinimal.h"
class UAbilityGraphDefinition;
class FAbilityNodeRegistry;
namespace AbilityOriginalProjectionValidation
{
    TEST_25D_API void Validate(const UAbilityGraphDefinition& Graph, const FAbilityNodeRegistry& Registry,
                               TArray<FString>& Errors);
}
