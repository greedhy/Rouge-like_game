// 调用链：实际批结果 -> 纯表现节点 -> 已注册帧/宿主与原共享Niagara renderer；没有隐式damage。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePresentationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "GameFramework/Actor.h"

bool UAbilityPrototypePulseNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    if (!Frame || Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1"))
    {
        Error = TEXT("Prototype pulse presentation requires this root's Actor parameter frame");
        return false;
    }
    using K = EAbilityPrototypeKind;
    const auto Kind = Frame->GetParameters().Kind;
    if (Kind != K::Cone && Kind != K::Ring && Kind != K::Line && Kind != K::Field && Kind != K::Trap)
    {
        Error = TEXT("Prototype pulse does not duplicate Burst startup or other contact presentations");
        return false;
    }
    if (auto* Host = Cast<AActor>(Owner.FindRuntimeResource(*Frame, TEXT("PrototypePresentation"))))
    {
        const uint64 Sequence = Owner.GetAcceptedEventCount() + 1;
        if (Sequence > MAX_uint32)
        {
            Owner.Cancel(EAbilityGraphEndReason::BudgetExceeded);
            return true;
        }
        // 原Pulse在所有GE之后读取host当前位置；ground投影与作者scale仍由共享renderer冻结值决定。
        AbilityPrototypePresentationService::Impact(Host, static_cast<uint32>(Sequence), Host->GetActorLocation());
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilityPrototypeOrbitImpactNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                      const UAbilityNodeConfig& Config,
                                                      const FAbilityNodeInput& InInput, FString& Error)
{
    const auto* Ticket = Cast<UAbilityPrototypeOrbitLaunchFrame>(InInput.ParameterFrame.Get());
    const auto* Frame = Ticket ? Ticket->GetParameters() : nullptr;
    if (!Ticket || !Ticket->BelongsTo(Owner.GetExecutionId(), InInput.SourceToken) || !Frame ||
        !Frame->BelongsTo(Owner.GetExecutionId(), InInput.SourceToken) ||
        Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1") ||
        Frame->GetParameters().Kind != EAbilityPrototypeKind::Orbit)
    {
        Error = TEXT("Orbit impact requires this root's exact Mixed launch ticket and its original prototype frame");
        return false;
    }
    auto Payload = InInput;
    Payload.ParameterFrame = const_cast<UAbilityPrototypeParameterFrame*>(Frame);
    // 同一个执行对象/Invocation调用父类共享动作；不新建执行器，也不嵌套激活任何能力。
    return Super::Start(Owner, Id, Config, Payload, Error);
}

void AbilityPrototypePresentationNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Pulse;
    Pulse.TypeId = TEXT("Presentation.PrototypePulse");
    Pulse.ConfigClass = UAbilityPrototypePulseNodeConfig::StaticClass();
    Pulse.ExecutionClass = UAbilityPrototypePulseNodeExecution::StaticClass();
    Pulse.InputPayload = EAbilityPortPayload::Origin;
    Pulse.bSupportsActorTargets = Pulse.bSupportsMassTargets = Pulse.bSupportsSnapshotTargets = true;
    Pulse.bPrototypeCompatibilitySafe = true;
    Pulse.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto Orbit = Pulse;
    Orbit.TypeId = TEXT("Presentation.PrototypeOrbitImpact");
    Orbit.ConfigClass = UAbilityPrototypeImpactNodeConfig::StaticClass();
    Orbit.ExecutionClass = UAbilityPrototypeOrbitImpactNodeExecution::StaticClass();
    Orbit.InputPayload = EAbilityPortPayload::DamageResult;
    Orbit.Ports = {{TEXT("Completed"), EAbilityPortPayload::DamageResult, true}};
    for (const auto& Descriptor : {Pulse, Orbit})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype presentation registration: %s"),
                             *Error);
        }
    }
}
