// 调用链：参数帧权限自动化 -> 具体测试值类型 -> 生产参数帧根/来源校验；不授予游戏角色。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilityExecutionParameterTestTypes.generated.h"

/** 具体只读值容器，避免测试实例化抽象 UObject/参数帧基类。 */
UCLASS()
class UAbilityExecutionParameterTestValue : public UAbilityExecutionParameters
{
    GENERATED_BODY()
};
