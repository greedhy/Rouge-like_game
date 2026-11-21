// 调用链：Effect.PrototypeDamageBatch.Contact -> 可替换原生命状态/接触表现 -> Completed 返回 receipt。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeContactNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "GameFramework/Actor.h"

bool UAbilityPrototypeSlowNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    const auto* Field = Frame ? Cast<UBatch6FieldPrototype>(&Frame->GetParameters()) : nullptr;
    if (!Field || !Input.Impact.IsSet())
    {
        Error = TEXT("Prototype Slow requires its own Batch6 frame and an actual historical contact.");
        return false;
    }
    FVector Current;
    if (Field->SlowMultiplier < 1.f &&
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.Impact->ConfirmedTarget, Current))
    {
        UBatch6SlowComponent::Apply(Input.SourceToken.World.Get(), Input.SourceToken.Avatar.Get(),
                                    Input.Impact->ConfirmedTarget.Target, Field->SlowMultiplier, Field->SlowDuration);
    }
    if (bStopped || Owner.IsFinished())
    {
        return true;
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilityPrototypeImpactNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    if (!Frame || !Input.Impact.IsSet())
    {
        return false;
    }
    if (auto* Host = Cast<AActor>(Owner.FindRuntimeResource(*Frame, TEXT("PrototypePresentation"))))
    {
        const uint64 Sequence = Input.Impact->Sequence ? Input.Impact->Sequence : Owner.GetAcceptedEventCount() + 1;
        if (Sequence > MAX_uint32)
        {
            Owner.Cancel(EAbilityGraphEndReason::BudgetExceeded);
            return true;
        }
        AbilityPrototypePresentationService::Impact(Host, static_cast<uint32>(Sequence), Input.Impact->Location);
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityPrototypeContactNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const bool bSlow : {true, false})
    {
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = bSlow ? TEXT("Status.PrototypeSlow") : TEXT("Presentation.PrototypeImpact");
        Descriptor.ConfigClass =
            bSlow ? UAbilityPrototypeSlowNodeConfig::StaticClass() : UAbilityPrototypeImpactNodeConfig::StaticClass();
        Descriptor.ExecutionClass = bSlow ? UAbilityPrototypeSlowNodeExecution::StaticClass()
                                          : UAbilityPrototypeImpactNodeExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::DamageResult;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bPrototypeCompatibilitySafe = true;
        // 每次Impact调用只发一次表现消息，受新片段调用/事件上限约束；不创建逻辑孩子。
        // Niagara组件AutoDestroy并以准确表现host为Outer，终结/cancel后host的.35秒寿命兜底回收。
        Descriptor.bRuntimeFiniteSafe = !bSlow;
        Descriptor.Ports = {{TEXT("Completed"), EAbilityPortPayload::DamageResult, true}};
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype contact registration: %s"), *Error);
        }
    }
}
