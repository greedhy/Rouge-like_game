// 调用链：GraphInstance 从注册类构造 -> Start/Advance -> Publish/FinishInvocation -> Stop 释放本次资源。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "AbilityNodeExecution.generated.h"

class UAbilityGraphInstance;

/** 每次调用独占一个策略对象；Stop 后弱图引用失效，异步回调不能继续发布。 */
UCLASS(Abstract, Transient)
class TEST_25D_API UAbilityNodeExecution : public UObject
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& InInvocationId, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error);
    virtual void Advance(float DeltaSeconds);
    virtual void Stop(EAbilityGraphEndReason Reason);

protected:
    TWeakObjectPtr<UAbilityGraphInstance> GraphOwner;
    FGuid InvocationId;
    FAbilityNodeInput Input;
    bool bStopped = false;

private:
    UPROPERTY(Transient)
    FDamageEffectParams RetainedDamageParams;
    UPROPERTY(Transient)
    TObjectPtr<UObject> RetainedContext;
};
