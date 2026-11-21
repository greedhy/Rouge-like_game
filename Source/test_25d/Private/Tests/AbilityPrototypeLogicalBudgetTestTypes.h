// 调用链：自动化本地注册 -> 测试发布器 -> 同一Graph的真实Scope，不进入产品注册表。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilityPrototypeLogicalBudgetTestTypes.generated.h"

UCLASS(Transient)
class UAbilityPrototypeLogicalBudgetTestContext : public UPrototypeCastContext
{
    GENERATED_BODY()
public:
    int32 AcceptedRequests = 0;
};

UCLASS(EditInlineNew)
class UAbilityPrototypeLogicalBudgetTestConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY()
    bool bRequestChildren = true;
};

UCLASS(Transient)
class UAbilityPrototypeLogicalBudgetTestPublisher : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只提供RequestChild与普通Publish两种入口；实际准入/Scope计数由生产执行器完成。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
