// 调用链：真实Hit -> ResolveImpact原生命复核 -> InputDamage -> 共享接触参数/伤害服务。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilityInputDamageNode.generated.h"
class FAbilityNodeRegistry;

/** 输入已经冻结基础数值和累计倍率；本配置不提供覆盖固定Damage的第二来源。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityInputDamageNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 局部倍率允许0以关闭主伤；唯一owner与累计输入倍率合并一次，DOT仍使用独立输入倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    /** 修改本节点实际结算的元素；原输入、费用和其它分支继续使用各自的冻结配置。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityElement ElementOverride = EAbilityElement::Original;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bCaptureLocationAfterDamage = false;
};
UCLASS(Transient)
class TEST_25D_API UAbilityInputDamageNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只有实际伤害owner准备一次forces与缩放副本；发布时仍保留原参数/累计倍率。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};
namespace AbilityInputDamageNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
