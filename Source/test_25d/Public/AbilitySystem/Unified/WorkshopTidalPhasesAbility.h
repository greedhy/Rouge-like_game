// 调用链：第四页试装 → 地面施法 → 圆形选敌 → 每个活目标的状态分流。
#pragma once

#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopTidalPhasesAbility.generated.h"

/** 同一次选敌中，状态匹配和未匹配各走独立的元素伤害及表现。 */
UCLASS()
class TEST_25D_API UWorkshopTidalPhases : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopTidalPhases();
};
