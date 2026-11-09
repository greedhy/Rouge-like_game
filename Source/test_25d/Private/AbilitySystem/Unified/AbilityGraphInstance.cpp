// 调用链：入口启动快照 -> 注册策略 Start/Advance -> Publish/RequestChild -> DrainEvents -> 后继策略。
// 关闭先封事件门，再 Stop 全部策略；回调中的 Cancel/Finish 不会使容器引用失效后被再次使用。
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** 事件不能借用另一施法者的有效令牌更换本次执行来源。 */
    bool SameSource(const FAbilitySourceToken& A, const FAbilitySourceToken& B)
    {
        return A.World == B.World && A.Avatar == B.Avatar && A.ASC == B.ASC && A.DungeonRunId == B.DungeonRunId &&
               A.EnemyLease == B.EnemyLease;
    }
} // namespace

bool UAbilityGraphInstance::Start(UAbilityGraphDefinition* Graph, const FAbilitySourceToken& Source,
                                  const FAbilityTargetToken& Target, FString& Error)
{
    FAbilityNodeInput Input;
    Input.SourceToken = Source;
    Input.LiveTargetToken = Target;
    AbilityExecutionIdentity::ResolveTarget(Source, Target, Input.Origin);
    return StartWithInput(Graph, Input, FAbilityExecutionOptions(), Error);
}

bool UAbilityGraphInstance::StartWithInput(UAbilityGraphDefinition* Graph, const FAbilityNodeInput& Input,
                                           const FAbilityExecutionOptions& InOptions, FString& Error)
{
    FAbilityNodeRegistry Builtins;
    Builtins.RegisterBuiltins();
    return StartWithInput(Graph, Input, InOptions, Builtins, Error);
}

bool UAbilityGraphInstance::StartWithInput(UAbilityGraphDefinition* Graph, const FAbilityNodeInput& Input,
                                           const FAbilityExecutionOptions& InOptions,
                                           const FAbilityNodeRegistry& InRegistry, FString& Error)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    Error.Reset();
    if (bStarted || !IsValid(Graph))
    {
        Error = TEXT("Graph is missing or instance has already started.");
        return false;
    }
    TArray<FString> Errors;
    if (!Graph->Validate(InRegistry, Errors))
    {
        Error = FString::Join(Errors, TEXT("; "));
        return false;
    }
    Options = InOptions;
    if (Options.CompatibilityProfile.IsNone())
    {
        Options.CompatibilityProfile = Graph->CompatibilityProfile;
    }
    if (Options.CompatibilityProfile != Graph->CompatibilityProfile || Options.BudgetPolicy != Graph->BudgetPolicy ||
        (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming &&
         (Graph->SchemaVersion != 2 || !FAbilityNodeRegistry::IsLegacyProfile(Options.CompatibilityProfile))))
    {
        Error = TEXT("Execution profile must match graph; streaming requires an explicit Schema2 legacy profile.");
        return false;
    }
    const auto* Entry = Graph->Nodes.FindByPredicate(
        [Graph](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Graph->EntryNodeId;
        });
    RootInput = Input;
    const auto* Descriptor = InRegistry.Find(Entry->TypeId);
    if (!ValidateInput(Input, Graph->SchemaVersion == 1 ? EAbilityPortPayload::LiveTarget : Descriptor->InputPayload,
                       Error))
    {
        return false;
    }
    // 调用者的注册表和配置都可在启动后消失；本次执行保留独立快照及类引用。
    Registry = InRegistry;
    Snapshot = DuplicateObject<UAbilityGraphDefinition>(Graph, this);
    for (const auto& Node : Snapshot->Nodes)
    {
        const auto* Registered = Registry.Find(Node.TypeId);
        RegisteredClasses.AddUnique(Registered->ExecutionClass.Get());
        RegisteredClasses.AddUnique(Registered->ConfigClass.Get());
    }
    ExecutionId = FGuid::NewGuid();
    bStarted = true;
    bFinished = false;
    const bool bSchema2 = Snapshot->SchemaVersion == 2;
    InvokeNode(Snapshot->EntryNodeId, 1, Input);
    if (bSchema2 && !bFinished)
    {
        // 同步根事件在启动帧迭代推进，仍遵守队列单帧上限，不恢复旧递归执行栈。
        TGuardValue<bool> Guard(bAdvancing, true);
        DrainEvents();
    }
    return true;
}

