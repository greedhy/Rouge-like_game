// 调用链：注册表 -> Delay/Petrify 执行对象 -> 图事件队列/已有石化状态服务。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityBasicNodeExecutions.generated.h"

UCLASS(Transient)
class TEST_25D_API UAbilityDelayNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;

private:
    double Remaining = 0;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPetrifyNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};
