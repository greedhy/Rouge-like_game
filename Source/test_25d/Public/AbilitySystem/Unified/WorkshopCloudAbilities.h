// 调用链：工作台 Ground 选点 → GAS 原费用 → 地云转化或驱散 → 在世界中独立结束的租约。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopCloudAbilities.generated.h"

/** 最近的自有潮地升为会接触离地目标的雾云。 */
UCLASS()
class TEST_25D_API UWorkshopMistLift : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopMistLift();
};

/** 将自有雾云恢复为地面的潮地，继承尺寸和剩余时间。 */
UCLASS()
class TEST_25D_API UWorkshopMistCondense : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopMistCondense();
};

/** 给自有雾云通电，后续接触走项目雷元素的共享反应。 */
UCLASS()
class TEST_25D_API UWorkshopStormMist : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopStormMist();
};

/** 清除云层上的自有临时租约，保留同位置的地面租约。 */
UCLASS()
class TEST_25D_API UWorkshopMistDispel : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopMistDispel();
};
