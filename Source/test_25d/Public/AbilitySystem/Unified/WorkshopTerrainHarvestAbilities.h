// 调用链：第四页地面施法 -> World.DispelSurface.Removed -> 自身护盾或有限恢复。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopTerrainHarvestAbilities.generated.h"

/** 消耗最近的同层自有临时霜地，为当前施法者授予护盾。 */
UCLASS()
class TEST_25D_API UWorkshopFrostHarvest : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    /** 只在地表实际回收后连接护盾节点。 */
    UWorkshopFrostHarvest();
};

/** 消耗最近的同层自有临时毒地，按有限节拍恢复当前施法者的生命。 */
UCLASS()
class TEST_25D_API UWorkshopBogHarvest : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    /** 独立配置地表选择、恢复节拍与单次恢复量。 */
    UWorkshopBogHarvest();
};
