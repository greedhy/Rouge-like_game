// 调用链：工作台 T 键 -> 自施放与服务器 Commit -> 有限节拍/净化图 -> ASC 属性或 Avatar 护盾。
#pragma once

#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopRejuvenationAbilities.generated.h"

class FAbilityNodeRegistry;

/** 恢复施法者实际缺少的生命或法力，单次额度和每次施法的限制相互独立。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityRestoreSelfNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Restoration")
    float Amount = 9.f;
    UPROPERTY(EditAnywhere, Category = "Restoration")
    bool bMana = false;
    UPROPERTY(EditAnywhere, Category = "Restoration")
    bool bOncePerCast = false;

    /** 编译期拒绝非有限数值和超过单节点上限的恢复量。 */
    virtual bool Validate(FString& Error) const override;
    /** 资源量可独立改装，资源类型和逐次施法限制由可信配方保留。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityRestoreSelfNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 用源身份执行恢复，达到资源上限时正常结束；只有实际恢复才发布 Restored。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 精确匹配当前施法者的元素状态，空槽不能成为净化条件。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityClearSelfExposureNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Element")
    EAbilityExposure Expected = EAbilityExposure::Chilled;

    /** 仅接受现有非空元素状态。 */
    virtual bool Validate(FString& Error) const override;
    /** 类型化参数必须精确匹配已定义的状态枚举。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityClearSelfExposureNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 清理源生命的匹配状态；不匹配时 Completed 照常驱动后续屏障。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 自身治疗，有限节拍归本次 GA 图所有。 */
UCLASS()
class TEST_25D_API UWorkshopTidalMend : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    /** 按默认次数和间隔组合节拍、恢复及治疗表现。 */
    UWorkshopTidalMend();
};

/** 精确净化及授予短时护盾；已授予的护盾由当前 Avatar 清理。 */
UCLASS()
class TEST_25D_API UWorkshopFrostAegis : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    /** 净化完成后独立授予护盾，即使施法时没有匹配状态。 */
    UWorkshopFrostAegis();
};

namespace WorkshopRejuvenationAbilities
{
    /** 将同步自身恢复、净化及成功/终结端口加入统一注册表。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace WorkshopRejuvenationAbilities
