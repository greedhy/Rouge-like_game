// 调用链：FireBolt原Notify -> 原等级/装备一次准备 -> 不可变发射帧 -> Scatter/真实Await/独立payload。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilityNativeProjectileParameterNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityGraphDefinition;

/** 参数帧不生成弹体、不保存目标；只冻结原发射设置，Scatter与Delivery不得再次读取装备。 */
UCLASS(Transient)
class TEST_25D_API UAbilityNativeProjectileParameterFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    /** 只接受已完成等级/装备解析的一次发射结果；冻结实际class、追踪开关、加速度和数量/散布。 */
    static UAbilityNativeProjectileParameterFrame* Create(UObject* Outer, FGuid Root, const FAbilitySourceToken& Source,
                                                          const FAbilityActorProjectileSettings& Launch, int32 Count,
                                                          FString& Error);
    /** 已冻结的原生飞行/接触合同；本引用不允许后继改写参数帧。 */
    const FAbilityDeliverySettings& GetDeliverySettings() const;
    /** 等级限制和装备追加后的最终数量，后继不得再次按等级裁剪。 */
    int32 GetCount() const;
    /** 包含本次装备最小散布规则的实际角度，不在命中时回读装备。 */
    float GetSpread() const;

private:
    UPROPERTY(Transient)
    FAbilityDeliverySettings Delivery;
    int32 Count = 0;
    float Spread = 0.f;
};

/** 原前端只导出CDO配置；实际Notify插槽/俯仰已经在Input，不从CDO猜测调用实参。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNativeProjectilePrepareConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityActorProjectileSettings Settings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNativeProjectilePrepareExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原min(level)先于装备Extra，冻结一次局部值，不提交第二次费用、不生成任何实际弹体。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityNativeProjectileParameters
{
    /** 准备 -> Scatter -> DeliveryAwait -> ResolveImpact -> InputDamage，不封装另一完整GA。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const FAbilityActorProjectileSettings& Settings);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityNativeProjectileParameters
