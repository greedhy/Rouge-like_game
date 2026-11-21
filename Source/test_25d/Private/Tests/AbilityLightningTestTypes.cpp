// 调用链：局部测试策略 -> 类型化TargetSet转发 -> 原生产效果；这里不施加伤害、状态或表现。
#include "AbilityLightningTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

bool UAbilityLightningSubsetTestExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.TargetSet || Input.TargetSet->Targets.Num() != 2)
    {
        Error = TEXT("Subset fixture requires two real original query targets.");
        return false;
    }
    auto Selected = MakeShared<FAbilityTargetSetSnapshot>(*Input.TargetSet);
    Selected->Targets = {Input.TargetSet->Targets[1]};
    const int32 ExtraId = CastChecked<UAbilityLightningSubsetTestConfig>(&Config)->InjectedEntityId;
    if (ExtraId >= 0)
    {
        FAbilityTargetToken Extra;
        if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget(ExtraId), Extra, Error))
        {
            return false;
        }
        Selected->Targets.Add(Extra);
    }
    auto Payload = Input;
    Payload.TargetSet = Selected;
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}
