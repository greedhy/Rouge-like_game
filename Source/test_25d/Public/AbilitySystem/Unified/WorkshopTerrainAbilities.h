// 调用链：工作台授予 -> 地面 TargetData -> 一次 Commit -> 原生地表节点。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopTerrainAbilities.generated.h"

/** 创建当前施法者持有的临时毒床。 */
UCLASS()
class TEST_25D_API UWorkshopVenomMire : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopVenomMire();
};

/** 以选定位置寻找最近的己方毒床，将剩余地表寿命转换为火地表。 */
UCLASS()
class TEST_25D_API UWorkshopMireIgnition : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopMireIgnition();
};
