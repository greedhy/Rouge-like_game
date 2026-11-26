// 调用链：元素工作台 Ground 目标 -> 原生默认图 -> 波前或状态筛选 -> 伤害 -> 命中表现。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/WorkshopElementalAbilities.h"
#include "WorkshopPropagationAbilities.generated.h"

class FAbilityNodeRegistry;
class UNiagaraSystem;

/** 复用短时粒子多播；Radius 仅给特效缩放，由伤害结果决定播放点。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UWorkshopElementImpactConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Presentation")
    TSoftObjectPtr<UNiagaraSystem> System;
    UPROPERTY(EditAnywhere, Category = "Presentation")
    float Radius = 100.f;

    virtual bool Validate(FString& Error) const override;
    /** 修改独立的表现尺寸，范围/命中数由玩法节点自己消费。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 只读取已结算的历史伤害和位置，目标可已死亡，不授予活目标操作权限。 */
UCLASS(Transient)
class TEST_25D_API UWorkshopElementImpactExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

/** 扫过落点附近的固定波前，命中按每次施放记录原生命。 */
UCLASS()
class TEST_25D_API UWorkshopFrostSurge : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopFrostSurge();
};

/** 以落点为中心选择有指定状态的活目标，每个目标单独结算一次雷伤。 */
UCLASS()
class TEST_25D_API UWorkshopWetArc : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopWetArc();
};

namespace WorkshopPropagationAbilities
{
    /** 注册跨技能共用的历史伤害表现节点。 */
    TEST_25D_API void RegisterNodes(FAbilityNodeRegistry& Registry);
} // namespace WorkshopPropagationAbilities
