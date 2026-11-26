// 调用链：实际 T 键 -> 地面 Commit -> 状态精确消耗 -> 原生局部爆发或邻近传播。
#pragma once

#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopExposureComboAbilities.generated.h"

/** 消耗灼热后在原目标当前位置释放一次局部爆发。 */
UCLASS()
class TEST_25D_API UWorkshopEmberRupture : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    /** 组合精确消耗、位置适配及既有区域伤害。 */
    UWorkshopEmberRupture();
};

/** 消耗导电后从锁定位置向邻近敌人发送一次有限雷伤脉冲。 */
UCLASS()
class TEST_25D_API UWorkshopConductiveRelay : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    /** 组合精确消耗、有限引信及排除锚点的传播查询。 */
    UWorkshopConductiveRelay();
};
