// 调用链：GraphInstance -> 每调用策略初始化；终止先断开图回调，再释放保活引用。
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

bool UAbilityNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& InInvocationId,
                                  const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput, FString& Error)
{
    GraphOwner = &Owner;
    InvocationId = InInvocationId;
    Input = InInput;
    RetainedDamageParams = Input.DamageParams;
    RetainedContext = Input.Context.Get();
    bStopped = false;
    Error.Reset();
    return true;
}

void UAbilityNodeExecution::Advance(float DeltaSeconds)
{
}

void UAbilityNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    GraphOwner.Reset();
    RetainedDamageParams = FDamageEffectParams();
    RetainedContext = nullptr;
}
