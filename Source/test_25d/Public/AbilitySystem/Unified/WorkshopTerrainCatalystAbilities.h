// 调用链：真实地面施法 → 有限节拍 → 当前地表租约查询 → 现有范围伤害或状态。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "Terrain/TerrainSurfaceConfig.h"
#include "WorkshopTerrainCatalystAbilities.generated.h"

class FAbilityNodeRegistry;

/** 在本次位置附近选择同层、同所有者且未到期的最近指定地表。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityFindOwnedSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Radius = 420.f;

    UPROPERTY(EditAnywhere, Category = "Terrain")
    ETerrainSurfaceType Kind = ETerrainSurfaceType::Conductive;

    /** 使用和转化相同的有限搜索范围及明确的地表类型。 */
    virtual bool Validate(FString& Error) const override;
    /** 独立编辑搜索半径与输入地表类别。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

/** 只读取共享地表租约，成功时发布实际位置和半尺寸。 */
UCLASS(Transient)
class TEST_25D_API UAbilityFindOwnedSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 不持有地表引用。后继若延时，则只持有本轮位置值。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 短时借用已有通电地表的位置释放雷击。 */
UCLASS()
class TEST_25D_API UWorkshopStormCatalyst : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopStormCatalyst();
};

/** 短时借用已有霜地的位置施加冻僵与独立减速。 */
UCLASS()
class TEST_25D_API UWorkshopFrostCatalyst : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopFrostCatalyst();
};

namespace AbilityWorkshopTerrainCatalyst
{
    /** 登记服务端有限图的地表位置查询端口。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace AbilityWorkshopTerrainCatalyst
