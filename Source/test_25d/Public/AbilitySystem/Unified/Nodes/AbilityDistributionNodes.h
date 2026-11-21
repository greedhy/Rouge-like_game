// 调用链：运行时配方 -> Scatter -> 多个Origin方向载荷 -> 独立投递；本节点没有生成/伤害权限。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityDistributionNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityScatterNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 原生准备节点已完成等级/装备一次解析时，从只读ParameterFrame取实际数量和角度。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bUseNativeLaunchParameters = false;
    /** 原生帧已应用等级/装备；选中后只替换最终数量，不重复解析装备。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridePreparedCount = false;
    /** 保留Notify方向/俯仰，只替换冻结帧的实际散布。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridePreparedSpread = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Count = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Spread = 90.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bLimitCountToLevel = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridePitch = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PitchOverride = 0.f;
    /** 拒绝无界数量/非有限角度；输入等级和方向由本次执行另行校验。 */
    virtual bool Validate(FString& Error) const override;
    /** 仅Count/Spread有真实消费点；其余轴失败，不改原生帧或共享资产。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityScatterNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只改变方向并逐个发布Launch；实际弹体由后继Delivery创建和累计预算。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};
namespace AbilityDistributionNodes
{
    /** 复用原等距角序；Count限制先于Spread，Pitch覆盖先于方向分布。 */
    TEST_25D_API TArray<FVector> BuildDirections(FVector Direction, float Level,
                                                 const UAbilityScatterNodeConfig& Config);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityDistributionNodes
