// 调用链：地表节点选择原生地块类 → ATerrainSurface 构建外观 → 服务端租约驱动真实接触。
#pragma once

#include "Terrain/TerrainSurface.h"
#include "WorkshopElementalSurfaces.generated.h"

/** 由服务器生成并复制的水地表，从同一个原生类模板恢复各端的配置。 */
UCLASS(NotBlueprintable)
class TEST_25D_API AWorkshopWaterTerrainSurface : public ATerrainSurface
{
    GENERATED_BODY()

public:
    AWorkshopWaterTerrainSurface();
    /** 原地表缩放完成后抬高可视平面，接触检测仍以原地面为准。 */
    virtual void OnConstruction(const FTransform& Transform) override;
};

/** 沿用水地几何的带电地表。玩法元素由类上稳定的地表枚举决定。 */
UCLASS(NotBlueprintable)
class TEST_25D_API AWorkshopConductiveTerrainSurface : public AWorkshopWaterTerrainSurface
{
    GENERATED_BODY()

public:
    AWorkshopConductiveTerrainSurface();
};

/** 云的 Actor 仍以地面原位置复制，表现抬高且没有可踩踏网格。 */
UCLASS(NotBlueprintable)
class TEST_25D_API AWorkshopMistCloudSurface : public ATerrainSurface
{
    GENERATED_BODY()

public:
    /** 提供两端共用的原生云模板，接触由服务器租约与原地表扫描调度。 */
    AWorkshopMistCloudSurface();
    /** 尺寸复制后仍按云层中心重建位置与缩放。 */
    virtual void OnConstruction(const FTransform& Transform) override;
};

/** 使用相同云体积，改变元素及环境 Niagara。 */
UCLASS(NotBlueprintable)
class TEST_25D_API AWorkshopConductiveCloudSurface : public AWorkshopMistCloudSurface
{
    GENERATED_BODY()

public:
    AWorkshopConductiveCloudSurface();
};
