// 调用链：节点Start -> 时间服务Acquire -> Advance只观察请求结束 -> Stop幂等Release。
#include "AbilitySystem/Unified/Nodes/AbilityWorldTimeNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "Engine/World.h"

bool UAbilityWorldSlowNodeConfig::Validate(FString& Error) const
{
    return Settings.Validate(Error);
}

bool UAbilityWorldSlowNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                           const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                           FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto* World = InInput.SourceToken.World.Get();
    Time = World ? World->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr;
    if (!Time.IsValid())
    {
        Error = TEXT("World time service unavailable.");
        return false;
    }
    Lease =
        Time->Acquire(InInput.SourceToken, CastChecked<UAbilityWorldSlowNodeConfig>(&Config)->Settings, this, Error);
    return Lease.IsValid();
}

void UAbilityWorldSlowNodeExecution::Advance(float DeltaSeconds)
{
    auto* Graph = GraphOwner.Get();
    if (bStopped || !Graph || Graph->IsFinished())
    {
        return;
    }
    if (!Time.IsValid() || !Time->IsActive(Lease))
    {
        if (Time.IsValid() && Time->HasExpired(Lease))
        {
            Graph->Publish(InvocationId, TEXT("Completed"), Input);
        }
        else
        {
            Graph->Cancel(EAbilityGraphEndReason::Cancelled);
        }
    }
}

void UAbilityWorldSlowNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    if (Time.IsValid())
    {
        Time->Release(Lease);
    }
    Lease.Invalidate();
    Time.Reset();
}

void AbilityWorldTimeNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Node;
    Node.TypeId = TEXT("Time.WorldSlow");
    Node.ConfigClass = UAbilityWorldSlowNodeConfig::StaticClass();
    Node.ExecutionClass = UAbilityWorldSlowNodeExecution::StaticClass();
    Node.InputPayload = EAbilityPortPayload::Origin;
    Node.bSupportsActorTargets = Node.bSupportsSnapshotTargets = true;
    Node.bIsAsync = true;
    Node.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Node.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Node, Error), TEXT("World time registration: %s"), *Error);
    }
}
