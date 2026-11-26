// 调用链：工作台临时授予 -> 地面 TargetData -> 统一费用和冷却 -> World.ConvertSurface -> 地表租约。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Terrain/TerrainSurfaceConfig.h"
#include "WorkshopSurfaceConversionAbilities.generated.h"

class FAbilityNodeRegistry;

/** 在选点附近转换一块当前施法者持有的指定地表。两种地表独立配置。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityConvertSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Terrain")
    float Radius = 360.f;

    UPROPERTY(EditAnywhere, Category = "Terrain")
    ETerrainSurfaceType InputKind = ETerrainSurfaceType::Lava;

    UPROPERTY(EditAnywhere, Category = "Terrain")
    ETerrainSurfaceType ResultKind = ETerrainSurfaceType::Frost;

    /** 新图显式允许云参与。旧地面图和已保存的默认值保持只转换地层。 */
    UPROPERTY(EditAnywhere, Category = "Terrain")
    bool bAllowCloud = false;

    /** 校验最终草稿的范围和不同的来源/产物种类。 */
    virtual bool Validate(FString& Error) const override;
    /** 设置单个字段。草稿校验在补丁应用后检查输入和输出的组合。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 仅搜索半径可以按比例改装，原地块尺寸及剩余寿命随租约继承。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

/** 在权威端调用同一地表事务，成功才提供真实转换位置。 */
UCLASS(Transient)
class TEST_25D_API UAbilityConvertSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 处理有效施法来源，依次发布可选的 Transformed 及终止端口 Completed。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 默认将最近的己方灼热地表转换为霜地。 */
UCLASS()
class TEST_25D_API UWorkshopLavaFreeze : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopLavaFreeze();
};

/** 默认将最近的己方霜地转换为毒床。 */
UCLASS()
class TEST_25D_API UWorkshopFrostCorrode : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopFrostCorrode();
};

/** 把最近的己方潮地转换成霜地，保留原租约。 */
UCLASS()
class TEST_25D_API UWorkshopWaterFreeze : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopWaterFreeze();
};

/** 把最近的己方潮地转换成通电地表，由地表继续结算接触。 */
UCLASS()
class TEST_25D_API UWorkshopWaterElectrify : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopWaterElectrify();
};

namespace AbilityWorkshopSurfaceConversion
{
    /** 向统一注册表登记有限 Origin 动作及其端口合同。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace AbilityWorkshopSurfaceConversion