bool UAbilityGraphInstance::ValidateInput(const FAbilityNodeInput& Input, EAbilityPortPayload Required,
                                          FString& Error) const
{
    Error.Reset();
    AActor* Source = nullptr;
    if (!SameSource(Input.SourceToken, RootInput.SourceToken) ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source) || Input.Origin.ContainsNaN() ||
        Input.Direction.ContainsNaN() || Input.EventCenter.ContainsNaN() || !FMath::IsFinite(Input.Level))
    {
        Error = TEXT("Input source identity or geometry is invalid.");
        return false;
    }
    if (Required == EAbilityPortPayload::LiveTarget)
    {
        FVector Location;
        if (!Input.LiveTargetToken.IsSet() ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location))
        {
            Error = TEXT("Input requires a current live target token.");
            return false;
        }
    }
    if ((Required == EAbilityPortPayload::ConfirmedImpact || Required == EAbilityPortPayload::DamageResult) &&
        (!Input.Impact.IsSet() || Input.Impact->Location.ContainsNaN() || Input.Impact->Direction.ContainsNaN()))
    {
        Error = TEXT("Input requires a finite confirmed impact snapshot.");
        return false;
    }
    if (Required == EAbilityPortPayload::DamageResult && !Input.bHasDamageResult)
    {
        Error = TEXT("Input requires an explicit damage result.");
        return false;
    }
    return true;
}

bool UAbilityGraphInstance::ResolveSource()
{
    AActor* Source = nullptr;
    if (bFinished)
    {
        return false;
    }
    if (!AbilityExecutionIdentity::ResolveSource(RootInput.SourceToken, Source))
    {
        Finish(EAbilityGraphEndReason::SourceInvalid);
        return false;
    }
    if (Snapshot->SchemaVersion == 1)
    {
        FVector Location;
        if (!RootInput.LiveTargetToken.IsSet() ||
            !AbilityExecutionIdentity::ResolveTarget(RootInput.SourceToken, RootInput.LiveTargetToken.GetValue(),
                                                     Location))
        {
            Finish(EAbilityGraphEndReason::TargetLost);
            return false;
        }
    }
    return true;
}

const FAbilityNodeDescriptor* UAbilityGraphInstance::DescriptorFor(const FInvocation& Invocation) const
{
    if (!Snapshot)
    {
        return nullptr;
    }
    const auto* Node = Snapshot->Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Item)
        {
            return Item.NodeId == Invocation.NodeId;
        });
    return Node ? Registry.Find(Node->TypeId) : nullptr;
}

