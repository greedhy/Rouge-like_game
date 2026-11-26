// 调用链：工作台 Ground 选点 -> World.DispelSurface -> 原地表租约 -> Removed 位置与独立表现。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Terrain/TerrainSurfaceConfig.h"
#include "WorkshopDispelAbilities.generated.h"

class FAbilityNodeRegistry;

/** 有界的地表移除范围、数量及可选类别，只作用于来源当前生命创造的地块。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDispelSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Radius = 360.f;
    UPROPERTY(EditAnywhere, Category = "Terrain")
    int32 MaxTargets = 2;
    /** 驱散从一个确切层采样，同一 XY 的另一层不在候选中。 */
    UPROPERTY(EditAnywhere, Category = "Terrain")
    bool bCloudLayer = false;
    UPROPERTY(EditAnywhere, Category = "Terrain")
    bool bFilterByKind = false;
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (EditCondition = "bFilterByKind"))
    ETerrainSurfaceType Kind = ETerrainSurfaceType::Toxic;

    /** 拒绝无界范围、未知类型和超过可持有地块数的上限。 */
    virtual bool Validate(FString& Error) const override;
    /** 绝对半径、数量、层和类型走统一参数轴；设置具体类型会启动类别筛选。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 缩放半径沿用绝对半径的验证。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityDispelSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 复核每块租约，成功关闭接触后才发布实际位置；无匹配则只结束图。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 工作台默认驱散。同一位置施法入口和原有 ASC 费用通路均继续使用。 */
UCLASS()
class TEST_25D_API UWorkshopTerrainDispel : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopTerrainDispel();
};

namespace AbilityWorkshopDispel
{
    /** 将有限位置策略加入统一注册表。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace AbilityWorkshopDispel
