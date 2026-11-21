// 调用链：本地测试发布器 ->70次类型化Pulse -> 生产Scope；没有模拟预算或自制执行器。
#include "AbilityPrototypeLogicalBudgetTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

bool UAbilityPrototypeLogicalBudgetTestPublisher::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                        const UAbilityNodeConfig& Config,
                                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto* Context = CastChecked<UAbilityPrototypeLogicalBudgetTestContext>(Input.Context.Get());
    const bool bRequest = CastChecked<UAbilityPrototypeLogicalBudgetTestConfig>(&Config)->bRequestChildren;
    for (int32 Index = 0; Index < 70 && !Owner.IsFinished(); ++Index)
    {
        if (bRequest ? Owner.RequestChild(Id, TEXT("Pulse"), Input) : Owner.Publish(Id, TEXT("Pulse"), Input))
        {
            ++Context->AcceptedRequests;
        }
    }
    return Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}
