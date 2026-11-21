// 调用链：Scope Ready 的历史/活输入 -> Target.PrototypeLive -> 同一生命的共享状态服务。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityExtensionFlowNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"

bool UAbilityPrototypeLiveNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    if (!Frame)
    {
        return false;
    }
    auto Payload = Input;
    const FAbilityTargetToken* Token = Input.LiveTargetToken.IsSet() ? &Input.LiveTargetToken.GetValue()
                                       : Input.Impact.IsSet()        ? &Input.Impact->ConfirmedTarget
                                                                     : nullptr;
    FVector Current;
    if (Token && AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *Token, Current))
    {
        // 保留请求原点；单目标位移需要原中心，不在权限转换时误改为目标位置。
        Payload.LiveTargetToken = *Token;
        return Owner.Publish(Id, TEXT("TargetFound"), Payload);
    }
    const auto* Extension = Cast<UExtensionNode>(&Frame->GetParameters());
    if (Extension && Extension->Behavior == EExtensionKind::Stack && Input.Impact.IsSet() &&
        (Input.bConfirmedHit || Input.bHasDamageResult))
    {
        Payload.LiveTargetToken.Reset();
        return Owner.Publish(Id, TEXT("Confirmed"), Payload);
    }
    Owner.FinishInvocation(Id, EAbilityGraphEndReason::TargetLost);
    return true;
}

void AbilityPrototypeExtensionNodes::Register(FAbilityNodeRegistry& Registry)
{
    AbilityExtensionFlowNodes::Register(Registry);
    auto Add = [&](const FAbilityNodeDescriptor& Descriptor)
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype extension registration: %s"),
                             *Error);
        }
    };
    FAbilityNodeDescriptor Resolve;
    Resolve.TypeId = TEXT("Target.PrototypeLive");
    Resolve.ConfigClass = UAbilityPrototypeLiveNodeConfig::StaticClass();
    Resolve.ExecutionClass = UAbilityPrototypeLiveNodeExecution::StaticClass();
    Resolve.InputPayload = EAbilityPortPayload::Origin;
    Resolve.bSupportsActorTargets = Resolve.bSupportsMassTargets = Resolve.bSupportsSnapshotTargets = true;
    Resolve.bPrototypeCompatibilitySafe = true;
    Resolve.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, true},
                     {TEXT("Confirmed"), EAbilityPortPayload::ConfirmedImpact, true}};
    Add(Resolve);
    FAbilityNodeDescriptor Attach = Resolve;
    Attach.TypeId = TEXT("State.PrototypeAttach");
    Attach.ConfigClass = UAbilityPrototypeAttachNodeConfig::StaticClass();
    Attach.ExecutionClass = UAbilityAttachNodeExecution::StaticClass();
    Attach.InputPayload = EAbilityPortPayload::LiveTarget;
    Attach.bOwnsTargetTracking = Attach.bIsAsync = true;
    Attach.Ports = {{TEXT("Period"), EAbilityPortPayload::LiveTarget, false},
                    {TEXT("Transferred"), EAbilityPortPayload::LiveTarget, false},
                    {TEXT("TargetLost"), EAbilityPortPayload::Origin, true},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Attach);
    FAbilityNodeDescriptor Stack = Resolve;
    Stack.TypeId = TEXT("State.PrototypeStack");
    Stack.ConfigClass = UAbilityPrototypeStackNodeConfig::StaticClass();
    Stack.ExecutionClass = UAbilityStackNodeExecution::StaticClass();
    Stack.InputPayload = EAbilityPortPayload::LiveTarget;
    Stack.bIsAsync = true;
    Stack.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Stack.Ports = {{TEXT("Threshold"), EAbilityPortPayload::Origin, false},
                   {TEXT("Released"), EAbilityPortPayload::Origin, true}};
    Add(Stack);
    Stack.TypeId = TEXT("State.PrototypeStackImpact");
    Stack.InputPayload = EAbilityPortPayload::ConfirmedImpact;
    Add(Stack);
    FAbilityNodeDescriptor Move = Attach;
    Move.TypeId = TEXT("Movement.PrototypeTargetDisplace");
    Move.ConfigClass = UAbilityPrototypeDisplaceNodeConfig::StaticClass();
    Move.ExecutionClass = UAbilityTargetDisplaceNodeExecution::StaticClass();
    Move.bOwnsTargetTracking = true;
    Move.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true},
                  {TEXT("Replaced"), EAbilityPortPayload::Origin, true},
                  {TEXT("TargetLost"), EAbilityPortPayload::Origin, true}};
    Move.Ports.Add({TEXT("Rejected"), EAbilityPortPayload::Origin, true});
    Add(Move);
    Move.TypeId = TEXT("Movement.PrototypeAreaDisplace");
    Move.ConfigClass = UAbilityPrototypeAreaDisplaceNodeConfig::StaticClass();
    Move.ExecutionClass = UAbilityPrototypeAreaDisplaceNodeExecution::StaticClass();
    Move.InputPayload = EAbilityPortPayload::Origin;
    Move.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Move);
}