bool UAbilityGraphInstance::InvokeNode(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input, bool bReserved)
{
    if (!ResolveSource())
    {
        return false;
    }
    if (bReserved)
    {
        --ReservedInvocations;
        --ReservedActive;
    }
    if (Invocations.Num() + ReservedInvocations >= Snapshot->Budget.MaxInvocations ||
        ActiveCount + ReservedActive >= Snapshot->Budget.MaxActiveNodes || Depth > Snapshot->Budget.MaxDepth)
    {
        Finish(EAbilityGraphEndReason::BudgetExceeded);
        return false;
    }
    const auto* Node = Snapshot->Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Item)
        {
            return Item.NodeId == NodeId;
        });
    const auto* Descriptor = Node ? Registry.Find(Node->TypeId) : nullptr;
    if (!Descriptor)
    {
        Finish(EAbilityGraphEndReason::Failed);
        return false;
    }
    FString Error;
    if (!ValidateInput(Input, Descriptor->InputPayload, Error))
    {
        if (Snapshot->SchemaVersion == 1)
        {
            Finish(EAbilityGraphEndReason::TargetLost);
            return false;
        }
        // V2 某个旧生命失效只拒绝这一调用，不撤销其它位置型/独立目标分支。
        return true;
    }
    const FGuid Id = FGuid::NewGuid();
    FInvocation Invocation;
    Invocation.NodeId = NodeId;
    Invocation.Depth = Depth;
    Invocation.Input = Input;
    Invocations.Add(Id, MoveTemp(Invocation));
    ++ActiveCount;
    UAbilityNodeExecution* Execution = NewObject<UAbilityNodeExecution>(this, Descriptor->ExecutionClass);
    const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
    Executions.Add(Id, Execution);
    // Start 可同步发布或取消，绝不能跨越回调保存 TMap 的元素引用。
    const UAbilityNodeConfig* Config = Node->Config;
    const TStrongObjectPtr<UAbilityGraphDefinition> SnapshotGuard(Snapshot.Get());
    if (!Execution->Start(*this, Id, *Config, Input, Error) && !bFinished)
    {
        Finish(EAbilityGraphEndReason::Failed);
        return false;
    }
    return !bFinished;
}

bool UAbilityGraphInstance::Publish(const FGuid& Id, FName Port, const FAbilityNodeInput& Payload,
                                    EAbilityGraphEndReason Reason)
{
    const auto* Invocation = Invocations.Find(Id);
    if (!Invocation || bFinished || Invocation->bTerminal)
    {
        return false;
    }
    FAbilityGraphEvent Event;
    Event.ExecutionId = ExecutionId;
    Event.NodeInvocationId = Id;
    Event.EventSequence = Invocation->LastSequence + 1;
    Event.OutputPort = Port;
    Event.Input = Payload;
    Event.EndReason = Reason;
    return EnqueueEvent(Event);
}

bool UAbilityGraphInstance::RequestChild(const FGuid& Id, FName Port, const FAbilityNodeInput& Payload)
{
    const auto* Invocation = Invocations.Find(Id);
    if (!Invocation || bFinished || Invocation->bTerminal || Snapshot->SchemaVersion != 2)
    {
        return false;
    }
    FAbilityGraphEvent Event;
    Event.ExecutionId = ExecutionId;
    Event.NodeInvocationId = Id;
    Event.EventSequence = Invocation->LastSequence + 1;
    Event.OutputPort = Port;
    Event.Input = Payload;
    Event.bChildrenReserved = true;
    return EnqueueEvent(Event);
}

