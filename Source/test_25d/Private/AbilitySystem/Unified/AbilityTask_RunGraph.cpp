// 调用链：GA ReadyForActivation -> Activate -> Instance Start；Task Tick -> Advance -> GraphFinished。
// GA 结束触发 OnDestroy；先解绑防重入，再取消尚未结束的图，目标持久石化不归本任务撤销。
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"

UAbilityTask_RunGraph::UAbilityTask_RunGraph()
{
    bTickingTask = true;
}

UAbilityTask_RunGraph* UAbilityTask_RunGraph::RunAbilityGraph(UGameplayAbility* Owner, UAbilityGraphDefinition* Graph,
                                                              FAbilitySourceToken Source, FAbilityTargetToken Target)
{
    FAbilityNodeInput Input;
    Input.SourceToken = Source;
    Input.LiveTargetToken = Target;
    AbilityExecutionIdentity::ResolveTarget(Source, Target, Input.Origin);
    return RunAbilityGraphWithInput(Owner, Graph, Input, FAbilityExecutionOptions());
}

UAbilityTask_RunGraph* UAbilityTask_RunGraph::RunAbilityGraphWithInput(UGameplayAbility* Owner,
                                                                       UAbilityGraphDefinition* Graph,
                                                                       const FAbilityNodeInput& Input,
                                                                       const FAbilityExecutionOptions& Options,
                                                                       bool bMeasureElapsedWorldTime)
{
    auto* Task = NewAbilityTask<UAbilityTask_RunGraph>(Owner);
    Task->Definition = Graph;
    Task->NodeInput = Input;
    Task->ExecutionOptions = Options;
    if (const auto* Frontend = Cast<UMyGameplayAbility>(Owner))
    {
        Task->ExecutionOptions.OwnerLease = Frontend->GetRuntimeExecutionOwnerLease();
    }
    Task->bMeasureElapsedWorldTime = bMeasureElapsedWorldTime;
    Task->RetainedContext = Input.Context.Get();
    Task->RetainedDamageParams = Input.DamageParams;
    return Task;
}

void UAbilityTask_RunGraph::Activate()
{
    if (bClosing || Instance)
    {
        return;
    }
    if (!Ability || !Ability->GetCurrentActorInfo() || !Ability->GetCurrentActorInfo()->IsNetAuthority() ||
        ExecutionOptions.Lifetime != EAbilityExecutionLifetime::BoundToAbility)
    {
        GraphFinished(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    if (bMeasureElapsedWorldTime && GetWorld())
    {
        PreviousWorldTime = GetWorld()->GetTimeSeconds();
    }
    Instance = NewObject<UAbilityGraphInstance>(this);
    Instance->OnFinished.AddUObject(this, &ThisClass::GraphFinished);
    const TStrongObjectPtr<UAbilityGraphInstance> Starting(Instance.Get());
    const auto StartingSource = NodeInput.SourceToken;
    auto* Frontend = Cast<UMyGameplayAbility>(Ability);
    if (Frontend)
    {
        Frontend->ObserveFrozenRuntimeGraph(Starting.Get(), StartingSource);
    }
    FString Error;
    if (!Starting->StartWithInput(Definition, NodeInput, ExecutionOptions, Error))
    {
        if (Frontend)
        {
            Frontend->ForgetFrozenRuntimeGraph(Starting.Get(), StartingSource);
        }
        GraphFinished(EAbilityGraphEndReason::Failed);
    }
}

void UAbilityTask_RunGraph::TickTask(float DeltaTime)
{
    Super::TickTask(DeltaTime);
    if (!bClosing && Instance)
    {
        if (PreviousWorldTime.IsSet() && GetWorld())
        {
            // ASC 可能在施法当帧 Tick，整帧 Delta 含有施法前的时间。
            const double WorldTime = GetWorld()->GetTimeSeconds();
            DeltaTime = static_cast<float>(FMath::Max(0., WorldTime - PreviousWorldTime.GetValue()));
            PreviousWorldTime = WorldTime;
        }
        Instance->Advance(DeltaTime);
    }
}

void UAbilityTask_RunGraph::GraphFinished(EAbilityGraphEndReason Reason)
{
    if (bClosing)
    {
        return;
    }
    bClosing = true;
    if (ShouldBroadcastAbilityTaskDelegates())
    {
        OnFinished.Broadcast(Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired);
    }
    EndTask();
}

void UAbilityTask_RunGraph::ExternalCancel()
{
    if (Instance)
    {
        Instance->Cancel();
    }
    else
    {
        GraphFinished(EAbilityGraphEndReason::Cancelled);
    }
    Super::ExternalCancel();
}

void UAbilityTask_RunGraph::OnDestroy(bool bAbilityEnded)
{
    bClosing = true;
    if (Instance)
    {
        Instance->OnFinished.RemoveAll(this);
        Instance->Cancel();
    }
    Instance = nullptr;
    Definition = nullptr;
    RetainedContext = nullptr;
    RetainedDamageParams = FDamageEffectParams();
    NodeInput = FAbilityNodeInput();
    Super::OnDestroy(bAbilityEnded);
}
