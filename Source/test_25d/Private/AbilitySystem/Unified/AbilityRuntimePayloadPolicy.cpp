// 调用链：服务器Recipe编译/Graph.Validate -> 实际端口传播 -> 新节点逐类型纯动作证明。
// 只读索引仅活到本次调用；没有World/Actor/Timer所有权，实际执行仍由原GraphInstance管理。
#include "AbilitySystem/Unified/AbilityRuntimePayloadPolicy.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"

void AbilityRuntimePayloadPolicy::BuildLineage(const UAbilityGraphDefinition& Graph,
                                               const FAbilityNodeRegistry& Registry, FAbilityRuntimeRepeatLineage& Out)
{
    Out.Inputs.Reset();
    TArray<FGuid> Pending;
    auto Admit = [&](const FAbilityGraphEdge& Edge, const FAbilityPortDescriptor& Port)
    {
        auto& Inputs = Out.Inputs.FindOrAdd(Edge.To);
        if (Inputs.IsEmpty())
        {
            Pending.Add(Edge.To);
        }
        Inputs.Add(Port.Payload);
    };
    // 种子是登记的具体输出端口，而非整个持续Profile或该节点的所有输出。
    for (const auto& Edge : Graph.Edges)
    {
        const auto* From = Graph.Nodes.FindByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Edge.From;
            });
        const auto* Descriptor = From ? Registry.Find(From->TypeId) : nullptr;
        if (!Descriptor || !Descriptor->PotentiallyUnboundedOutputPorts.Contains(Edge.OutputPort))
        {
            continue;
        }
        if (const auto* Port = Descriptor->Ports.FindByPredicate(
                [&](const FAbilityPortDescriptor& Item)
                {
                    return Item.Name == Edge.OutputPort;
                }))
        {
            Admit(Edge, *Port);
        }
    }
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        const FGuid Current = Pending[Index];
        const auto* Node = Graph.Nodes.FindByPredicate(
            [Current](const FAbilityGraphNode& Item)
            {
                return Item.NodeId == Current;
            });
        const auto* Descriptor = Node ? Registry.Find(Node->TypeId) : nullptr;
        if (!Descriptor)
        {
            continue;
        }
        for (const auto& Edge : Graph.Edges)
        {
            if (Edge.From != Current)
            {
                continue;
            }
            if (const auto* Port = Descriptor->Ports.FindByPredicate(
                    [&](const FAbilityPortDescriptor& Item)
                    {
                        return Item.Name == Edge.OutputPort;
                    }))
            {
                Admit(Edge, *Port);
            }
        }
    }
}

bool AbilityRuntimePayloadPolicy::IsRepeatedOutput(const FAbilityGraphNode& Node, FName Port,
                                                   const FAbilityNodeRegistry& Registry,
                                                   const FAbilityRuntimeRepeatLineage& Lineage)
{
    const auto* Descriptor = Registry.Find(Node.TypeId);
    return Descriptor && Descriptor->OutputPorts.Contains(Port) &&
           (Descriptor->PotentiallyUnboundedOutputPorts.Contains(Port) || Lineage.Inputs.Contains(Node.NodeId));
}

bool AbilityRuntimePayloadPolicy::ValidatePureNode(const FAbilityGraphNode& Node, EAbilityPortPayload Incoming,
                                                   const FAbilityNodeRegistry& Registry, FString& Error)
{
    const auto* Descriptor = Registry.Find(Node.TypeId);
    if (!Descriptor || !Descriptor->bRuntimePurePayload || Descriptor->bIsAsync || !IsValid(Node.Config) ||
        !Node.Config->IsA(Descriptor->ConfigClass.Get()) || !Node.Config->MatchesType(Node.TypeId))
    {
        Error = TEXT("Repeated runtime payload requires an explicitly audited synchronous action: ") +
                Node.TypeId.ToString();
        return false;
    }
    return Node.Config->Validate(Error) && Node.Config->SupportsRuntimePurePayload(Node.TypeId, Incoming, Error);
}

void AbilityRuntimePayloadPolicy::ValidateGraph(const UAbilityGraphDefinition& Graph,
                                                const FAbilityNodeRegistry& Registry, TArray<FString>& Errors)
{
    FAbilityRuntimeRepeatLineage Lineage;
    BuildLineage(Graph, Registry, Lineage);
    for (const auto& Node : Graph.Nodes)
    {
        if (Node.bRuntimeFinite && Lineage.Inputs.Contains(Node.NodeId))
        {
            Errors.Add(
                TEXT("An indefinitely repeated runtime branch cannot silently inherit the cumulative finite budget."));
        }
        if (!Node.bRuntimeRepeatedPayload)
        {
            continue;
        }
        const auto* Inputs = Lineage.Inputs.Find(Node.NodeId);
        if (Graph.SchemaVersion != 2 || Node.NodeId == Graph.EntryNodeId || !Inputs || Inputs->IsEmpty() ||
            Node.bRuntimeFinite || Node.OriginalIndex != INDEX_NONE ||
            Node.OriginalRole != EAbilityOriginalNodeRole::Helper || !Node.OriginalOutputs.IsEmpty())
        {
            Errors.Add(TEXT(
                "Repeated runtime payload cannot claim an entry, original invocation, finite budget or unproved lineage."));
            continue;
        }
        for (const auto Incoming : *Inputs)
        {
            FString Error;
            if (!ValidatePureNode(Node, Incoming, Registry, Error))
            {
                Errors.Add(FString::Printf(TEXT("NodeId %s: %s"), *Node.NodeId.ToString(), *Error));
            }
        }
    }
}
