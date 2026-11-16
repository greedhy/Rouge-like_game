// 调用链：图节点 -> 防御组件共享动作；节点取消只关闭本次动作，奖励由Pawn生命持有。
#include "AbilitySystem/Unified/Nodes/AbilityDefenseNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "GameFramework/Character.h"

bool UAbilityPerfectDodgeNodeConfig::Validate(FString& Error) const
{
    return Settings.Validate(Error);
}

bool UAbilityPerfectDodgeNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Defense = UPerfectDodgeComponent::FindOrCreate(Cast<ACharacter>(Input.SourceToken.Avatar.Get()));
    if (!Defense)
    {
        Error = TEXT("Perfect dodge requires an authoritative Character source.");
        return false;
    }
    const auto* Settings = CastChecked<UAbilityPerfectDodgeNodeConfig>(&Config);
    Execution = Defense->Reserve(Input.SourceToken, this, Settings->Settings, Input.Direction, Error);
    if (!Execution.IsValid() || !Defense->Begin(Execution))
    {
        Defense->Finish(Execution, true);
        return false;
    }
    return true;
}

void UAbilityPerfectDodgeNodeExecution::Advance(float)
{
    auto* Graph = GraphOwner.Get();
    if (bStopped || !Graph || Graph->IsFinished() || !Defense)
    {
        return;
    }
    auto Output = Input;
    if (const auto* Avatar = Input.SourceToken.Avatar.Get())
    {
        Output.Origin = Avatar->GetActorLocation();
    }
    if (!Defense->IsRunning(Execution) && Defense->WasCancelled(Execution))
    {
        Graph->Cancel(EAbilityGraphEndReason::Cancelled);
        return;
    }
    if (!bPerfectPublished && Defense->WasPerfect(Execution))
    {
        bPerfectPublished = true;
        if (!Graph->Publish(InvocationId, TEXT("PerfectDodged"), Output))
        {
            return;
        }
    }
    if (!Defense->IsRunning(Execution))
    {
        if (Defense->WasCancelled(Execution))
        {
            Graph->Cancel(EAbilityGraphEndReason::Cancelled);
        }
        else
        {
            Graph->Publish(InvocationId, TEXT("Completed"), Output);
        }
    }
}

void UAbilityPerfectDodgeNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    if (Defense)
    {
        Defense->Finish(Execution, Reason != EAbilityGraphEndReason::Completed);
    }
    Execution.Invalidate();
    Defense = nullptr;
}

void AbilityDefenseNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Defense.PerfectDodge");
    Descriptor.ConfigClass = UAbilityPerfectDodgeNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPerfectDodgeNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    Descriptor.Ports = {{TEXT("PerfectDodged"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Defense registration: %s"), *Error);
    }
}
