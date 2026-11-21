// 调用链：GA/制作入口 -> Validate -> 注册表/配置/端口检查 -> 拓扑排序 -> 路径展开预算。
// 全部临时索引与计数属于本次校验；共享 DA、Config 和注册表都不被修改。
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityOriginalProjectionValidation.h"
#include "AbilitySystem/Unified/AbilityRuntimePayloadPolicy.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"

bool UAbilityGraphDefinition::ValidateForAuthoring(TArray<FString>& Errors) const
{
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    return Validate(Registry, Errors);
}

namespace
{
    /** 编辑器 Clamp 元数据不能约束程序化赋值；拒绝零、负数及超出 V1 已验证上限的预算。 */
    void CheckBudget(const TCHAR* Name, int32 Value, int32 Maximum, TArray<FString>& Errors)
    {
        if (Value <= 0 || Value > Maximum)
        {
            Errors.Add(FString::Printf(TEXT("Budget.%s must be in [1, %d], got %d."), Name, Maximum, Value));
        }
    }

    /** 端点错误和端口错误共用完整定位信息，避免制作时只能猜测出错边。 */
    FString DescribeEdge(const FAbilityGraphEdge& Edge)
    {
        return FString::Printf(TEXT("NodeId %s port '%s' -> NodeId %s"), *Edge.From.ToString(),
                               *Edge.OutputPort.ToString(), *Edge.To.ToString());
    }
} // namespace

