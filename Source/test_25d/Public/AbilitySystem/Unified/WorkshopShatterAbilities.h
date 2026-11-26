// 调用链：Ground 目标 -> 狭线查询 -> 状态条件 -> 实际伤害 -> 复核原生命 -> 消耗匹配状态。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopShatterAbilities.generated.h"

class FAbilityNodeRegistry;

/** 指定要消耗的一个元素状态，参数值与 Condition.ElementalStatus 使用同一枚举。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityConsumeExposureNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Exposure")
    EAbilityExposure Expected = EAbilityExposure::Frozen;

    /** 拒绝空状态、未知状态和不能对应枚举的编辑值。 */
    virtual bool Validate(FString& Error) const override;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 只接受带权限的活目标，无独立计时器或跨帧资源。 */
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port, FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityConsumeExposureNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 重验生命和元素槽位，只有实际清除才发布 Consumed。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 以施法者为起点，沿准星方向击裂冻结目标；费用仍由统一前端提交一次。 */
UCLASS()
class TEST_25D_API UWorkshopIceShatter : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    /** 配置所有独立图节点，默认快照在每次试装时由配方编译器深拷贝。 */
    UWorkshopIceShatter();
};

namespace WorkshopShatterAbilities
{
    /** 注册可被其他技能复用的活目标状态消耗节点。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace WorkshopShatterAbilities
