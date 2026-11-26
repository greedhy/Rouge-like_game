// 调用链：第四页试装 -> Ground 输入确认 -> 风压图 -> 原世界位移服务与独立表现。
#pragma once

#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopGaleAbilities.generated.h"

/** 根据鼠标方向推开扇区内带指定元素状态的敌人。 */
UCLASS()
class TEST_25D_API UWorkshopGaleSweep : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopGaleSweep();
};
