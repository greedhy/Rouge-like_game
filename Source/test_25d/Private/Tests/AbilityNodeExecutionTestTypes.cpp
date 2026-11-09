// 调用链：自动化专用注册表 -> 测试策略 Start/Advance/Stop；验证不依赖 TypeId 分派。
#include "AbilityNodeExecutionTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

bool UAbilityNodeExecutionTestStrategy::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    Remaining = CastChecked<UAbilityDelayNodeConfig>(&Config)->DelaySeconds;
    if (auto* Context = Cast<UAbilityNodeExecutionTestContext>(Input.Context.Get()))
    {
        ++Context->Starts;
        if (Context->bImmediateStart)
        {
            Remaining = 0;
            Advance(0.f);
        }
    }
    return true;
}

void UAbilityNodeExecutionTestStrategy::Advance(float DeltaSeconds)
{
    if (bStopped || !GraphOwner.IsValid())
    {
        return;
    }
    Remaining -= DeltaSeconds;
    if (Remaining > 0)
    {
        return;
    }
    auto* Context = CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get());
    if (Context->bStream)
    {
        if (Context->bRequestChildren)
        {
            if (GraphOwner->RequestChild(InvocationId, TEXT("PeriodElapsed"), Input))
            {
                ++Context->AcceptedChildren;
            }
        }
        else
        {
            GraphOwner->Publish(InvocationId, TEXT("PeriodElapsed"), Input);
        }
        return;
    }
    for (const auto& Target : Context->Targets)
    {
        if (!GraphOwner.IsValid())
        {
            return;
        }
        FAbilityNodeInput Hit = Input;
        Hit.LiveTargetToken = Target;
        GraphOwner->Publish(InvocationId, TEXT("TargetFound"), Hit);
    }
    if (GraphOwner.IsValid())
    {
        GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityNodeExecutionTestStrategy::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    if (auto* Context = Cast<UAbilityNodeExecutionTestContext>(Input.Context.Get()))
    {
        ++Context->Stops;
        if (GraphOwner.IsValid() && GraphOwner->Publish(InvocationId, TEXT("PeriodElapsed"), Input))
        {
            ++Context->ReentrantAccepted;
        }
    }
    Super::Stop(Reason);
}
