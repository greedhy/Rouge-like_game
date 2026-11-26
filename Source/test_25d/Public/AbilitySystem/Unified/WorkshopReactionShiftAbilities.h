// 调用链：第四页试装 → ASC 的地面施法 → 主图筛选并消耗状态 → 施加元素或恢复施法者。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopReactionShiftAbilities.generated.h"

/** 将范围内敌人的蒸汽转为潮湿，命中本身没有伤害。 */
UCLASS()
class TEST_25D_API UWorkshopSteamCondensation : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopSteamCondensation();
};

/** 消耗范围内敌人的导电，每次施法至多恢复一次法力。 */
UCLASS()
class TEST_25D_API UWorkshopConductiveRecharge : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopConductiveRecharge();
};