bool UAbilityGraphDefinition::Validate(const FAbilityNodeRegistry& Registry, TArray<FString>& Errors) const
{
    Errors.Reset();
    if (!RuntimeCompilationSeal.IsEmpty() && RuntimeCompilationSeal != AbilityRuntimeRecipes::Fingerprint(*this))
    {
        // StartWithInput与再次Compile都先走此校验；删除公开域标记不能将快照误改洗成原helper。
        Errors.Add(
            TEXT("Runtime compiled definition was modified after publication; compile a new private recipe instead."));
        return false;
    }
    if (SchemaVersion != 1 && SchemaVersion != 2)
    {
        Errors.Add(FString::Printf(TEXT("Unsupported SchemaVersion %d; expected 1 or 2."), SchemaVersion));
    }
    const bool bLegacy = SchemaVersion == 2 && FAbilityNodeRegistry::IsLegacyProfile(CompatibilityProfile);
    const bool bLegacyStreaming = BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming && bLegacy;
    const bool bSustained = BudgetPolicy == EAbilityGraphBudgetPolicy::RegisteredSustained;
    const bool bPrototype = BudgetPolicy == EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    const bool bStreaming = bLegacyStreaming || bSustained || bPrototype;
    if (BudgetPolicy != EAbilityGraphBudgetPolicy::Bounded &&
        BudgetPolicy != EAbilityGraphBudgetPolicy::LegacyStreaming && !bSustained && !bPrototype)
    {
        Errors.Add(TEXT("Unknown graph budget policy."));
    }
    if (bSustained && (SchemaVersion != 2 || CompatibilityProfile != TEXT("UnifiedV2")))
    {
        Errors.Add(TEXT("Registered sustained admission requires a Schema2 UnifiedV2 graph."));
    }
    if (bPrototype &&
        (SchemaVersion != 2 || !FAbilityNodeRegistry::IsPrototypeCompatibilityProfile(CompatibilityProfile)))
    {
        Errors.Add(TEXT("Prototype compatibility requires its explicit Schema2 fine profile."));
    }
    if ((SchemaVersion == 1 && CompatibilityProfile != TEXT("UnifiedV1")) ||
        (SchemaVersion == 2 && CompatibilityProfile != TEXT("UnifiedV2") && !bLegacy &&
         !(bPrototype && FAbilityNodeRegistry::IsPrototypeCompatibilityProfile(CompatibilityProfile))))
    {
        Errors.Add(FString::Printf(TEXT("Unsupported CompatibilityProfile '%s'; expected UnifiedV1."),
                                   *CompatibilityProfile.ToString()));
    }
    if (BudgetPolicy == EAbilityGraphBudgetPolicy::LegacyStreaming && !bLegacyStreaming)
    {
        Errors.Add(TEXT("Streaming budget requires Schema2 and an explicitly registered Legacy profile."));
    }
    if (CancelScope != EAbilityGraphCancelScope::Cast)
    {
        Errors.Add(TEXT("Only Cast cancellation scope is implemented by UnifiedV1."));
    }
    const FAbilityGraphBudget Limits;
    CheckBudget(TEXT("MaxInvocations"), Budget.MaxInvocations, Limits.MaxInvocations, Errors);
    CheckBudget(TEXT("MaxEvents"), Budget.MaxEvents, Limits.MaxEvents, Errors);
    CheckBudget(TEXT("MaxActiveNodes"), Budget.MaxActiveNodes,
                bLegacyStreaming || (bPrototype && OriginalProjection.bEnabled) ? 64 : Limits.MaxActiveNodes, Errors);
    CheckBudget(TEXT("MaxDepth"), Budget.MaxDepth, Limits.MaxDepth, Errors);
    CheckBudget(TEXT("MaxEventsPerTick"), Budget.MaxEventsPerTick, Limits.MaxEventsPerTick, Errors);
    const bool bFiniteNodes = Nodes.ContainsByPredicate(
        [](const auto& Node)
        {
            return Node.bRuntimeFinite;
        });
    if (bHasRuntimeFinite != bFiniteNodes || (bHasRuntimeFinite && (!bPrototype && !bSustained)))
    {
        Errors.Add(TEXT("Runtime finite nodes require their explicit aggregate overlay on a registered host."));
    }
    if (bHasRuntimeFinite)
    {
        CheckBudget(TEXT("RuntimeFinite.MaxInvocations"), RuntimeFiniteBudget.MaxInvocations, Limits.MaxInvocations,
                    Errors);
        CheckBudget(TEXT("RuntimeFinite.MaxEvents"), RuntimeFiniteBudget.MaxEvents, Limits.MaxEvents, Errors);
        CheckBudget(TEXT("RuntimeFinite.MaxActiveNodes"), RuntimeFiniteBudget.MaxActiveNodes, Limits.MaxActiveNodes,
                    Errors);
        CheckBudget(TEXT("RuntimeFinite.MaxDepth"), RuntimeFiniteBudget.MaxDepth, Limits.MaxDepth, Errors);
        CheckBudget(TEXT("RuntimeFinite.MaxEventsPerTick"), RuntimeFiniteBudget.MaxEventsPerTick,
                    Limits.MaxEventsPerTick, Errors);
    }
    if (bSustained)
    {
        CheckBudget(TEXT("Sustained.InvocationStartsPerSecond"), SustainedBudget.InvocationStartsPerSecond, 256,
                    Errors);
        CheckBudget(TEXT("Sustained.PhysicalStartsPerSecond"), SustainedBudget.PhysicalStartsPerSecond, 64, Errors);
        CheckBudget(TEXT("Sustained.MaxActivePhysical"), SustainedBudget.MaxActivePhysical, 32, Errors);
    }
    if (bPrototype)
    {
        CheckBudget(TEXT("PrototypeHelper.MaxRetainedInvocations"), PrototypeHelperBudget.MaxRetainedInvocations, 256,
                    Errors);
        CheckBudget(TEXT("PrototypeHelper.MaxActiveNodes"), PrototypeHelperBudget.MaxActiveNodes, 128, Errors);
        CheckBudget(TEXT("PrototypeHelper.MaxEventsPerTick"), PrototypeHelperBudget.MaxEventsPerTick, 8192, Errors);
        CheckBudget(TEXT("PrototypeHelper.MaxActivePhysical"), PrototypeHelperBudget.MaxActivePhysical, 64, Errors);
        CheckBudget(TEXT("PrototypeHelper.PhysicalStartsPerSecond"), PrototypeHelperBudget.PhysicalStartsPerSecond, 64,
                    Errors);
    }
    if (!Errors.IsEmpty())
    {
        return false;
    }
    if (Nodes.IsEmpty())
    {
        Errors.Add(TEXT("Graph must contain at least one node and a valid EntryNodeId."));
        return false;
    }
    if ((!bStreaming && (Nodes.Num() > Budget.MaxInvocations || Edges.Num() >= Budget.MaxInvocations)) ||
        (bStreaming && (Nodes.Num() > 512 || Edges.Num() > 2048)))
    {
        // 可达 DAG 至少执行每个节点一次、每条边一次；先拒绝必超预算输入，限制后续校验内存和工作量。
        Errors.Add(FString::Printf(TEXT("Entry NodeId %s: graph topology exceeds Budget.MaxInvocations %d."),
                                   *EntryNodeId.ToString(), Budget.MaxInvocations));
        return false;
    }

    TMap<FGuid, int32> NodeIndices;
    TArray<const FAbilityNodeDescriptor*> NodeDescriptors;
    NodeDescriptors.SetNumZeroed(Nodes.Num());
    for (int32 Index = 0; Index < Nodes.Num(); ++Index)
    {
        const FAbilityGraphNode& Node = Nodes[Index];
        const FString NodeName = Node.NodeId.ToString();
        if (!Node.NodeId.IsValid())
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s at node index %d is empty."), *NodeName, Index));
        }
        else if (NodeIndices.Contains(Node.NodeId))
        {
            Errors.Add(FString::Printf(TEXT("Duplicate NodeId %s."), *NodeName));
        }
        else
        {
            NodeIndices.Add(Node.NodeId, Index);
        }

        const FAbilityNodeDescriptor* Descriptor = Registry.Find(Node.TypeId);
        NodeDescriptors[Index] = Descriptor;
        if (!Descriptor)
        {
            Errors.Add(
                FString::Printf(TEXT("NodeId %s uses unknown TypeId '%s'."), *NodeName, *Node.TypeId.ToString()));
            continue;
        }
        if (bSustained && !Node.bRuntimeFinite && !Descriptor->bSustainedSafe)
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s TypeId '%s' has no sustained resource contract."), *NodeName,
                                       *Node.TypeId.ToString()));
        }
        if (Node.bRuntimeFinite &&
            (Node.NodeId == EntryNodeId || Node.OriginalIndex != INDEX_NONE ||
             Node.OriginalRole != EAbilityOriginalNodeRole::Helper || !Node.OriginalOutputs.IsEmpty()))
        {
            Errors.Add(TEXT("Runtime finite nodes cannot claim a root or original/helper budget identity."));
        }
        if (Node.bRuntimeFinite && !Descriptor->bRuntimeFiniteSafe)
        {
            Errors.Add(TEXT("Runtime finite strategy has no audited physical/resource contract: ") +
                       Node.TypeId.ToString());
        }
        if (bPrototype && !Node.bRuntimeFinite && !Descriptor->bPrototypeCompatibilitySafe)
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s TypeId '%s' has no prototype helper contract."), *NodeName,
                                       *Node.TypeId.ToString()));
        }
        if (!IsValid(Node.Config) || !Node.Config->IsA(Descriptor->ConfigClass.Get()))
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s TypeId '%s' requires ConfigClass '%s'; got '%s'."), *NodeName,
                                       *Node.TypeId.ToString(), *GetNameSafe(Descriptor->ConfigClass.Get()),
                                       *GetNameSafe(Node.Config.Get())));
            continue;
        }
        FString ConfigError;
        if (!Node.Config->Validate(ConfigError))
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s: %s"), *NodeName, *ConfigError));
        }
        if (!Node.Config->MatchesType(Node.TypeId))
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s: config does not implement registered TypeId %s."), *NodeName,
                                       *Node.TypeId.ToString()));
        }
    }
    if (!EntryNodeId.IsValid() || !NodeIndices.Contains(EntryNodeId))
    {
        Errors.Add(FString::Printf(TEXT("EntryNodeId %s does not identify a valid node."), *EntryNodeId.ToString()));
    }
    else if (bSustained)
    {
        const int32 EntryIndex = NodeIndices.FindChecked(EntryNodeId);
        const auto* Descriptor = NodeDescriptors[EntryIndex];
        FString AdmissionError;
        if (!Descriptor || !Descriptor->bSustainedRoot || !Nodes[EntryIndex].Config ||
            !Nodes[EntryIndex].Config->SupportsSustainedRoot(AdmissionError))
        {
            Errors.Add(TEXT("Entry has no registered sustained-root admission: ") + AdmissionError);
        }
    }
    else if (bPrototype)
    {
        const int32 EntryIndex = NodeIndices.FindChecked(EntryNodeId);
        const auto* Descriptor = NodeDescriptors[EntryIndex];
        FString AdmissionError;
        if (!Descriptor || !Descriptor->bPrototypeCompatibilityRoot || !Descriptor->bPrototypeLogicalEntry ||
            !Nodes[EntryIndex].Config ||
            !Nodes[EntryIndex].Config->SupportsPrototypeCompatibilityRoot(CompatibilityProfile, AdmissionError))
        {
            Errors.Add(TEXT("Entry has no registered prototype root admission: ") + AdmissionError);
        }
    }

    AbilityOriginalProjectionValidation::Validate(*this, Registry, Errors);
    AbilityRuntimePayloadPolicy::ValidateGraph(*this, Registry, Errors);
    TArray<TArray<int32>> Outgoing;
    Outgoing.SetNum(Nodes.Num());
    TArray<int32> Indegrees;
    Indegrees.SetNumZeroed(Nodes.Num());
    TSet<FString> SeenEdges;
    for (const FAbilityGraphEdge& Edge : Edges)
    {
        const FString EdgeName = DescribeEdge(Edge);
        const int32* FromIndex = NodeIndices.Find(Edge.From);
        const int32* ToIndex = NodeIndices.Find(Edge.To);
        if (!FromIndex || !ToIndex)
        {
            Errors.Add(EdgeName + TEXT(": edge endpoint is missing or invalid."));
            continue;
        }
        const FAbilityNodeDescriptor* Descriptor = NodeDescriptors[*FromIndex];
        if (!Descriptor || !Descriptor->OutputPorts.Contains(Edge.OutputPort))
        {
            Errors.Add(EdgeName + TEXT(": output port is not declared by the source node."));
            continue;
        }
        if (Edge.OutputPort == TEXT("Cancelled") || Edge.OutputPort == TEXT("Failed"))
        {
            // V1 终止端口只报告诊断，不建立业务执行边；连到 Delay 再接效果也会在此拒绝。
            Errors.Add(EdgeName + TEXT(": terminal ports cannot schedule execution nodes in UnifiedV1."));
            continue;
        }
        if (SchemaVersion == 2)
        {
            const auto* Port = Descriptor->Ports.FindByPredicate(
                [&](const FAbilityPortDescriptor& Item)
                {
                    return Item.Name == Edge.OutputPort;
                });
            const auto* TargetDescriptor = NodeDescriptors[*ToIndex];
            const bool bDropsPreparedInventory =
                Port && TargetDescriptor && Port->Payload == EAbilityPortPayload::PreparedInventory &&
                TargetDescriptor->InputPayload != EAbilityPortPayload::PreparedInventory;
            if (Port && TargetDescriptor &&
                (bDropsPreparedInventory || (TargetDescriptor->InputPayload != EAbilityPortPayload::Origin &&
                                             TargetDescriptor->InputPayload != Port->Payload)))
            {
                Errors.Add(EdgeName +
                           TEXT(": payload cannot satisfy destination input; snapshots are not live targets."));
            }
        }
        if (SeenEdges.Contains(EdgeName))
        {
            Errors.Add(EdgeName + TEXT(": duplicate execution edge."));
            continue;
        }
        SeenEdges.Add(EdgeName);
        Outgoing[*FromIndex].Add(*ToIndex);
        ++Indegrees[*ToIndex];
    }
    if (!Errors.IsEmpty())
    {
        // 不对含重复身份/悬空端点的半份图继续计算，避免错误索引及误报可执行结果。
        return false;
    }

    // Kahn 拓扑排序不递归，覆盖不可达分量；共享子节点由入度归零判定，不会被误当作环。
    TArray<int32> TopologicalOrder;
    for (int32 Index = 0; Index < Nodes.Num(); ++Index)
    {
        if (Indegrees[Index] == 0)
        {
            TopologicalOrder.Add(Index);
        }
    }
    for (int32 Cursor = 0; Cursor < TopologicalOrder.Num(); ++Cursor)
    {
        for (const int32 Child : Outgoing[TopologicalOrder[Cursor]])
        {
            if (--Indegrees[Child] == 0)
            {
                TopologicalOrder.Add(Child);
            }
        }
    }
    if (TopologicalOrder.Num() != Nodes.Num())
    {
        for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        {
            if (Indegrees[Index] > 0)
            {
                Errors.Add(FString::Printf(TEXT("NodeId %s is in a cycle or depends on a cycle."),
                                           *Nodes[Index].NodeId.ToString()));
            }
        }
        return false;
    }

    const int32 EntryIndex = NodeIndices.FindChecked(EntryNodeId);
    TArray<int64> PathCounts;
    PathCounts.SetNumZeroed(Nodes.Num());
    TArray<int32> Depths;
    Depths.SetNumZeroed(Nodes.Num());
    PathCounts[EntryIndex] = 1;
    Depths[EntryIndex] = 1;
    const int64 SaturationLimit = static_cast<int64>(FMath::Max(Budget.MaxInvocations, Budget.MaxEvents)) + 1;
    int64 TotalInvocations = 0;
    for (const int32 Index : TopologicalOrder)
    {
        if (PathCounts[Index] == 0)
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s is unreachable from EntryNodeId %s."),
                                       *Nodes[Index].NodeId.ToString(), *EntryNodeId.ToString()));
            continue;
        }
        const int32 DepthLimit = Nodes[Index].bRuntimeFinite   ? RuntimeFiniteBudget.MaxDepth
                                 : OriginalProjection.bEnabled ? Limits.MaxDepth
                                                               : Budget.MaxDepth;
        if (Depths[Index] > DepthLimit)
        {
            Errors.Add(FString::Printf(TEXT("NodeId %s depth %d exceeds Budget.MaxDepth %d."),
                                       *Nodes[Index].NodeId.ToString(), Depths[Index], Budget.MaxDepth));
        }
        // 同一节点可能由多条分支分别调用；计数饱和在最大相关预算+1，指数路径不会使整数溢出。
        TotalInvocations = FMath::Min(SaturationLimit, TotalInvocations + PathCounts[Index]);
        for (const int32 Child : Outgoing[Index])
        {
            PathCounts[Child] = FMath::Min(SaturationLimit, PathCounts[Child] + PathCounts[Index]);
            const bool bOriginalEntry = OriginalProjection.bEnabled &&
                                        (Nodes[Child].OriginalRole == EAbilityOriginalNodeRole::Entry ||
                                         Nodes[Child].OriginalRole == EAbilityOriginalNodeRole::EntryAndLifetime);
            const bool bFiniteEntry = Nodes[Child].bRuntimeFinite && !Nodes[Index].bRuntimeFinite;
            const int32 ChildDepth =
                bFiniteEntry || (bPrototype && (NodeDescriptors[Child]->bPrototypeLogicalEntry || bOriginalEntry))
                    ? 1
                    : Depths[Index] + 1;
            Depths[Child] = FMath::Max(Depths[Child], ChildDepth);
        }
    }
    if (!bStreaming && TotalInvocations > Budget.MaxInvocations)
    {
        Errors.Add(FString::Printf(TEXT("Entry NodeId %s: expanded calls exceed Budget.MaxInvocations %d."),
                                   *EntryNodeId.ToString(), Budget.MaxInvocations));
    }
    if (!bStreaming && TotalInvocations > Budget.MaxEvents)
    {
        // 首包每次节点调用只接受一个终止事件；Started 不产生额外事件预算。
        Errors.Add(FString::Printf(TEXT("Entry NodeId %s: terminal events exceed Budget.MaxEvents %d."),
                                   *EntryNodeId.ToString(), Budget.MaxEvents));
    }
    // 活动节点峰值随异步完成时序改变，由运行期 MaxActiveNodes 守卫；每帧额度仅限流，不能拒绝可跨帧图。
    return Errors.IsEmpty();
}