bool UAbilityGraphInstance::EnqueueEvent(const FAbilityGraphEvent& Incoming)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    if (bFinished || Incoming.ExecutionId != ExecutionId || !ResolveSource())
    {
        return false;
    }
    auto* Invocation = Invocations.Find(Incoming.NodeInvocationId);
    if (!Invocation || Invocation->bTerminal || Incoming.EventSequence <= Invocation->LastSequence)
    {
        return false;
    }
    if (Snapshot->SchemaVersion == 1 && (Incoming.EventSequence != 1 || Incoming.OutputPort != TEXT("Completed") ||
                                         Incoming.EndReason != EAbilityGraphEndReason::Completed))
    {
        return false;
    }
    const auto* Descriptor = DescriptorFor(*Invocation);
    const auto* Port = Descriptor->Ports.FindByPredicate(
        [&](const FAbilityPortDescriptor& Item)
        {
            return Item.Name == Incoming.OutputPort;
        });
    if (!Port || Incoming.OutputPort == TEXT("Cancelled") || Incoming.OutputPort == TEXT("Failed"))
    {
        return false;
    }
    FString Error;
    if (!ValidateInput(Incoming.Input, Port->Payload, Error))
    {
        return false;
    }
    int32 ChildCount = 0;
    if (Incoming.bChildrenReserved)
    {
        for (const auto& Edge : Snapshot->Edges)
        {
            if (Edge.From == Invocation->NodeId && Edge.OutputPort == Incoming.OutputPort)
            {
                ++ChildCount;
            }
        }
        if (ChildCount == 0 || Invocations.Num() + ReservedInvocations + ChildCount > Snapshot->Budget.MaxInvocations ||
            ActiveCount + ReservedActive + ChildCount > Snapshot->Budget.MaxActiveNodes ||
            Invocation->Depth + 1 > Snapshot->Budget.MaxDepth)
        {
            return false;
        }
    }
    const bool bStreaming = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming;
    if (Queue.Num() >= Snapshot->Budget.MaxEvents ||
        (!bStreaming && EventCount >= static_cast<uint64>(Snapshot->Budget.MaxEvents)))
    {
        if (!Incoming.bChildrenReserved || !bStreaming)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    FAbilityGraphEvent Event = Incoming;
    Event.SourceToken = RootInput.SourceToken;
    Event.Location = Incoming.Input.Origin;
    if (Port->Payload == EAbilityPortPayload::LiveTarget)
    {
        Event.TargetToken = Incoming.Input.LiveTargetToken.GetValue();
        AbilityExecutionIdentity::ResolveTarget(Event.SourceToken, Event.TargetToken, Event.Location);
    }
    else if (Port->Payload == EAbilityPortPayload::ConfirmedImpact ||
             Port->Payload == EAbilityPortPayload::DamageResult)
    {
        // 快照输出的历史身份不授权后继调用对已死亡/复用目标施加状态。
        Event.Input.LiveTargetToken.Reset();
    }
    Invocation->LastSequence = Event.EventSequence;
    ++Invocation->PendingEvents;
    ++EventCount;
    ReservedInvocations += ChildCount;
    ReservedActive += ChildCount;
    Queue.Add(Event);
    FAbilityQueuedReferences References;
    References.Damage = Event.Input.DamageParams;
    References.Context = Event.Input.Context.Get();
    QueueReferences.Add(References);
    if (Port->bTerminal)
    {
        FinishInvocation(Event.NodeInvocationId, Event.EndReason);
    }
    return !bFinished;
}

void UAbilityGraphInstance::FinishInvocation(const FGuid& Id, EAbilityGraphEndReason Reason)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    auto* Invocation = Invocations.Find(Id);
    if (bFinished || !Invocation || Invocation->bTerminal)
    {
        return;
    }
    Invocation->bTerminal = true;
    if (auto* Execution = Executions.FindRef(Id).Get())
    {
        const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
        Execution->Stop(Reason);
    }
    ReleaseInvocation(Id);
}

void UAbilityGraphInstance::ReleaseInvocation(const FGuid& Id)
{
    auto* Invocation = Invocations.Find(Id);
    if (Invocation && Invocation->bTerminal && !Invocation->bConsumed && Invocation->PendingEvents == 0)
    {
        Invocation->bConsumed = true;
        --ActiveCount;
        Executions.Remove(Id);
    }
}

void UAbilityGraphInstance::Advance(float DeltaSeconds)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    if (bFinished || bAdvancing)
    {
        return;
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f)
    {
        Finish(EAbilityGraphEndReason::Failed);
        return;
    }
    TGuardValue<bool> Guard(bAdvancing, true);
    if (!ResolveSource())
    {
        return;
    }
    TArray<FGuid> ActiveIds;
    Executions.GetKeys(ActiveIds);
    for (const FGuid& Id : ActiveIds)
    {
        if (!ResolveSource())
        {
            return;
        }
        const auto* Invocation = Invocations.Find(Id);
        if (!Invocation || Invocation->bTerminal)
        {
            continue;
        }
        FString Error;
        if (!ValidateInput(Invocation->Input, DescriptorFor(*Invocation)->InputPayload, Error))
        {
            FinishInvocation(Id, EAbilityGraphEndReason::TargetLost);
            continue;
        }
        if (auto* Execution = Executions.FindRef(Id).Get())
        {
            const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
            Execution->Advance(DeltaSeconds);
        }
    }
    DrainEvents();
}

void UAbilityGraphInstance::DrainEvents()
{
    int32 Processed = 0;
    while (!bFinished && !Queue.IsEmpty() && Processed < Snapshot->Budget.MaxEventsPerTick)
    {
        if (!ResolveSource())
        {
            return;
        }
        const FAbilityGraphEvent Event = Queue[0];
        Queue.RemoveAt(0, 1, EAllowShrinking::No);
        ++Processed;
        FInvocation* Current = Invocations.Find(Event.NodeInvocationId);
        --Current->PendingEvents;
        const FInvocation Invocation = *Current;
        // 终态队列消费后释放活动额度，但执行对象保活到观察者及后继调用完成。
        if (Current->bTerminal && Current->PendingEvents == 0 && !Current->bConsumed)
        {
            Current->bConsumed = true;
            --ActiveCount;
        }
        OnEvent.Broadcast(Event);
        if (bFinished || !ResolveSource())
        {
            return;
        }
        const auto Edges = Snapshot->Edges;
        for (const auto& Edge : Edges)
        {
            if (Edge.From == Invocation.NodeId && Edge.OutputPort == Event.OutputPort)
            {
                if (!InvokeNode(Edge.To, Invocation.Depth + 1, Event.Input, Event.bChildrenReserved))
                {
                    return;
                }
            }
        }
        if (const auto* After = Invocations.Find(Event.NodeInvocationId); After && After->bConsumed)
        {
            Executions.Remove(Event.NodeInvocationId);
        }
        QueueReferences.RemoveAt(0, 1, EAllowShrinking::No);
    }
    if (!bFinished && Queue.IsEmpty() && ActiveCount == 0)
    {
        Finish(EAbilityGraphEndReason::Completed);
    }
}

void UAbilityGraphInstance::Cancel(EAbilityGraphEndReason Reason)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    Finish(Reason == EAbilityGraphEndReason::Completed ? EAbilityGraphEndReason::Cancelled : Reason);
}

void UAbilityGraphInstance::Finish(EAbilityGraphEndReason Reason)
{
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    EndReason = Reason;
    TArray<FAbilityGraphEvent> Diagnostics;
    if (Reason != EAbilityGraphEndReason::Completed)
    {
        for (const auto& Pair : Invocations)
        {
            if (Pair.Value.bConsumed)
            {
                continue;
            }
            const auto* Descriptor = DescriptorFor(Pair.Value);
            FAbilityGraphEvent Event;
            Event.ExecutionId = ExecutionId;
            Event.NodeInvocationId = Pair.Key;
            Event.EventSequence = Pair.Value.LastSequence + 1;
            Event.OutputPort =
                Reason == EAbilityGraphEndReason::Cancelled && Descriptor->OutputPorts.Contains(TEXT("Cancelled"))
                    ? FName(TEXT("Cancelled"))
                    : FName(TEXT("Failed"));
            Event.Input = Pair.Value.Input;
            Event.SourceToken = RootInput.SourceToken;
            Event.TargetToken = Pair.Value.Input.LiveTargetToken.Get(FAbilityTargetToken());
            Event.Location = Pair.Value.Input.Origin;
            Event.EndReason = Reason;
            Diagnostics.Add(Event);
        }
    }
    // 先关门和清队列；Stop 允许同步回调 Cancel/Publish，但不能重新打开业务通路。
    Queue.Reset();
    QueueReferences.Reset();
    TArray<TObjectPtr<UAbilityNodeExecution>> ToStop;
    Executions.GenerateValueArray(ToStop);
    for (auto Execution : ToStop)
    {
        const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution.Get());
        Execution->Stop(Reason);
    }
    Executions.Reset();
    Invocations.Reset();
    ActiveCount = 0;
    ReservedActive = 0;
    ReservedInvocations = 0;
    Snapshot = nullptr;
    RegisteredClasses.Reset();
    for (const auto& Event : Diagnostics)
    {
        OnEvent.Broadcast(Event);
    }
    OnFinished.Broadcast(Reason);
}
