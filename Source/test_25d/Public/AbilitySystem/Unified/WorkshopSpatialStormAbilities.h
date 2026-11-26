// 调用链：统一施法图冻结配置 -> 带电目标转落点或落点分布 -> 范围查询 -> 伤害与表现。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopSpatialStormAbilities.generated.h"

class FAbilityNodeRegistry;

/** 定义首点及后续点的生成节奏与最大分布半径。范围伤害由下游另行配置。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityStaggeredPointsNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    /** 编译时校验数量和有限时空范围。 */
    virtual bool Validate(FString& Error) const override;
    /** 按比例调整落点分布半径。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    /** 工作台可分别设置整数点数、分布半径和发射间隔。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;

    UPROPERTY(EditAnywhere, Category = "Staggered Points", meta = (ClampMin = "1", ClampMax = "8"))
    int32 Count = 4;

    UPROPERTY(EditAnywhere, Category = "Staggered Points", meta = (ClampMin = "0.01", ClampMax = "1000"))
    float Radius = 220.f;

    UPROPERTY(EditAnywhere, Category = "Staggered Points", meta = (ClampMin = "0.1", ClampMax = "2"))
    float Period = .18f;
};

/** 图实例独占时钟和输入位置。首点在原点，后续点按固定次序分布在水平圆内。 */
UCLASS(Transient)
class TEST_25D_API UAbilityStaggeredPointsExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 保存当前执行的参数并发布首点。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 随权威图推进，补发本帧到期的点；取消后由基类切断图引用。 */
    virtual void Advance(float DeltaSeconds) override;

private:
    /** 发布一个独立位置，最后一点之后结束调用。 */
    bool EmitPoint(UAbilityGraphInstance& Owner);

    int32 Count = 0;
    int32 Emitted = 0;
    float Radius = 0.f;
    float Period = 0.f;
    double Elapsed = 0.0;
};

/** 选出带电敌人并锁定各自的位置，延迟后在这些落点爆裂。 */
UCLASS()
class TEST_25D_API UWorkshopThunderMarkBurst : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    /** 装配选取、锁定、等待和局部雷击的默认图。 */
    UWorkshopThunderMarkBurst();
};

/** 从范围落点依次降下多个可独立调节的冰晶攻击。 */
UCLASS()
class TEST_25D_API UWorkshopFrostShower : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    /** 装配分布、局部查询、冰伤害和表现的默认图。 */
    UWorkshopFrostShower();
};

namespace WorkshopSpatialStormAbilities
{
    /** 登记有限落点流及其位置类型端口。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace WorkshopSpatialStormAbilities
