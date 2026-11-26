// 调用链：第四页试装 → 地面输入及 GAS Commit → 默认 Schema 2 图 → 已注册的地表节点。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopWaterShallowsAbility.generated.h"

/** 放置可转换的临时水地，占用常规地表租约之一。 */
UCLASS()
class TEST_25D_API UWorkshopWaterShallows : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopWaterShallows();
};
