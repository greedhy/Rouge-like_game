// 调用链：孤立展开的显式接口 -> 无副作用桥接 -> 原图后继，仍使用同一注册表/执行器。
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"

FName AbilityNormalizationNodes::OriginalPortType(EAbilityPortPayload Payload)
{
    switch (Payload)
    {
    case EAbilityPortPayload::Origin:
        return TEXT("Event.OriginalOrigin");
    case EAbilityPortPayload::LiveTarget:
        return TEXT("Event.OriginalLive");
    case EAbilityPortPayload::ConfirmedImpact:
        return TEXT("Event.OriginalImpact");
    case EAbilityPortPayload::DamageResult:
        return TEXT("Event.OriginalDamage");
    case EAbilityPortPayload::TargetSet:
        return TEXT("Event.OriginalTargets");
    default:
        return NAME_None;
    }
}

bool UAbilityOriginalPortNodeConfig::Validate(FString& Error) const
{
    Error = AbilityNormalizationNodes::OriginalPortType(Payload).IsNone() ? TEXT("Unregistered original port payload.")
                                                                          : FString();
    return Error.IsEmpty();
}

bool UAbilityOriginalPortNodeConfig::MatchesType(FName TypeId) const
{
    return TypeId == AbilityNormalizationNodes::OriginalPortType(Payload);
}

bool UAbilityOriginalPortNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    if (CastChecked<UAbilityOriginalPortNodeConfig>(&Config)->bRequestChild)
    {
        if (!Owner.HasSuccessor(Id, TEXT("Forward")) || !Owner.RequestChild(Id, TEXT("Forward"), Input))
        {
            Owner.FinishInvocation(Id);
        }
        return true;
    }
    return Owner.Publish(Id, TEXT("Forward"), Input);
}

bool UAbilityImpactFactNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    return Super::Start(Owner, Id, Config, InInput, Error) && Owner.Publish(Id, TEXT("Completed"), InInput);
}

bool UAbilityOrderedCompletionNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                   const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                   FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Owner.Publish(Id, TEXT("Expired"), InInput))
    {
        return false;
    }
    return Owner.Publish(Id, TEXT("Completed"), InInput);
}

bool UAbilityPrototypeCompletedInputNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                         const UAbilityNodeConfig& Config,
                                                         const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    FAbilityNodeInput Output;
    return Frame && Frame->RestoreEntryInput(Input, Output, Error) && Owner.Publish(Id, TEXT("Completed"), Output);
}

void AbilityNormalizationNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (auto Payload :
         {EAbilityPortPayload::Origin, EAbilityPortPayload::LiveTarget, EAbilityPortPayload::ConfirmedImpact,
          EAbilityPortPayload::DamageResult, EAbilityPortPayload::TargetSet})
    {
        FAbilityNodeDescriptor Original;
        Original.TypeId = OriginalPortType(Payload);
        Original.ConfigClass = UAbilityOriginalPortNodeConfig::StaticClass();
        Original.ExecutionClass = UAbilityOriginalPortNodeExecution::StaticClass();
        Original.InputPayload = Payload;
        Original.bSupportsActorTargets = Original.bSupportsMassTargets = Original.bSupportsSnapshotTargets = true;
        Original.bPrototypeCompatibilitySafe = true;
        Original.bOriginalProjectionEndpoint = true;
        Original.bRuntimeFiniteSafe = true;
        Original.Ports = {{TEXT("Forward"), Payload, true}};
        if (!Registry.Find(Original.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Original, Error), TEXT("Original endpoint registration: %s"), *Error);
        }
    }
    FAbilityNodeDescriptor Impact;
    Impact.TypeId = TEXT("Event.ImpactFact");
    Impact.ConfigClass = UAbilityImpactFactNodeConfig::StaticClass();
    Impact.ExecutionClass = UAbilityImpactFactNodeExecution::StaticClass();
    Impact.InputPayload = EAbilityPortPayload::DamageResult;
    Impact.bSupportsActorTargets = Impact.bSupportsMassTargets = Impact.bSupportsSnapshotTargets = true;
    Impact.bPrototypeCompatibilitySafe = true;
    // 只复制原生命快照/发布一次输出，不创建Actor、时钟或独立执行器。
    Impact.bRuntimeFiniteSafe = true;
    Impact.Ports = {{TEXT("Completed"), EAbilityPortPayload::ConfirmedImpact, true}};
    FAbilityNodeDescriptor Complete = Impact;
    Complete.TypeId = TEXT("Event.OrderedCompletion");
    Complete.bRuntimeFiniteSafe = true;
    Complete.ConfigClass = UAbilityOrderedCompletionNodeConfig::StaticClass();
    Complete.ExecutionClass = UAbilityOrderedCompletionNodeExecution::StaticClass();
    Complete.InputPayload = EAbilityPortPayload::Origin;
    Complete.Ports = {{TEXT("Expired"), EAbilityPortPayload::Origin, false},
                      {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto OriginalInput = Complete;
    OriginalInput.TypeId = TEXT("Event.PrototypeCompletedInput");
    OriginalInput.ConfigClass = UAbilityPrototypeCompletedInputNodeConfig::StaticClass();
    OriginalInput.ExecutionClass = UAbilityPrototypeCompletedInputNodeExecution::StaticClass();
    OriginalInput.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    OriginalInput.bOriginalProjectionEndpoint = true;
    OriginalInput.bRuntimeFiniteSafe = true;
    for (const auto& Item : {Impact, Complete, OriginalInput})
    {
        if (!Registry.Find(Item.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Item, Error), TEXT("Normalization bridge: %s"), *Error);
        }
    }
}
