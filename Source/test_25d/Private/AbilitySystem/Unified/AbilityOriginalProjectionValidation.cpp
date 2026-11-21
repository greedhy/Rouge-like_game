// 调用链：共享定义Validate -> 本只读proof -> 参数/注册策略；helper没有原调用额度身份。
#include "AbilitySystem/Unified/AbilityOriginalProjectionValidation.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"

void AbilityOriginalProjectionValidation::Validate(const UAbilityGraphDefinition& Graph,
                                                   const FAbilityNodeRegistry& Registry, TArray<FString>& Errors)
{
    const auto& Projection = Graph.OriginalProjection;
    if (!Projection.bEnabled)
    {
        if (!Projection.Definitions.IsEmpty() || !Projection.Edges.IsEmpty())
        {
            Errors.Add(TEXT("Disabled original projection cannot retain a hidden manifest."));
        }
        for (const auto& Node : Graph.Nodes)
        {
            if (Node.OriginalIndex != INDEX_NONE || Node.OriginalRole != EAbilityOriginalNodeRole::Helper ||
                !Node.OriginalOutputs.IsEmpty())
            {
                Errors.Add(TEXT("Original node metadata requires an explicit original graph projection."));
            }
        }
        return;
    }
    if (Graph.BudgetPolicy != EAbilityGraphBudgetPolicy::PrototypeCompatibility || Projection.SchemaVersion != 2 ||
        !Projection.Definitions.IsValidIndex(Projection.EntryIndex) || Projection.Definitions.Num() > 512 ||
        (Projection.BudgetPolicy != EAbilityGraphBudgetPolicy::Bounded &&
         Projection.BudgetPolicy != EAbilityGraphBudgetPolicy::LegacyStreaming) ||
        (Projection.CompatibilityProfile != TEXT("UnifiedV2") &&
         !FAbilityNodeRegistry::IsLegacyProfile(Projection.CompatibilityProfile)) ||
        (Projection.BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming &&
         !FAbilityNodeRegistry::IsLegacyProfile(Projection.CompatibilityProfile)))
    {
        Errors.Add(TEXT("Original projection needs a proved Schema2 Bounded/LegacyStreaming source."));
        return;
    }
    if (Projection.BudgetPolicy == EAbilityGraphBudgetPolicy::Bounded && Graph.Budget.MaxActiveNodes > 32)
    {
        Errors.Add(TEXT("Original Bounded source cannot claim a LegacyStreaming active64 declaration."));
    }
    TArray<const FAbilityGraphNode*> Entries;
    Entries.Init(nullptr, Projection.Definitions.Num());
    TArray<TSet<FName>> Lifetimes;
    Lifetimes.SetNum(Projection.Definitions.Num());
    TSet<FString> MappedPorts;
    for (const auto& Node : Graph.Nodes)
    {
        if (Node.OriginalIndex == INDEX_NONE && Node.OriginalRole == EAbilityOriginalNodeRole::Helper &&
            Node.OriginalOutputs.IsEmpty())
        {
            continue;
        }
        if (!Projection.Definitions.IsValidIndex(Node.OriginalIndex))
        {
            Errors.Add(TEXT("Original ordinal is outside the immutable source manifest."));
            continue;
        }
        const auto& Original = Projection.Definitions[Node.OriginalIndex];
        const auto* OldType = Registry.Find(Original.TypeId);
        const auto* CurrentType = Registry.Find(Node.TypeId);
        if (!OldType || !CurrentType || !IsValid(Node.Config))
        {
            Errors.Add(TEXT("Original/current projection strategy is not registered or valid."));
            continue;
        }
        const bool bEntry = Node.OriginalRole == EAbilityOriginalNodeRole::Entry ||
                            Node.OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime;
        const bool bLifetime = Node.OriginalRole == EAbilityOriginalNodeRole::Lifetime ||
                               Node.OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime;
        if (Node.OriginalRole != EAbilityOriginalNodeRole::Helper && !bEntry && !bLifetime)
        {
            Errors.Add(TEXT("Unknown original projection role."));
        }
        if (bEntry)
        {
            FString ProofError;
            const bool bValid =
                Original.bLegacyMacro
                    ? CurrentType->bPrototypeLogicalEntry && Node.OriginalRole == EAbilityOriginalNodeRole::Entry &&
                          Node.Config->SupportsOriginalNode(Original.TypeId, Original.LegacyProfile, ProofError)
                    : Node.TypeId == Original.TypeId && Node.Config->IsA(OldType->ConfigClass.Get()) &&
                          Node.OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime;
            if (Entries[Node.OriginalIndex] || !bValid)
            {
                Errors.Add(TEXT("Original ordinal needs exactly one real entry: ") + ProofError);
            }
            Entries[Node.OriginalIndex] = &Node;
        }
        if (bLifetime)
        {
            if (Lifetimes[Node.OriginalIndex].Contains(Node.TypeId))
            {
                Errors.Add(TEXT("Duplicate main lifetime type for an original definition."));
            }
            Lifetimes[Node.OriginalIndex].Add(Node.TypeId);
        }
        for (const auto& Mapping : Node.OriginalOutputs)
        {
            const auto* CurrentPort = CurrentType->Ports.FindByPredicate(
                [&](const auto& P)
                {
                    return P.Name == Mapping.OutputPort;
                });
            const auto* OldPort = OldType->Ports.FindByPredicate(
                [&](const auto& P)
                {
                    return P.Name == Mapping.OriginalPort;
                });
            const FString Key = FString::Printf(TEXT("%d:%s"), Node.OriginalIndex, *Mapping.OriginalPort.ToString());
            const bool bCompatible = CurrentPort && OldPort &&
                                     (CurrentPort->Payload == OldPort->Payload ||
                                      (OldPort->Payload == EAbilityPortPayload::Origin &&
                                       CurrentPort->Payload != EAbilityPortPayload::PreparedInventory));
            const auto Expected = Original.bLegacyMacro ? (Mapping.OriginalPort == TEXT("Completed")
                                                               ? EAbilityOriginalPublication::Publish
                                                               : EAbilityOriginalPublication::LegacyChildRequest)
                                                        : EAbilityOriginalPublication::Preserve;
            if (!bCompatible || MappedPorts.Contains(Key) || Mapping.Publication != Expected ||
                (Original.bLegacyMacro && !CurrentType->bOriginalProjectionEndpoint) ||
                Mapping.OriginalPort == TEXT("Failed") || Mapping.OriginalPort == TEXT("Cancelled"))
            {
                Errors.Add(TEXT("Original output mapping lacks a unique matching descriptor/publication."));
            }
            MappedPorts.Add(Key);
        }
    }
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const auto* Entry = Entries[Index];
        const auto& Original = Projection.Definitions[Index];
        if (!Entry || Lifetimes[Index].IsEmpty())
        {
            Errors.Add(TEXT("Every original node needs one entry and an explicit main lifetime."));
            continue;
        }
        if (!Original.bLegacyMacro)
        {
            if (Lifetimes[Index].Num() != 1 || !Lifetimes[Index].Contains(Original.TypeId))
            {
                Errors.Add(TEXT("Copied original utility entry/lifetime must be the same actual call."));
            }
            continue;
        }
        if (Lifetimes[Index].Num() > Entry->Config->MaximumOriginalLifetimeNodes())
        {
            Errors.Add(TEXT("Original macro has too many main lifetime owners."));
        }
        for (const auto Type : Lifetimes[Index])
        {
            FString ProofError;
            if (!Entry->Config->SupportsOriginalLifetime(Original.TypeId, Original.LegacyProfile, Type, ProofError))
            {
                Errors.Add(ProofError);
            }
        }
    }
    const auto* Root = Graph.Nodes.FindByPredicate(
        [&](const auto& Node)
        {
            return Node.NodeId == Graph.EntryNodeId;
        });
    if (!Root || Root->OriginalIndex != Projection.EntryIndex || Entries[Projection.EntryIndex] != Root)
    {
        Errors.Add(TEXT("Current root must prove the original entry ordinal."));
    }
    if (!Errors.IsEmpty())
    {
        return;
    }
    // 核对原业务边，不把查询、表现和位置转换的透明helper路径当成新的原调用。
    TArray<FString> ExpectedEdges;
    TArray<TArray<int32>> Outgoing;
    Outgoing.SetNum(Entries.Num());
    TArray<int32> Indegrees;
    Indegrees.SetNumZeroed(Entries.Num());
    for (const auto& Edge : Projection.Edges)
    {
        if (!Entries.IsValidIndex(Edge.FromIndex) || !Entries.IsValidIndex(Edge.ToIndex))
        {
            Errors.Add(TEXT("Original edge ordinal is outside the source manifest."));
            continue;
        }
        const auto* From = Registry.Find(Projection.Definitions[Edge.FromIndex].TypeId);
        const auto* To = Registry.Find(Projection.Definitions[Edge.ToIndex].TypeId);
        const auto* Port = From ? From->Ports.FindByPredicate(
                                      [&](const auto& P)
                                      {
                                          return P.Name == Edge.OutputPort;
                                      })
                                : nullptr;
        const FString Key =
            FString::Printf(TEXT("%d:%s:%d"), Edge.FromIndex, *Edge.OutputPort.ToString(), Edge.ToIndex);
        if (!Port || !To || Edge.OutputPort == TEXT("Failed") || Edge.OutputPort == TEXT("Cancelled") ||
            (To->InputPayload != EAbilityPortPayload::Origin && To->InputPayload != Port->Payload) ||
            ExpectedEdges.Contains(Key))
        {
            Errors.Add(TEXT("Original edge lacks a unique valid original typed port."));
            continue;
        }
        ExpectedEdges.Add(Key);
        Outgoing[Edge.FromIndex].Add(Edge.ToIndex);
        ++Indegrees[Edge.ToIndex];
    }
    TArray<FString> ActualEdges;
    for (const auto& Edge : Graph.Edges)
    {
        const auto* From = Graph.Nodes.FindByPredicate(
            [&](const auto& N)
            {
                return N.NodeId == Edge.From;
            });
        const auto* To = Graph.Nodes.FindByPredicate(
            [&](const auto& N)
            {
                return N.NodeId == Edge.To;
            });
        if (!From || !To)
        {
            continue; // 通用图校验器报告悬空节点；这里不解引用错误输入。
        }
        const bool bOriginalEntry = To->OriginalRole == EAbilityOriginalNodeRole::Entry ||
                                    To->OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime;
        if (bOriginalEntry)
        {
            const auto* Mapping = From->OriginalOutputs.FindByPredicate(
                [&](const auto& M)
                {
                    return M.OutputPort == Edge.OutputPort;
                });
            if (!Mapping || !Entries.IsValidIndex(From->OriginalIndex))
            {
                Errors.Add(TEXT("Original entry cannot be reached through an unmapped helper publication."));
                continue;
            }
            ActualEdges.Add(FString::Printf(TEXT("%d:%s:%d"), From->OriginalIndex, *Mapping->OriginalPort.ToString(),
                                            To->OriginalIndex));
        }
        else if (From->OriginalIndex != INDEX_NONE && To->OriginalIndex != INDEX_NONE &&
                 From->OriginalIndex != To->OriginalIndex)
        {
            Errors.Add(TEXT("Helpers cannot cross source groups while bypassing an original entry."));
        }
    }
    if (ExpectedEdges != ActualEdges)
    {
        Errors.Add(TEXT("Original mapped edges must exactly preserve the source declaration order."));
    }
    // 重建原DAG的静态深度/路径预算。helper局部重置深度不能隐藏被篡改的原拓扑。
    TArray<int32> Order;
    for (int32 Index = 0; Index < Indegrees.Num(); ++Index)
    {
        if (Indegrees[Index] == 0)
        {
            Order.Add(Index);
        }
    }
    for (int32 Cursor = 0; Cursor < Order.Num(); ++Cursor)
    {
        for (const int32 Child : Outgoing[Order[Cursor]])
        {
            if (--Indegrees[Child] == 0)
            {
                Order.Add(Child);
            }
        }
    }
    if (Order.Num() != Entries.Num())
    {
        Errors.Add(TEXT("Original source manifest must remain acyclic."));
        return;
    }
    TArray<int32> Depths;
    TArray<int64> Paths;
    Depths.SetNumZeroed(Entries.Num());
    Paths.SetNumZeroed(Entries.Num());
    Depths[Projection.EntryIndex] = 1;
    Paths[Projection.EntryIndex] = 1;
    int64 Total = 0;
    const int64 Limit = FMath::Max(Graph.Budget.MaxInvocations, Graph.Budget.MaxEvents) + 1;
    for (int32 Index : Order)
    {
        if (Paths[Index] == 0 || Depths[Index] > Graph.Budget.MaxDepth)
        {
            Errors.Add(TEXT("Original source has an unreachable node or exceeds its original depth budget."));
        }
        Total = FMath::Min(Limit, Total + Paths[Index]);
        for (const int32 Child : Outgoing[Index])
        {
            Depths[Child] = FMath::Max(Depths[Child], Depths[Index] + 1);
            Paths[Child] = FMath::Min(Limit, Paths[Child] + Paths[Index]);
        }
    }
    if (Projection.BudgetPolicy == EAbilityGraphBudgetPolicy::Bounded &&
        (Total > Graph.Budget.MaxInvocations || Total > Graph.Budget.MaxEvents))
    {
        Errors.Add(TEXT("Original source expanded paths exceed its original finite call/event budgets."));
    }
}
