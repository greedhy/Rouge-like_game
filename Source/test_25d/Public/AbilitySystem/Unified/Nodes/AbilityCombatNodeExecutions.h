// 调用链：Schema2 查询 -> TargetFound 活目标端口 -> Damage 节点 -> 原 Actor/Mass 伤害服务。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"
#include "AbilityCombatNodeExecutions.generated.h"

class FAbilityNodeRegistry;

/** 明确的权限转换：历史快照只允许重新验证原生命，不重新捕获相同 Actor 的新租约。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityResolveImpactNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 伤害结果分支可同时要求正生命伤害，再把历史目标换成当前原生命。 */
    UPROPERTY(EditAnywhere, Category = "Damage Result")
    bool bRequirePositiveDamage = false;

    virtual bool MatchesType(FName TypeId) const override;
    /** 两种准确事实输入只重验原生命；同调度器同步转换，不重捕目标、不建宿主或时钟。 */
    virtual bool SupportsRuntimePurePayload(FName RegisteredType, EAbilityPortPayload Incoming,
                                            FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityResolveImpactNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityQueryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityQueryRequest Query;
    /** 输入位置/方向来自前一节点；资产保留几何尺寸与目标过滤。 */
    virtual bool Validate(FString& Error) const override;
    /** 缩放实际使用 Radius 的形状，直线的距离和宽度用各自的类型化参数。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    /** 为当前几何暴露真实尺寸。通用查询可以在保持端口不变的前提下替换形状。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;

private:
    /** 批查询继承自身已登记的校验，不降级成普通查询的目标数上限。 */
    bool ApplyQueryEdit(const FAbilityQueryRequest& Edited, FString& Error);
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDamageNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Damage = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamagePerLevel = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag DamageType;
    /** 原生默认图可以保存元素而不在 CDO 构造时查询标签。 */
    UPROPERTY(EditAnywhere, Category = "Damage")
    EAbilityElement ElementOverride = EAbilityElement::Original;
    FGameplayTag ResolveDamageType() const;
    virtual bool Validate(FString& Error) const override;
    /** 缩放两段真正参与伤害计算的数值，编辑器和配方使用同一个轴。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    /** 在配方副本写入共享元素映射；实际伤害仍由原 Actor/Mass 入口结算。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityQueryNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityDamageNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityCombatNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
