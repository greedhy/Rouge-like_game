// 调用链：Automation 临时注册策略 -> 独立调用时钟 -> 多目标事件/重入 Stop 观测。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityNodeExecutionTestTypes.generated.h"

UCLASS(Transient)
class UAbilityNodeExecutionTestContext : public UObject
{
    GENERATED_BODY()
public:
    TArray<FAbilityTargetToken> Targets;
    int32 Starts = 0;
    int32 Stops = 0;
    int32 ReentrantAccepted = 0;
    bool bStream = false;
    bool bImmediateStart = false;
    bool bRequestChildren = false;
    int32 AcceptedChildren = 0;
};

/** 只供原生自动化注册，不属于生产内建策略。 */
UCLASS(Transient)
class UAbilityNodeExecutionTestStrategy : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    double Remaining = 0;
};
