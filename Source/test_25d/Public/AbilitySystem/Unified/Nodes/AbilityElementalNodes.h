// 调用链：范围查询 -> LiveTarget 施加元素；同次查询的 Completed -> Origin 表现。
#pragma once

#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityElementalNodes.generated.h"

class FAbilityNodeRegistry;

/** 无伤害的元素施加，与伤害端复用同一反应表。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityApplyExposureConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Element")
    EAbilityElement Element = EAbilityElement::Water;
    UPROPERTY(EditAnywhere, Category = "Element")
    float DurationScale = 1.f;

    virtual bool Validate(FString& Error) const override;
    /** 元素轴只接受实际能施加的元素，0 不用于无伤害节点。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 缩放本次反应的新状态时长，保留期内冻结的原到期时间。 */
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
    virtual bool SupportsRuntimePurePayload(FName Type, EAbilityPortPayload Port, FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityApplyExposureExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 交给目标生命所有者管理状态，到期不会持有本节点或当前图。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 纯表现使用查询载荷的落点和半径；编辑范围时自然使用同一份结果。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityElementPulseVisualConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Presentation")
    TSoftObjectPtr<UNiagaraSystem> System;
    /** 非圆形来源可以显式提供独立表现尺寸，零保留原 ShapeRadius 合同。 */
    UPROPERTY(EditAnywhere, Category = "Presentation")
    float FallbackRadius = 0.f;
    /** 仅偏移表现点，不改变位置载荷、范围或伤害。 */
    UPROPERTY(EditAnywhere, Category = "Presentation")
    float ForwardOffset = 0.f;

    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityElementPulseVisualExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    /** 发送一次有界表现事件，缺少渲染资产时玩法链仍照常完成。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityElementalNodes
{
    /** 注册不同输入合同和寿命的元素施加、区域表现策略。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityElementalNodes
