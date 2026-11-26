// 调用链：入口启动快照 -> 注册策略 Start/Advance -> Publish/RequestChild -> DrainEvents -> 后继策略。
// 关闭先封事件门，再 Stop 全部策略；回调中的 Cancel/Finish 不会使容器引用失效后被再次使用。
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/AbilitySelectedTargetService.h"
#include "AbilitySystemComponent.h"
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

bool UAbilityGraphInstance::ReservePhysicalSpawn(int32 Count)
{
    if (!bStarted || bFinished || !Snapshot || Count <= 0 ||
        (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained ||
         Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility) ||
        Count > Snapshot->Budget.MaxInvocations - PhysicalSpawnCount || !ResolveSource())
    {
        return false;
    }
    PhysicalSpawnCount += Count;
    PhysicalSpawnCountTotal += Count;
    return true;
}

bool UAbilityGraphInstance::ReservePhysicalSpawn(const FGuid& Id, int32 Count)
{
    if (bFinished || !Snapshot || !ResolveSource())
    {
        return false;
    }
    const auto* Invocation = Invocations.Find(Id);
    // 无有限域的旧图保留原无Id合同；开启有限域后，未知Id不能借旧API逃离自己的额度。
    if (!Invocation)
    {
        return !Snapshot->bHasRuntimeFinite && ReservePhysicalSpawn(Count);
    }
    if (!Invocation->bRuntimeFinite)
    {
        return ReservePhysicalSpawn(Count);
    }
    if (Invocation->bTerminal || Count <= 0 ||
        Count > Snapshot->RuntimeFiniteBudget.MaxInvocations - FinitePhysicalStarts)
    {
        return false;
    }
    FinitePhysicalStarts += Count;
    PhysicalSpawnCountTotal += Count;
    return true;
}

bool UAbilityGraphInstance::ResolveOrCaptureOriginalTarget(const FPureMassCombatTarget& Target,
                                                           FAbilityTargetToken& OutTarget, FVector& OutLocation,
                                                           FString& Error)
{
    OutTarget = {};
    OutLocation = FVector::ZeroVector;
    Error.Reset();
    if (!ResolveSource())
    {
        Error = TEXT("Original-root query source is no longer current.");
        return false;
    }
    if (const auto* Existing = OriginalTargetLives.Find(Target))
    {
        OutTarget = *Existing;
        if (AbilityExecutionIdentity::ResolveTarget(RootInput.SourceToken, OutTarget, OutLocation))
        {
            return true;
        }
        Error = TEXT("Original target life has ended; this root cannot capture its replacement.");
        return false;
    }
    if (OriginalTargetLives.Num() >= 65536 ||
        !AbilityExecutionIdentity::CaptureTarget(RootInput.SourceToken, Target, OutTarget, Error))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("Original-root target history reached its complete-selection safety limit.");
        }
        return false;
    }
    // 首次捕获后立即固定生命。即使后续几何筛选排除它，也不能在池复用后认领新生命。
    OriginalTargetLives.Add(Target, OutTarget);
    return AbilityExecutionIdentity::ResolveTarget(RootInput.SourceToken, OutTarget, OutLocation);
}

bool UAbilityGraphInstance::HasSuccessor(const FGuid& InvocationId, FName Port) const
{
    const auto* Invocation = Invocations.Find(InvocationId);
    return !bFinished && Snapshot && Invocation &&
           Snapshot->Edges.ContainsByPredicate(
               [&](const FAbilityGraphEdge& Edge)
               {
                   return Edge.From == Invocation->NodeId && Edge.OutputPort == Port;
               });
}

EAbilityPhysicalAdmission UAbilityGraphInstance::CheckPhysicalAdmission(const FGuid& Id, int32 Count) const
{
    if (!bStarted || bFinished || !Snapshot || Count <= 0)
    {
        return EAbilityPhysicalAdmission::Exhausted;
    }
    const auto* Invocation = Invocations.Find(Id);
    if ((!Invocation && Snapshot->bHasRuntimeFinite) || (Invocation && Invocation->bTerminal))
    {
        return EAbilityPhysicalAdmission::Exhausted;
    }
    if (Invocation && Invocation->bRuntimeFinite)
    {
        if (Count > Snapshot->RuntimeFiniteBudget.MaxInvocations - FinitePhysicalStarts)
        {
            return EAbilityPhysicalAdmission::Exhausted;
        }
        return Count > Snapshot->RuntimeFiniteBudget.MaxActiveNodes - FiniteActivePhysical
                   ? EAbilityPhysicalAdmission::Wait
                   : EAbilityPhysicalAdmission::Ready;
    }
    const bool bPrototype = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    const bool bSustained = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained || bPrototype;
    if (!bSustained && Count > Snapshot->Budget.MaxInvocations - PhysicalSpawnCount)
    {
        return EAbilityPhysicalAdmission::Exhausted;
    }
    const int32 Capacity = bPrototype   ? Snapshot->PrototypeHelperBudget.MaxActivePhysical
                           : bSustained ? Snapshot->SustainedBudget.MaxActivePhysical
                                        : Snapshot->Budget.MaxActiveNodes;
    return Count > Capacity - (ActivePhysicalSpawnCount - FiniteActivePhysical) ||
                   (bSustained && PhysicalStartTokens < static_cast<float>(Count))
               ? EAbilityPhysicalAdmission::Wait
               : EAbilityPhysicalAdmission::Ready;
}

bool UAbilityGraphInstance::ReservePhysicalSpawnWithLease(FGuid& OutLease, int32 Count)
{
    OutLease.Invalidate();
    if (!bStarted || bFinished || !Snapshot || Count <= 0 || !ResolveSource())
    {
        return false;
    }
    const bool bPrototype = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    const bool bSustained = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained || bPrototype;
    const int32 Capacity = bPrototype   ? Snapshot->PrototypeHelperBudget.MaxActivePhysical
                           : bSustained ? Snapshot->SustainedBudget.MaxActivePhysical
                                        : Snapshot->Budget.MaxActiveNodes;
    if (Count > Capacity - (ActivePhysicalSpawnCount - FiniteActivePhysical) ||
        (bSustained && PhysicalStartTokens < static_cast<float>(Count)) ||
        (!bSustained && Count > Snapshot->Budget.MaxInvocations - PhysicalSpawnCount))
    {
        return false;
    }
    if (bSustained)
    {
        PhysicalStartTokens -= Count;
    }
    else
    {
        PhysicalSpawnCount += Count;
    }
    OutLease = FGuid::NewGuid();
    PhysicalLeases.Add(OutLease, Count);
    ActivePhysicalSpawnCount += Count;
    PhysicalSpawnCountTotal += Count;
    return true;
}

bool UAbilityGraphInstance::ReleasePhysicalSpawnLease(FGuid Lease)
{
    const auto* Count = PhysicalLeases.Find(Lease);
    if (!Count)
    {
        return false;
    }
    ActivePhysicalSpawnCount -= *Count;
    if (FinitePhysicalLeases.Remove(Lease) > 0)
    {
        FiniteActivePhysical -= *Count;
    }
    PhysicalLeases.Remove(Lease);
    return true;
}

bool UAbilityGraphInstance::ReservePhysicalSpawnWithLease(const FGuid& Id, FGuid& OutLease, int32 Count)
{
    OutLease.Invalidate();
    if (bFinished || !Snapshot || !ResolveSource())
    {
        return false;
    }
    const auto* Invocation = Invocations.Find(Id);
    if (!Invocation)
    {
        return !Snapshot->bHasRuntimeFinite && ReservePhysicalSpawnWithLease(OutLease, Count);
    }
    if (!Invocation->bRuntimeFinite)
    {
        return ReservePhysicalSpawnWithLease(OutLease, Count);
    }
    if (Invocation->bTerminal || Count <= 0 ||
        Count > Snapshot->RuntimeFiniteBudget.MaxInvocations - FinitePhysicalStarts ||
        Count > Snapshot->RuntimeFiniteBudget.MaxActiveNodes - FiniteActivePhysical)
    {
        return false;
    }
    OutLease = FGuid::NewGuid();
    PhysicalLeases.Add(OutLease, Count);
    FinitePhysicalLeases.Add(OutLease);
    FinitePhysicalStarts += Count;
    FiniteActivePhysical += Count;
    ActivePhysicalSpawnCount += Count;
    PhysicalSpawnCountTotal += Count;
    return true;
}

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
    if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained)
    {
        InvocationStartTokens = Snapshot->SustainedBudget.InvocationStartsPerSecond;
        PhysicalStartTokens = Snapshot->SustainedBudget.PhysicalStartsPerSecond;
    }
    else if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility)
    {
        PhysicalStartTokens = Snapshot->PrototypeHelperBudget.PhysicalStartsPerSecond;
    }
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
        Input.Direction.ContainsNaN() || Input.EventCenter.ContainsNaN() || !FMath::IsFinite(Input.Level) ||
        !FMath::IsFinite(Input.DamageScale) || Input.DamageScale < 0.f || !FMath::IsFinite(Input.DebuffDamageScale) ||
        Input.DebuffDamageScale < 0.f ||
        (Input.ShapeRadius.IsSet() && (!FMath::IsFinite(*Input.ShapeRadius) || *Input.ShapeRadius <= 0.f)))
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
    if (!Input.ParameterFrame.IsExplicitlyNull())
    {
        const auto* Parameters = Input.ParameterFrame.Get();
        if (!Parameters || !Parameters->BelongsTo(ExecutionId, RootInput.SourceToken))
        {
            Error = TEXT("Input parameters belong to another execution/source or have already been released.");
            return false;
        }
    }
    if (Input.TargetSet &&
        !AbilitySelectedTargetService::ValidateSelectionOwner(ExecutionId, Input.SourceToken, *Input.TargetSet))
    {
        // 几何回执携带可写准入账本，不能借同ASC的合法目标集合跨施法修改另一根的命中历史。
        Error = TEXT("Selection receipt belongs to another execution/source or has invalid step metadata.");
        return false;
    }
    if (Required == EAbilityPortPayload::TargetSet)
    {
        if (!Input.TargetSet || Input.TargetSet->Targets.Num() > 65536 ||
            !SameSource(Input.TargetSet->Source, RootInput.SourceToken))
        {
            Error = TEXT("Input requires a complete target set captured by this source life.");
            return false;
        }
        for (const auto& Target : Input.TargetSet->Targets)
        {
            if (Target.World != RootInput.SourceToken.World)
            {
                Error = TEXT("Target set contains a target from another world.");
                return false;
            }
        }
        // 集合保留查询时的历史身份；单个失效目标由效果策略跳过，不丢弃其他仍有效成员。
    }
    if (Required == EAbilityPortPayload::PreparedInventory &&
        (!Input.ParameterFrame.IsValid() || !Input.LiveTargetToken.IsSet()))
    {
        Error = TEXT("Prepared inventory requires this root's value frame and an original target identity.");
        return false;
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

int32 UAbilityGraphInstance::InvocationCapacity() const
{
    return Snapshot && Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility
               ? Snapshot->PrototypeHelperBudget.MaxRetainedInvocations
           : Snapshot ? Snapshot->Budget.MaxInvocations
                      : 0;
}

int32 UAbilityGraphInstance::ActiveCapacity() const
{
    return Snapshot && Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility
               ? Snapshot->PrototypeHelperBudget.MaxActiveNodes
           : Snapshot ? Snapshot->Budget.MaxActiveNodes
                      : 0;
}

int32 UAbilityGraphInstance::FrameEventCapacity() const
{
    return Snapshot && Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility
               ? Snapshot->PrototypeHelperBudget.MaxEventsPerTick
           : Snapshot ? Snapshot->Budget.MaxEventsPerTick
                      : 0;
}

int32 UAbilityGraphInstance::QueueCapacity() const
{
    // 原事件的小额度由独立投影账本解释；编译器增加的查询/表现仍使用固定硬256队列。
    return HasOriginalProjection() || Snapshot->bHasRuntimeFinite ? 256 : Snapshot->Budget.MaxEvents;
}

int32 UAbilityGraphInstance::NextEligibleEventIndex() const
{
    if (Queue.IsEmpty())
    {
        return INDEX_NONE;
    }
    return Queue.IndexOfByPredicate(
        [&](const auto& Event)
        {
            const bool bOriginalBlocked =
                Event.bOriginalPublication && (bAdvancingNode || bImmediatePayloadDrain ||
                                               OriginalFrameEventsProcessed >= Snapshot->Budget.MaxEventsPerTick);
            const bool bFiniteBlocked = Event.bFinitePublication &&
                                        FiniteFrameEventsProcessed >= Snapshot->RuntimeFiniteBudget.MaxEventsPerTick;
            return !bOriginalBlocked && !bFiniteBlocked;
        });
}

bool UAbilityGraphInstance::RetiresConsumedInvocations() const
{
    return Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained ||
           Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility;
}

bool UAbilityGraphInstance::HasOriginalProjection() const
{
    return Snapshot && Snapshot->OriginalProjection.bEnabled;
}

bool UAbilityGraphInstance::IsOriginalEntry(const FAbilityGraphNode& Node) const
{
    return HasOriginalProjection() && !Node.bRuntimeFinite &&
           (Node.OriginalRole == EAbilityOriginalNodeRole::Entry ||
            Node.OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime);
}

void UAbilityGraphInstance::ReleaseOriginalInvocation(FGuid Id)
{
    auto* Original = OriginalInvocations.Find(Id);
    if (Original && Original->bTerminal && !Original->bConsumed && Original->PendingEvents == 0)
    {
        Original->bConsumed = true;
        --OriginalActiveCount;
    }
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

bool UAbilityGraphInstance::InvokeNode(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input, bool bReserved,
                                       FGuid ContinuationGroup, FGuid OriginalParent, int32 FiniteParentDepth)
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
    const int32 FiniteDepth = Node->bRuntimeFinite ? FiniteParentDepth + 1 : 0;
    if (Node->bRuntimeFinite)
    {
        if (bReserved)
        {
            --FiniteReservedInvocations;
        }
        const auto& Budget = Snapshot->RuntimeFiniteBudget;
        if (FiniteInvocationCount + FiniteReservedInvocations >= Budget.MaxInvocations ||
            FiniteActiveCount + FiniteReservedInvocations >= Budget.MaxActiveNodes || FiniteDepth > Budget.MaxDepth)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
            return false;
        }
        Depth = FiniteDepth;
    }
    const bool bOriginalEntry = IsOriginalEntry(*Node);
    const auto* Parent = OriginalInvocations.Find(OriginalParent);
    const int32 OriginalDepth = Parent ? Parent->Depth + 1 : 1;
    if (bOriginalEntry)
    {
        // 原工具调用也有自己的helper局部深度；原逻辑深度单独按同一旧预算校验，不能把桥接算进去。
        Depth = 1;
        if (bReserved)
        {
            --ReservedOriginalInvocations;
        }
        if (OriginalInvocations.Num() + ReservedOriginalInvocations >= Snapshot->Budget.MaxInvocations ||
            OriginalActiveCount + ReservedOriginalInvocations >= Snapshot->Budget.MaxActiveNodes ||
            OriginalDepth > Snapshot->Budget.MaxDepth)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
            return false;
        }
    }
    if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility && Descriptor->bPrototypeLogicalEntry)
    {
        Depth = 1;
        if (bReserved && !HasOriginalProjection() && !Node->bRuntimeFinite)
        {
            --ReservedPrototypeLogicalInvocations;
        }
        if (!HasOriginalProjection() && !Node->bRuntimeFinite &&
            PrototypeLogicalInvocations + ReservedPrototypeLogicalInvocations >= Snapshot->Budget.MaxInvocations)
        {
            // 普通Publish没有旧RequestChild预准入；达到旧累计调用上限时仍按原Invoke硬失败。
            Finish(EAbilityGraphEndReason::BudgetExceeded);
            return false;
        }
    }
    if (Invocations.Num() + ReservedInvocations >= InvocationCapacity() ||
        (!Node->bRuntimeFinite &&
         ActiveCount - FiniteActiveCount + ReservedActive - FiniteReservedInvocations >= ActiveCapacity()) ||
        Depth > (Node->bRuntimeFinite      ? Snapshot->RuntimeFiniteBudget.MaxDepth
                 : HasOriginalProjection() ? FAbilityGraphBudget{}.MaxDepth
                                           : Snapshot->Budget.MaxDepth))
    {
        Finish(EAbilityGraphEndReason::BudgetExceeded);
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
    if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained && !Node->bRuntimeFinite)
    {
        if (InvocationStartTokens < 1.f)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
            return false;
        }
        InvocationStartTokens -= 1.f;
    }
    if (!Node->bRuntimeFinite && !HasOriginalProjection() &&
        Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility && Descriptor->bPrototypeLogicalEntry)
    {
        // 和旧Invocations.Add同一时点累计；它与Context预种的Spawned/深度上限是两道不同的门。
        ++PrototypeLogicalInvocations;
    }
    const FGuid Id = FGuid::NewGuid();
    FInvocation Invocation;
    Invocation.NodeId = NodeId;
    Invocation.Depth = Depth;
    Invocation.Input = Input;
    Invocation.ContinuationGroup = ContinuationGroup;
    Invocation.bRuntimeFinite = Node->bRuntimeFinite;
    Invocation.FiniteDepth = Node->bRuntimeFinite ? FiniteDepth : (bOriginalEntry ? 0 : FiniteParentDepth);
    if (Node->bRuntimeFinite)
    {
        ++FiniteInvocationCount;
        ++FiniteActiveCount;
    }
    Invocation.OriginalCall = OriginalParent;
    if (bOriginalEntry)
    {
        Invocation.OriginalCall = FGuid::NewGuid();
        FOriginalInvocation Original;
        Original.DefinitionIndex = Node->OriginalIndex;
        Original.Depth = OriginalDepth;
        OriginalInvocations.Add(Invocation.OriginalCall, Original);
        ++OriginalActiveCount;
    }
    if (auto* Group = Continuations.Find(ContinuationGroup))
    {
        ++Group->Pending;
    }
    Invocations.Add(Id, MoveTemp(Invocation));
    ++ActiveCount;
    UAbilityNodeExecution* Execution = NewObject<UAbilityNodeExecution>(this, Descriptor->ExecutionClass);
    const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
    Executions.Add(Id, Execution);
    // Start 可同步发布或取消，绝不能跨越回调保存 TMap 的元素引用。
    const UAbilityNodeConfig* Config = Node->Config;
    const TStrongObjectPtr<UAbilityGraphDefinition> SnapshotGuard(Snapshot.Get());
    TGuardValue<bool> ScopedFlowGuard(
        bInsertScopedEvents,
        (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility || ContinuationGroup.IsValid()) &&
            bDrainingEvents);
    TGuardValue<int32> InsertCursorGuard(ScopedEventInsertCursor, 0);
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

bool UAbilityGraphInstance::PublishWithContinuation(const FGuid& Id, FName Port, const FAbilityNodeInput& Payload,
                                                    TFunction<void()> Resume)
{
    auto* Invocation = Invocations.Find(Id);
    if (bFinished || !Snapshot || !Invocation || Invocation->bTerminal || !Resume || Snapshot->SchemaVersion != 2 ||
        Continuations.Num() >= ActiveCapacity())
    {
        return false;
    }
    const auto* Descriptor = DescriptorFor(*Invocation);
    const auto* Output = Descriptor ? Descriptor->Ports.FindByPredicate(
                                          [Port](const FAbilityPortDescriptor& Item)
                                          {
                                              return Item.Name == Port;
                                          })
                                    : nullptr;
    if (!Output || Output->bTerminal)
    {
        return false;
    }
    const FGuid GroupId = FGuid::NewGuid();
    FContinuation Group;
    Group.OwnerInvocation = Id;
    Group.Resume = MoveTemp(Resume);
    Continuations.Add(GroupId, MoveTemp(Group));
    ++Invocation->PendingContinuations;
    FAbilityGraphEvent Event;
    Event.ExecutionId = ExecutionId;
    Event.NodeInvocationId = Id;
    Event.EventSequence = Invocation->LastSequence + 1;
    Event.OutputPort = Port;
    Event.Input = Payload;
    // 只排队而不在这里推进。父Start/事件观察者内调用也不会产生嵌套执行栈。
    if (EnqueueGroupedEvent(Event, GroupId, true))
    {
        return true;
    }
    Continuations.Remove(GroupId);
    if (auto* Current = Invocations.Find(Id))
    {
        --Current->PendingContinuations;
        ReleaseInvocation(Id);
    }
    return false;
}

bool UAbilityGraphInstance::RegisterRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot,
                                                    UObject* Resource)
{
    if (!bStarted || bFinished || Slot.IsNone() || !IsValid(Resource) ||
        !Frame.BelongsTo(ExecutionId, RootInput.SourceToken) || !ResolveSource())
    {
        return false;
    }
    RuntimeResources.RemoveAll(
        [](const FRuntimeResourceSlot& Entry)
        {
            return !Entry.Frame.IsValid() || !Entry.Resource.IsValid();
        });
    const auto* Existing = RuntimeResources.FindByPredicate(
        [&](const FRuntimeResourceSlot& Entry)
        {
            return Entry.Frame.Get() == &Frame && Entry.Slot == Slot;
        });
    if (Existing)
    {
        // 重复登记相同租约幂等；不得无声覆盖仍由另一个节点拥有的资源。
        return Existing->Resource.Get() == Resource;
    }
    if (RuntimeResources.Num() >= 256)
    {
        return false;
    }
    RuntimeResources.Add({&Frame, Slot, Resource});
    return true;
}

UObject* UAbilityGraphInstance::FindRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot) const
{
    AActor* Avatar = nullptr;
    if (!bStarted || bFinished || Slot.IsNone() || !Frame.BelongsTo(ExecutionId, RootInput.SourceToken) ||
        !AbilityExecutionIdentity::ResolveSource(RootInput.SourceToken, Avatar))
    {
        return nullptr;
    }
    const auto* Entry = RuntimeResources.FindByPredicate(
        [&](const FRuntimeResourceSlot& Candidate)
        {
            return Candidate.Frame.Get() == &Frame && Candidate.Slot == Slot;
        });
    return Entry ? Entry->Resource.Get() : nullptr;
}

bool UAbilityGraphInstance::UnregisterRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot,
                                                      UObject* ExactResource)
{
    if (!bStarted || bFinished || !ExactResource || !Frame.BelongsTo(ExecutionId, RootInput.SourceToken))
    {
        return false;
    }
    const TWeakObjectPtr<UObject> Exact(ExactResource);
    return RuntimeResources.RemoveAll(
               [&](const FRuntimeResourceSlot& Entry)
               {
                   return Entry.Frame.Get() == &Frame && Entry.Slot == Slot && Entry.Resource == Exact;
               }) != 0;
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
    return EnqueueGroupedEvent(Incoming, FGuid(), false);
}

bool UAbilityGraphInstance::EnqueueGroupedEvent(const FAbilityGraphEvent& Incoming, FGuid Group, bool bOverrideGroup)
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
    const auto* Definition = Snapshot->Nodes.FindByPredicate(
        [&](const auto& Candidate)
        {
            return Candidate.NodeId == Invocation->NodeId;
        });
    const auto* OriginalMapping = HasOriginalProjection() && Definition
                                      ? Definition->OriginalOutputs.FindByPredicate(
                                            [&](const auto& Mapping)
                                            {
                                                return Mapping.OutputPort == Incoming.OutputPort;
                                            })
                                      : nullptr;
    auto* OriginalCall = OriginalInvocations.Find(Invocation->OriginalCall);
    if (OriginalMapping && (!OriginalCall || OriginalCall->DefinitionIndex != Definition->OriginalIndex))
    {
        Finish(EAbilityGraphEndReason::Failed);
        return false;
    }
    int32 ChildCount = 0;
    int32 LogicalChildCount = 0;
    int32 OriginalChildCount = 0;
    int32 FiniteChildCount = 0;
    if (Incoming.bChildrenReserved)
    {
        for (const auto& Edge : Snapshot->Edges)
        {
            if (Edge.From == Invocation->NodeId && Edge.OutputPort == Incoming.OutputPort)
            {
                ++ChildCount;
                if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility ||
                    Snapshot->bHasRuntimeFinite)
                {
                    const auto* Child = Snapshot->Nodes.FindByPredicate(
                        [&](const auto& Candidate)
                        {
                            return Candidate.NodeId == Edge.To;
                        });
                    const auto* ChildType = Child ? Registry.Find(Child->TypeId) : nullptr;
                    LogicalChildCount += Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility &&
                                                 !HasOriginalProjection() && ChildType &&
                                                 ChildType->bPrototypeLogicalEntry && !Child->bRuntimeFinite
                                             ? 1
                                             : 0;
                    OriginalChildCount += Child && IsOriginalEntry(*Child) ? 1 : 0;
                    FiniteChildCount += Child && Child->bRuntimeFinite ? 1 : 0;
                }
            }
        }
        if (ChildCount == 0 ||
            PrototypeLogicalInvocations + ReservedPrototypeLogicalInvocations + LogicalChildCount >
                Snapshot->Budget.MaxInvocations ||
            Invocations.Num() + ReservedInvocations + ChildCount > InvocationCapacity() ||
            ActiveCount - FiniteActiveCount + ReservedActive - FiniteReservedInvocations + ChildCount -
                    FiniteChildCount >
                ActiveCapacity() ||
            (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained && !Invocation->bRuntimeFinite &&
             InvocationStartTokens - ReservedInvocations < ChildCount) ||
            (Options.BudgetPolicy != EAbilityGraphBudgetPolicy::PrototypeCompatibility &&
             Invocation->Depth + 1 > Snapshot->Budget.MaxDepth) ||
            (OriginalChildCount > 0 && (OriginalInvocations.Num() + ReservedOriginalInvocations + OriginalChildCount >
                                            Snapshot->Budget.MaxInvocations ||
                                        OriginalActiveCount + ReservedOriginalInvocations + OriginalChildCount >
                                            Snapshot->Budget.MaxActiveNodes ||
                                        (OriginalCall && OriginalCall->Depth + 1 > Snapshot->Budget.MaxDepth))) ||
            (FiniteChildCount > 0 &&
             (FiniteInvocationCount + FiniteReservedInvocations + FiniteChildCount >
                  Snapshot->RuntimeFiniteBudget.MaxInvocations ||
              FiniteActiveCount + FiniteReservedInvocations + FiniteChildCount >
                  Snapshot->RuntimeFiniteBudget.MaxActiveNodes ||
              (Invocation->bRuntimeFinite && Invocation->FiniteDepth + 1 > Snapshot->RuntimeFiniteBudget.MaxDepth))))
        {
            return false;
        }
    }
    const bool bStreaming = Options.BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming ||
                            Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained ||
                            Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    const bool bOriginalStreaming = HasOriginalProjection() && Snapshot->OriginalProjection.BudgetPolicy ==
                                                                   EAbilityGraphBudgetPolicy::LegacyStreaming;
    int32 HostQueuedEvents = 0;
    if (Snapshot->bHasRuntimeFinite && !HasOriginalProjection())
    {
        for (const auto& Pending : Queue)
        {
            HostQueuedEvents += Pending.bFinitePublication ? 0 : 1;
        }
    }
    if (Snapshot->bHasRuntimeFinite && !HasOriginalProjection() && !Invocation->bRuntimeFinite &&
        HostQueuedEvents >= Snapshot->Budget.MaxEvents)
    {
        if (!Incoming.bChildrenReserved || !bStreaming)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    if (Invocation->bRuntimeFinite &&
        (FiniteEventCount >= static_cast<uint64>(Snapshot->RuntimeFiniteBudget.MaxEvents) ||
         FiniteQueuedEvents >= Snapshot->RuntimeFiniteBudget.MaxEvents))
    {
        Finish(EAbilityGraphEndReason::BudgetExceeded);
        return false;
    }
    if (OriginalMapping &&
        (OriginalQueuedEvents >= Snapshot->Budget.MaxEvents ||
         (!bOriginalStreaming && OriginalEventCount >= static_cast<uint64>(Snapshot->Budget.MaxEvents))))
    {
        if (!Incoming.bChildrenReserved || !bOriginalStreaming)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    if (Queue.Num() + DeferredLogicalInvocations.Num() >= QueueCapacity() ||
        (!bStreaming && EventCount >= static_cast<uint64>(Snapshot->Budget.MaxEvents)))
    {
        if (!Incoming.bChildrenReserved || !bStreaming)
        {
            Finish(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    FAbilityGraphEvent Event = Incoming;
    Event.bOriginalPublication = OriginalMapping != nullptr;
    Event.bFinitePublication = Invocation->bRuntimeFinite;
    Event.NodeType = Descriptor->TypeId;
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
    if (!bOverrideGroup)
    {
        Group = Invocation->ContinuationGroup;
    }
    if (auto* Pending = Continuations.Find(Group))
    {
        ++Pending->Pending;
    }
    ++EventCount;
    ReservedInvocations += ChildCount;
    ReservedActive += ChildCount;
    ReservedPrototypeLogicalInvocations += LogicalChildCount;
    ReservedOriginalInvocations += OriginalChildCount;
    FiniteReservedInvocations += FiniteChildCount;
    if (Invocation->bRuntimeFinite)
    {
        ++FiniteEventCount;
        ++FiniteQueuedEvents;
    }
    if (OriginalMapping)
    {
        ++OriginalEventCount;
        ++OriginalQueuedEvents;
        ++OriginalCall->PendingEvents;
        if (OriginalMapping->OriginalPort == TEXT("Completed") &&
            Snapshot->OriginalProjection.Definitions[OriginalCall->DefinitionIndex].bLegacyMacro)
        {
            OriginalCall->bTerminal = true;
        }
    }
    FAbilityQueuedReferences References;
    References.Damage = Event.Input.DamageParams;
    References.Context = Event.Input.Context.Get();
    References.ParameterFrame = Event.Input.ParameterFrame.Get();
    if (bInsertScopedEvents && bDrainingEvents && !OriginalMapping)
    {
        // 同一次Start的多个输出保留声明/候选顺序；下一次Start重新从队首插入，模拟旧同步片段边界。
        Queue.Insert(Event, ScopedEventInsertCursor);
        QueueGroups.Insert(Group, ScopedEventInsertCursor);
        QueueScopedFlow.Insert(true, ScopedEventInsertCursor);
        QueueReferences.Insert(References, ScopedEventInsertCursor++);
    }
    else
    {
        Queue.Add(Event);
        QueueGroups.Add(Group);
        QueueScopedFlow.Add(false);
        QueueReferences.Add(References);
    }
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
    if (HasOriginalProjection())
    {
        const auto* Definition = Snapshot->Nodes.FindByPredicate(
            [&](const auto& Candidate)
            {
                return Candidate.NodeId == Invocation->NodeId;
            });
        if (Definition && Definition->OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime)
        {
            if (auto* Original = OriginalInvocations.Find(Invocation->OriginalCall))
            {
                Original->bTerminal = true;
            }
            ReleaseOriginalInvocation(Invocation->OriginalCall);
        }
    }
    if (auto* Execution = Executions.FindRef(Id).Get())
    {
        const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
        Execution->Stop(Reason);
    }
    // Stop可同步取消整图；不能继续使用回调前的TMap元素引用。
    Invocation = Invocations.Find(Id);
    if (!Invocation || bFinished)
    {
        return;
    }
    if (HasOriginalProjection() && Invocation->PendingEvents > 0 && !Invocation->bActiveReleased &&
        Invocation->PendingContinuations == 0)
    {
        const auto* Definition = Snapshot->Nodes.FindByPredicate(
            [&](const auto& Candidate)
            {
                return Candidate.NodeId == Invocation->NodeId;
            });
        const auto* Descriptor = DescriptorFor(*Invocation);
        if (Definition && Definition->OriginalRole == EAbilityOriginalNodeRole::Helper &&
            !Definition->OriginalOutputs.IsEmpty() && Descriptor && Descriptor->bOriginalProjectionEndpoint &&
            !Descriptor->bIsAsync)
        {
            // 透明桥已同步完成，不能同时占“待消费桥活动位”和其原Scope预留，挤掉合法原64调用。
            // 只归还helper活动额；Invocation metadata/强队列引用仍留到事件消费，原主寿命账本不变。
            Invocation->bActiveReleased = true;
            --ActiveCount;
            if (Invocation->bRuntimeFinite)
            {
                --FiniteActiveCount;
            }
        }
    }
    ReleaseInvocation(Id);
}

void UAbilityGraphInstance::ReleaseInvocation(const FGuid& Id)
{
    auto* Invocation = Invocations.Find(Id);
    if (Invocation && Invocation->bTerminal && !Invocation->bConsumed && Invocation->PendingEvents == 0 &&
        Invocation->PendingContinuations == 0)
    {
        Invocation->bConsumed = true;
        if (!Invocation->bActiveReleased)
        {
            Invocation->bActiveReleased = true;
            --ActiveCount;
            if (Invocation->bRuntimeFinite)
            {
                --FiniteActiveCount;
            }
        }
        RetireConsumedInvocation(Id);
    }
}

void UAbilityGraphInstance::RetireConsumedInvocation(const FGuid& Id)
{
    auto* Invocation = Invocations.Find(Id);
    if (!Invocation || !Invocation->bConsumed)
    {
        return;
    }
    const FGuid Group = Invocation->ContinuationGroup;
    Invocation->ContinuationGroup.Invalidate();
    Executions.Remove(Id);
    if (RetiresConsumedInvocations())
    {
        // 普通有限图仍保留累计调用/防重放记录；组计数只在首次回收时归还。
        Invocations.Remove(Id);
    }
    CompleteContinuationUnit(Group);
}

void UAbilityGraphInstance::CompleteContinuationUnit(FGuid Id)
{
    auto* Group = Continuations.Find(Id);
    if (Group && !Group->bReady && --Group->Pending == 0)
    {
        Group->bReady = true;
        ReadyContinuations.Add(Id);
    }
}

void UAbilityGraphInstance::RunReadyContinuation()
{
    const FGuid Id = ReadyContinuations[0];
    ReadyContinuations.RemoveAt(0, 1, EAllowShrinking::No);
    auto* Pending = Continuations.Find(Id);
    if (!Pending || !Pending->bReady || Pending->Pending != 0)
    {
        return;
    }
    const FGuid OwnerId = Pending->OwnerInvocation;
    auto Resume = MoveTemp(Pending->Resume);
    Continuations.Remove(Id);
    auto* Owner = Invocations.Find(OwnerId);
    if (!Owner)
    {
        return;
    }
    --Owner->PendingContinuations;
    const bool bCanResume = !Owner->bTerminal && !Owner->bConsumed;
    const TStrongObjectPtr<UAbilityNodeExecution> OwnerGuard(Executions.FindRef(OwnerId).Get());
    // 计数/回调先从容器取走，用户逻辑可以取消整根、GC或发布下一组而不使迭代引用悬空。
    if (bCanResume && ResolveSource())
    {
        TGuardValue<bool> FlowGuard(bInsertScopedEvents, true);
        TGuardValue<int32> CursorGuard(ScopedEventInsertCursor, 0);
        Resume();
    }
    ReleaseInvocation(OwnerId);
}

void UAbilityGraphInstance::Advance(float DeltaSeconds)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    if (bFinished || bAdvancing || bDrainingEvents)
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
    if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained)
    {
        const auto& Limits = Snapshot->SustainedBudget;
        InvocationStartTokens = FMath::Min(static_cast<float>(Limits.InvocationStartsPerSecond),
                                           InvocationStartTokens + DeltaSeconds * Limits.InvocationStartsPerSecond);
        PhysicalStartTokens = FMath::Min(static_cast<float>(Limits.PhysicalStartsPerSecond),
                                         PhysicalStartTokens + DeltaSeconds * Limits.PhysicalStartsPerSecond);
    }
    else if (Options.BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility)
    {
        const float Rate = Snapshot->PrototypeHelperBudget.PhysicalStartsPerSecond;
        PhysicalStartTokens = FMath::Min(Rate, PhysicalStartTokens + DeltaSeconds * Rate);
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
        const auto* Descriptor = DescriptorFor(*Invocation);
        const auto Required = Descriptor->bOwnsTargetTracking ? EAbilityPortPayload::Origin : Descriptor->InputPayload;
        if (!ValidateInput(Invocation->Input, Required, Error))
        {
            FinishInvocation(Id, EAbilityGraphEndReason::TargetLost);
            continue;
        }
        if (auto* Execution = Executions.FindRef(Id).Get())
        {
            const TStrongObjectPtr<UAbilityNodeExecution> ExecutionGuard(Execution);
            TGuardValue<bool> OriginalBodyGuard(bAdvancingNode, HasOriginalProjection());
            Execution->Advance(DeltaSeconds);
        }
    }
    DrainEvents();
}

bool UAbilityGraphInstance::FlushPublishedEvents(bool bRequireImmediate)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    if (!bStarted)
    {
        return false;
    }
    if (bFinished)
    {
        return EndReason == EAbilityGraphEndReason::Completed || EndReason == EAbilityGraphEndReason::Expired;
    }
    bImmediateDrainRequested |= bRequireImmediate;
    if (bDrainingEvents)
    {
        // 回调只请求当前迭代循环继续；不能在观察者或节点 Start 内递归执行同一队列。
        return true;
    }
    DrainEvents();
    return bFinished ? EndReason == EAbilityGraphEndReason::Completed || EndReason == EAbilityGraphEndReason::Expired
                     : NextEligibleEventIndex() == INDEX_NONE && ReadyContinuations.IsEmpty() &&
                           (bImmediatePayloadDrain || DeferredLogicalInvocations.IsEmpty());
}

bool UAbilityGraphInstance::FlushImmediatePayloads(bool bRequireImmediate)
{
    if (Options.BudgetPolicy != EAbilityGraphBudgetPolicy::PrototypeCompatibility)
    {
        return FlushPublishedEvents(bRequireImmediate);
    }
    // 该限制只包围原型本体的同步接触，不影响原生弹体或普通 DAG 的 Flush 合同。
    TGuardValue<bool> PayloadGuard(bImmediatePayloadDrain, true);
    return FlushPublishedEvents(bRequireImmediate);
}

bool UAbilityGraphInstance::DeferLogicalInvocation(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input,
                                                   bool bAlreadyReserved, FGuid ContinuationGroup, FGuid OriginalParent,
                                                   int32 FiniteParentDepth)
{
    const auto* Definition = Snapshot->Nodes.FindByPredicate(
        [&](const auto& Candidate)
        {
            return Candidate.NodeId == NodeId;
        });
    const bool bOriginalEntry = Definition && IsOriginalEntry(*Definition);
    const bool bFinite = Definition && Definition->bRuntimeFinite;
    const auto* Original = OriginalInvocations.Find(OriginalParent);
    if (Queue.Num() + DeferredLogicalInvocations.Num() >= QueueCapacity() ||
        (!bAlreadyReserved &&
         (Invocations.Num() + ReservedInvocations >= InvocationCapacity() ||
          (!bFinite &&
           ActiveCount - FiniteActiveCount + ReservedActive - FiniteReservedInvocations >= ActiveCapacity()) ||
          (!HasOriginalProjection() && !bFinite &&
           PrototypeLogicalInvocations + ReservedPrototypeLogicalInvocations >= Snapshot->Budget.MaxInvocations) ||
          (bOriginalEntry &&
           (OriginalInvocations.Num() + ReservedOriginalInvocations >= Snapshot->Budget.MaxInvocations ||
            OriginalActiveCount + ReservedOriginalInvocations >= Snapshot->Budget.MaxActiveNodes ||
            (Original && Original->Depth + 1 > Snapshot->Budget.MaxDepth))) ||
          (bFinite &&
           (FiniteInvocationCount + FiniteReservedInvocations >= Snapshot->RuntimeFiniteBudget.MaxInvocations ||
            FiniteActiveCount + FiniteReservedInvocations >= Snapshot->RuntimeFiniteBudget.MaxActiveNodes ||
            FiniteParentDepth + 1 > Snapshot->RuntimeFiniteBudget.MaxDepth)))))
    {
        Finish(EAbilityGraphEndReason::BudgetExceeded);
        return false;
    }
    if (!bAlreadyReserved)
    {
        ++ReservedInvocations;
        ++ReservedActive;
        if (bOriginalEntry)
        {
            ++ReservedOriginalInvocations;
        }
        if (!HasOriginalProjection() && !bFinite)
        {
            ++ReservedPrototypeLogicalInvocations;
        }
        if (bFinite)
        {
            ++FiniteReservedInvocations;
        }
    }
    FDeferredLogicalInvocation Deferred;
    Deferred.NodeId = NodeId;
    Deferred.Depth = Depth;
    Deferred.Input = Input;
    Deferred.ContinuationGroup = ContinuationGroup;
    Deferred.OriginalParent = OriginalParent;
    Deferred.FiniteParentDepth = FiniteParentDepth;
    DeferredLogicalInvocations.Add(MoveTemp(Deferred));
    FAbilityQueuedReferences References;
    References.Damage = Input.DamageParams;
    References.Context = Input.Context.Get();
    References.ParameterFrame = Input.ParameterFrame.Get();
    DeferredReferences.Add(MoveTemp(References));
    if (auto* Group = Continuations.Find(ContinuationGroup))
    {
        // 事件消费之后，尚未启动的后继仍是组的一部分；不能提前恢复本体。
        ++Group->Pending;
    }
    return true;
}

void UAbilityGraphInstance::RunDeferredLogicalInvocation()
{
    const auto Deferred = DeferredLogicalInvocations[0];
    const auto References = DeferredReferences[0];
    const TStrongObjectPtr<UObject> ContextGuard(References.Context.Get());
    const TStrongObjectPtr<UAbilityExecutionParameters> FrameGuard(References.ParameterFrame.Get());
    const TStrongObjectPtr<UObject> WorldGuard(References.Damage.WorldContextObject.Get());
    const TStrongObjectPtr<UClass> EffectGuard(References.Damage.DamageGameplayEffectClass.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> SourceGuard(References.Damage.SourceAbilitySystemComponent.Get());
    const TStrongObjectPtr<UAbilitySystemComponent> TargetGuard(References.Damage.TargetAbilitySystemComponent.Get());
    DeferredLogicalInvocations.RemoveAt(0, 1, EAllowShrinking::No);
    DeferredReferences.RemoveAt(0, 1, EAllowShrinking::No);
    InvokeNode(Deferred.NodeId, Deferred.Depth, Deferred.Input, true, Deferred.ContinuationGroup,
               Deferred.OriginalParent, Deferred.FiniteParentDepth);
    // InvokeNode 已登记实际调用的组占用，或因旧生命失效跳过；现在才归还预留占用。
    CompleteContinuationUnit(Deferred.ContinuationGroup);
}

void UAbilityGraphInstance::DrainEvents()
{
    if (bFinished || bDrainingEvents)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    TGuardValue<bool> DrainingGuard(bDrainingEvents, true);
    if (LastDrainFrame != GFrameCounter)
    {
        LastDrainFrame = GFrameCounter;
        FrameEventsProcessed = 0;
        OriginalFrameEventsProcessed = 0;
        FiniteFrameEventsProcessed = 0;
    }
    auto HasDrainWork = [&]()
    {
        return NextEligibleEventIndex() != INDEX_NONE || !ReadyContinuations.IsEmpty() ||
               (!bImmediatePayloadDrain && !DeferredLogicalInvocations.IsEmpty());
    };
    while (!bFinished && HasDrainWork() && FrameEventsProcessed < FrameEventCapacity())
    {
        if (!ResolveSource())
        {
            return;
        }
        if (!ReadyContinuations.IsEmpty())
        {
            // continuation 与事件共享原实际帧额度；重复 Flush 不能免费执行更多步骤。
            ++FrameEventsProcessed;
            RunReadyContinuation();
            continue;
        }
        if (!bImmediatePayloadDrain && !DeferredLogicalInvocations.IsEmpty() &&
            (Queue.IsEmpty() || !QueueScopedFlow[0]))
        {
            // 刚启动片段的同步流程先结算，再启动下一个逻辑兄弟，保持旧同步片段顺序。
            ++FrameEventsProcessed;
            RunDeferredLogicalInvocation();
            continue;
        }
        const int32 EventIndex = NextEligibleEventIndex();
        if (EventIndex == INDEX_NONE)
        {
            break;
        }
        const FAbilityGraphEvent Event = Queue[EventIndex];
        const FGuid EventGroup = QueueGroups[EventIndex];
        const auto References = QueueReferences[EventIndex];
        const TStrongObjectPtr<UObject> EventContextGuard(References.Context.Get());
        const TStrongObjectPtr<UAbilityExecutionParameters> EventParametersGuard(References.ParameterFrame.Get());
        const TStrongObjectPtr<UObject> EventWorldContextGuard(References.Damage.WorldContextObject.Get());
        const TStrongObjectPtr<UClass> EventDamageClassGuard(References.Damage.DamageGameplayEffectClass.Get());
        const TStrongObjectPtr<UAbilitySystemComponent> EventSourceASCGuard(
            References.Damage.SourceAbilitySystemComponent.Get());
        const TStrongObjectPtr<UAbilitySystemComponent> EventTargetASCGuard(
            References.Damage.TargetAbilitySystemComponent.Get());
        Queue.RemoveAt(EventIndex, 1, EAllowShrinking::No);
        QueueGroups.RemoveAt(EventIndex, 1, EAllowShrinking::No);
        QueueScopedFlow.RemoveAt(EventIndex, 1, EAllowShrinking::No);
        QueueReferences.RemoveAt(EventIndex, 1, EAllowShrinking::No);
        ++FrameEventsProcessed;
        FInvocation* Current = Invocations.Find(Event.NodeInvocationId);
        --Current->PendingEvents;
        const FInvocation Invocation = *Current;
        if (Event.bFinitePublication)
        {
            ++FiniteFrameEventsProcessed;
            --FiniteQueuedEvents;
        }
        if (Event.bOriginalPublication)
        {
            ++OriginalFrameEventsProcessed;
            --OriginalQueuedEvents;
            if (auto* Original = OriginalInvocations.Find(Invocation.OriginalCall))
            {
                --Original->PendingEvents;
            }
            ReleaseOriginalInvocation(Invocation.OriginalCall);
        }
        // 终态队列消费后释放活动额度，但执行对象保活到观察者及后继调用完成。
        if (Current->bTerminal && Current->PendingEvents == 0 && Current->PendingContinuations == 0 &&
            !Current->bConsumed)
        {
            Current->bConsumed = true;
            if (!Current->bActiveReleased)
            {
                Current->bActiveReleased = true;
                --ActiveCount;
                if (Current->bRuntimeFinite)
                {
                    --FiniteActiveCount;
                }
            }
        }
        const TStrongObjectPtr<UAbilityNodeExecution> EventExecutionGuard(
            Executions.FindRef(Event.NodeInvocationId).Get());
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
                const auto* Destination = Snapshot->Nodes.FindByPredicate(
                    [&](const FAbilityGraphNode& Node)
                    {
                        return Node.NodeId == Edge.To;
                    });
                const auto* DestinationType = Destination ? Registry.Find(Destination->TypeId) : nullptr;
                const bool bDefer =
                    bImmediatePayloadDrain && DestinationType && DestinationType->bPrototypeLogicalEntry;
                const bool bAccepted =
                    bDefer ? DeferLogicalInvocation(Edge.To, Invocation.Depth + 1, Event.Input, Event.bChildrenReserved,
                                                    EventGroup, Invocation.OriginalCall, Invocation.FiniteDepth)
                           : InvokeNode(Edge.To, Invocation.Depth + 1, Event.Input, Event.bChildrenReserved, EventGroup,
                                        Invocation.OriginalCall, Invocation.FiniteDepth);
                if (!bAccepted)
                {
                    return;
                }
            }
        }
        if (const auto* After = Invocations.Find(Event.NodeInvocationId); After && After->bConsumed)
        {
            RetireConsumedInvocation(Event.NodeInvocationId);
        }
        // 输出事件的占用延续到全部直接子调用已登记后；异步后继继续持有自己的占用。
        CompleteContinuationUnit(EventGroup);
    }
    if (!bFinished && Queue.IsEmpty() && ReadyContinuations.IsEmpty() && DeferredLogicalInvocations.IsEmpty() &&
        ActiveCount == 0)
    {
        Finish(EAbilityGraphEndReason::Completed);
    }
    else if (!bFinished && HasDrainWork() && bImmediateDrainRequested)
    {
        // 原即时接触合同不能把伤害偷偷延迟到下一帧；超额明确终止，绝不刷新帧额度再排一次。
        Finish(EAbilityGraphEndReason::BudgetExceeded);
    }
    else if (!bFinished && bImmediateDrainRequested && Snapshot->bHasRuntimeFinite &&
             FiniteFrameEventsProcessed >= Snapshot->RuntimeFiniteBudget.MaxEventsPerTick &&
             Queue.ContainsByPredicate(
                 [](const auto& Event)
                 {
                     return Event.bFinitePublication;
                 }))
    {
        // 普通有限片段的强同步伤害合同不能借helper额把本帧已承诺的效果偷偷推迟。
        Finish(EAbilityGraphEndReason::BudgetExceeded);
    }
    if (NextEligibleEventIndex() == INDEX_NONE && ReadyContinuations.IsEmpty())
    {
        bImmediateDrainRequested = false;
    }
}

void UAbilityGraphInstance::Cancel(EAbilityGraphEndReason Reason)
{
    const TStrongObjectPtr<UAbilityGraphInstance> SelfGuard(this);
    Finish(Reason == EAbilityGraphEndReason::Completed ? EAbilityGraphEndReason::Cancelled : Reason);
}

bool UAbilityGraphInstance::AdmitDamageTrigger(const FGuid& InvocationId, int32 Every, int32 Limit, float Cooldown)
{
    const auto* Invocation = Invocations.Find(InvocationId);
    auto* World = RootInput.SourceToken.World.Get();
    if (bFinished || !Invocation || Invocation->bTerminal || !World || Every < 1 || Limit < 1 ||
        !FMath::IsFinite(Cooldown) || Cooldown < 0.f)
    {
        return false;
    }
    auto& State = DamageTriggers.FindOrAdd(Invocation->NodeId);
    const double Now = World->GetTimeSeconds();
    if (State.Passed >= Limit || Now < State.NextAllowedTime)
    {
        return false;
    }
    if (++State.SinceLast < Every)
    {
        return false;
    }
    State.SinceLast = 0;
    ++State.Passed;
    State.NextAllowedTime = Now + Cooldown;
    return true;
}

void UAbilityGraphInstance::Finish(EAbilityGraphEndReason Reason)
{
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    bImmediateDrainRequested = false;
    DamageTriggers.Reset();
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
            Event.NodeType = Descriptor->TypeId;
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
    QueueGroups.Reset();
    QueueScopedFlow.Reset();
    QueueReferences.Reset();
    DeferredLogicalInvocations.Reset();
    DeferredReferences.Reset();
    ReadyContinuations.Reset();
    Continuations.Reset();
    // 索引先失效；之后各策略Stop只关闭自己的强持有资源，不能再查询旧根发布的宿主。
    RuntimeResources.Reset();
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
    PrototypeLogicalInvocations = ReservedPrototypeLogicalInvocations = 0;
    OriginalInvocations.Reset();
    OriginalActiveCount = ReservedOriginalInvocations = 0;
    OriginalEventCount = 0;
    OriginalQueuedEvents = OriginalFrameEventsProcessed = 0;
    FiniteInvocationCount = FiniteActiveCount = FiniteReservedInvocations = 0;
    FiniteEventCount = 0;
    FiniteQueuedEvents = FiniteFrameEventsProcessed = 0;
    FinitePhysicalStarts = FiniteActivePhysical = 0;
    FinitePhysicalLeases.Reset();
    ReservedInvocations = 0;
    PhysicalLeases.Reset();
    ActivePhysicalSpawnCount = 0;
    OriginalTargetLives.Reset();
    RootInput.TargetSet.Reset();
    RootInput.ParameterFrame.Reset();
    Snapshot = nullptr;
    RegisteredClasses.Reset();
    for (const auto& Event : Diagnostics)
    {
        OnEvent.Broadcast(Event);
    }
    OnFinished.Broadcast(Reason);
}
