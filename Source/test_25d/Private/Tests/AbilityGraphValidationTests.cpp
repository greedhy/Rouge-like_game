// 调用链：Automation test_25d.UnifiedGA.Validation -> 临时图/注册表 -> Validate/Register。
// 测试只创建内存配置，不加载内容资产，不启动图执行，也不修改共享 DA。
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    /** 添加带独立稳定身份的 Delay 配置，供链、分叉和环的真实验证输入使用。 */
    FGuid AddDelay(UAbilityGraphDefinition& Graph)
    {
        FAbilityGraphNode& Node = Graph.Nodes.AddDefaulted_GetRef();
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Control.Delay");
        Node.Config = NewObject<UAbilityDelayNodeConfig>(&Graph);
        return Node.NodeId;
    }

    /** V1 每条边沿用同一捕获目标；测试只改变拓扑和输出端口。 */
    void AddEdge(UAbilityGraphDefinition& Graph, FGuid From, FGuid To, FName Port = TEXT("Completed"))
    {
        FAbilityGraphEdge& Edge = Graph.Edges.AddDefaulted_GetRef();
        Edge.From = From;
        Edge.To = To;
        Edge.OutputPort = Port;
    }

    /** 创建合法的 0.25 秒等待 -> 1 秒石化；配置全部由临时图拥有。 */
    UAbilityGraphDefinition* MakeGraph()
    {
        UAbilityGraphDefinition* Graph = NewObject<UAbilityGraphDefinition>();
        Graph->EntryNodeId = AddDelay(*Graph);
        FAbilityGraphNode& Petrify = Graph->Nodes.AddDefaulted_GetRef();
        Petrify.NodeId = FGuid::NewGuid();
        Petrify.TypeId = TEXT("Status.Petrify");
        Petrify.Config = NewObject<UAbilityPetrifyNodeConfig>(Graph);
        AddEdge(*Graph, Graph->EntryNodeId, Petrify.NodeId);
        return Graph;
    }

    /** 用真实内建注册表校验；每次调用都应替换错误数组而不是混入上次结果。 */
    bool ValidateGraph(const UAbilityGraphDefinition& Graph, TArray<FString>& Errors)
    {
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        return Graph.Validate(Registry, Errors);
    }

    /** 错误允许改进文字，但必须保留可定位的身份、端口或预算字段。 */
    bool HasDiagnostic(const TArray<FString>& Errors, const FString& Fragment)
    {
        return Errors.ContainsByPredicate(
            [&Fragment](const FString& Error)
            {
                return Error.Contains(Fragment);
            });
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphValidReadOnly, "test_25d.UnifiedGA.Validation.ValidGraphReadOnly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获漏做校验结果清理、原地修正配置以及合法图被误拒绝。 */
bool FUnifiedGraphValidReadOnly::RunTest(const FString&)
{
    UAbilityGraphDefinition* Graph = MakeGraph();
    const FGuid OriginalEntry = Graph->EntryNodeId;
    const UObject* OriginalConfig = Graph->Nodes[0].Config;
    TArray<FString> Errors = {TEXT("stale error")};
    TestTrue(TEXT("Delay -> Petrify is valid"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Successful validation clears errors"), Errors.IsEmpty());
    TestEqual(TEXT("Entry is unchanged"), Graph->EntryNodeId, OriginalEntry);
    TestEqual(TEXT("Config identity is unchanged"), static_cast<const UObject*>(Graph->Nodes[0].Config),
              OriginalConfig);
    TestEqual(TEXT("Delay is unchanged"), CastChecked<UAbilityDelayNodeConfig>(Graph->Nodes[0].Config)->DelaySeconds,
              0.25f);
    TestEqual(TEXT("Duration is unchanged"),
              CastChecked<UAbilityPetrifyNodeConfig>(Graph->Nodes[1].Config)->DurationSeconds, 1.0f);
    TestEqual(TEXT("Topology is unchanged"), Graph->Edges.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphRegistry,
                                 "test_25d.UnifiedGA.Validation.RegistryRejectsInvalidDeclarations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获覆盖重复注册、空配置类及虚构端口被接受的错误。 */
bool FUnifiedGraphRegistry::RunTest(const FString&)
{
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    const FAbilityNodeDescriptor* Delay = Registry.Find(TEXT("Control.Delay"));
    const FAbilityNodeDescriptor* Petrify = Registry.Find(TEXT("Status.Petrify"));
    if (!TestNotNull(TEXT("Delay registered"), Delay) || !TestNotNull(TEXT("Petrify registered"), Petrify))
    {
        return false;
    }
    TestTrue(TEXT("Delay carries live Actor and Mass targets"),
             Delay->bSupportsActorTargets && Delay->bSupportsMassTargets);
    TestFalse(TEXT("Delay cannot turn a location into a live target"), Delay->bSupportsSnapshotTargets);
    TestTrue(TEXT("Delay owns an asynchronous wait"), Delay->bIsAsync);
    TestFalse(TEXT("Petrify applies synchronously; state service owns duration"), Petrify->bIsAsync);
    TestTrue(TEXT("Delay advertises cancellation"), Delay->OutputPorts.Contains(TEXT("Cancelled")));
    TestFalse(TEXT("Petrify does not advertise unimplemented cancellation"),
              Petrify->OutputPorts.Contains(TEXT("Cancelled")));
    TestNull(TEXT("Legacy GA is not implicitly composable"), Registry.Find(TEXT("LegacyEntry")));
    FString Error;
    FAbilityNodeDescriptor Candidate = *Delay;
    TestFalse(TEXT("Duplicate TypeId is rejected"), Registry.Register(Candidate, Error));
    TestTrue(TEXT("Duplicate error identifies TypeId"), Error.Contains(TEXT("Control.Delay")));
    TestEqual(TEXT("Duplicate does not replace descriptor"), Registry.Find(Candidate.TypeId)->ConfigClass.Get(),
              UAbilityDelayNodeConfig::StaticClass());
    Candidate.TypeId = NAME_None;
    TestFalse(TEXT("Empty TypeId rejected"), Registry.Register(Candidate, Error));
    Candidate.TypeId = TEXT("Test.Invalid");
    Candidate.ConfigClass = nullptr;
    TestFalse(TEXT("Missing ConfigClass rejected"), Registry.Register(Candidate, Error));
    Candidate.ConfigClass = UAbilityNodeConfig::StaticClass();
    TestFalse(TEXT("Abstract ConfigClass rejected"), Registry.Register(Candidate, Error));
    Candidate.ConfigClass = UAbilityDelayNodeConfig::StaticClass();
    Candidate.OutputPorts.Add(TEXT("Completed"));
    TestFalse(TEXT("Duplicate port rejected"), Registry.Register(Candidate, Error));
    Candidate.OutputPorts = {NAME_None};
    TestFalse(TEXT("Empty port rejected"), Registry.Register(Candidate, Error));
    Candidate.OutputPorts.Reset();
    Candidate.Ports.Reset();
    TestFalse(TEXT("Missing ports rejected"), Registry.Register(Candidate, Error));
    Candidate.OutputPorts = {TEXT("Completed")};
    Candidate.bSupportsActorTargets = false;
    Candidate.bSupportsMassTargets = false;
    TestFalse(TEXT("Missing target support rejected"), Registry.Register(Candidate, Error));
    TestNull(TEXT("Invalid registration never inserts partial descriptor"), Registry.Find(Candidate.TypeId));
    Candidate.bSupportsActorTargets = true;
    TestTrue(TEXT("Complete descriptor can be registered"), Registry.Register(Candidate, Error));
    TestTrue(TEXT("Successful registration clears stale error"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphNodeTypes, "test_25d.UnifiedGA.Validation.NodeTypeAndConfig",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获未知 TypeId、空配置或错误配置类型在执行前漏检。 */
bool FUnifiedGraphNodeTypes::RunTest(const FString&)
{
    TArray<FString> Errors;
    UAbilityGraphDefinition* Graph = MakeGraph();
    Graph->Nodes[0].TypeId = TEXT("Missing.Node");
    TestFalse(TEXT("Unknown TypeId rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Unknown type error identifies NodeId"), HasDiagnostic(Errors, Graph->Nodes[0].NodeId.ToString()));
    Graph->Nodes[0].TypeId = TEXT("Control.Delay");
    Graph->Nodes[0].Config = NewObject<UAbilityPetrifyNodeConfig>(Graph);
    TestFalse(TEXT("Mismatched ConfigClass rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Mismatch identifies NodeId"), HasDiagnostic(Errors, Graph->Nodes[0].NodeId.ToString()));
    Graph->Nodes[0].Config = nullptr;
    TestFalse(TEXT("Missing config rejected"), ValidateGraph(*Graph, Errors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphNodeIdentity, "test_25d.UnifiedGA.Validation.NodeIdentityAndEntry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获空/重复节点身份以及悬空入口，避免运行时依赖数组顺序。 */
bool FUnifiedGraphNodeIdentity::RunTest(const FString&)
{
    TArray<FString> Errors;
    UAbilityGraphDefinition* Graph = MakeGraph();
    Graph->Nodes[1].NodeId = Graph->Nodes[0].NodeId;
    TestFalse(TEXT("Duplicate NodeId rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Duplicate error identifies NodeId"), HasDiagnostic(Errors, Graph->Nodes[0].NodeId.ToString()));
    Graph = MakeGraph();
    Graph->Nodes[1].NodeId.Invalidate();
    TestFalse(TEXT("Empty NodeId rejected"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    Graph->EntryNodeId = FGuid::NewGuid();
    TestFalse(TEXT("Missing entry rejected"), ValidateGraph(*Graph, Errors));
    Graph->EntryNodeId.Invalidate();
    TestFalse(TEXT("Empty entry rejected"), ValidateGraph(*Graph, Errors));
    Graph->Nodes.Reset();
    Graph->Edges.Reset();
    TestFalse(TEXT("Empty graph rejected"), ValidateGraph(*Graph, Errors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphEdgeContract, "test_25d.UnifiedGA.Validation.EdgeIdentityAndPorts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获不存在的边端点和端口，错误必须同时提供源节点与端口定位。 */
bool FUnifiedGraphEdgeContract::RunTest(const FString&)
{
    TArray<FString> Errors;
    UAbilityGraphDefinition* Graph = MakeGraph();
    Graph->Edges[0].From = FGuid::NewGuid();
    TestFalse(TEXT("Missing edge source rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Edge error identifies source"), HasDiagnostic(Errors, Graph->Edges[0].From.ToString()));
    TestTrue(TEXT("Edge error identifies port"), HasDiagnostic(Errors, TEXT("Completed")));
    Graph = MakeGraph();
    Graph->Edges[0].To = FGuid::NewGuid();
    TestFalse(TEXT("Missing edge destination rejected"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    Graph->Edges[0].OutputPort = TEXT("HitConfirmed");
    TestFalse(TEXT("Unimplemented output port rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Unknown port error identifies NodeId"), HasDiagnostic(Errors, Graph->EntryNodeId.ToString()));
    TestTrue(TEXT("Unknown port error identifies port"), HasDiagnostic(Errors, TEXT("HitConfirmed")));
    Graph->Edges[0].OutputPort = NAME_None;
    TestFalse(TEXT("Empty output port rejected"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    AddEdge(*Graph, Graph->EntryNodeId, Graph->Nodes[1].NodeId);
    TestFalse(TEXT("Duplicate edge cannot silently double an effect"), ValidateGraph(*Graph, Errors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphTerminalPorts,
                                 "test_25d.UnifiedGA.Validation.TerminalPortsCannotApplyEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获取消/失败后补效果，包括试图用 Delay 间接进入效果节点。 */
bool FUnifiedGraphTerminalPorts::RunTest(const FString&)
{
    TArray<FString> Errors;
    for (const FName Port : {FName(TEXT("Cancelled")), FName(TEXT("Failed"))})
    {
        UAbilityGraphDefinition* Graph = MakeGraph();
        Graph->Edges[0].OutputPort = Port;
        TestFalse(TEXT("Terminal event cannot invoke effect"), ValidateGraph(*Graph, Errors));
        TestTrue(TEXT("Terminal edge diagnostic identifies port"), HasDiagnostic(Errors, Port.ToString()));
        const FGuid Delay = AddDelay(*Graph);
        Graph->Edges[0].To = Delay;
        AddEdge(*Graph, Delay, Graph->Nodes[1].NodeId);
        TestFalse(TEXT("Terminal event cannot indirectly invoke effect"), ValidateGraph(*Graph, Errors));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphTopology, "test_25d.UnifiedGA.Validation.CyclesAndUnreachableNodes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获环路、孤立节点与不可达环；验证必须对整图关闭失败。 */
bool FUnifiedGraphTopology::RunTest(const FString&)
{
    TArray<FString> Errors;
    UAbilityGraphDefinition* Graph = MakeGraph();
    AddEdge(*Graph, Graph->Nodes[1].NodeId, Graph->EntryNodeId);
    TestFalse(TEXT("Back edge rejected"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    AddEdge(*Graph, Graph->EntryNodeId, Graph->EntryNodeId);
    TestFalse(TEXT("Self cycle rejected"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    const FGuid Isolated = AddDelay(*Graph);
    TestFalse(TEXT("Unreachable node rejected"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Unreachable error identifies node"), HasDiagnostic(Errors, Isolated.ToString()));
    AddEdge(*Graph, Isolated, Isolated);
    TestFalse(TEXT("Unreachable cycle rejected without recursion"), ValidateGraph(*Graph, Errors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphSharedChild, "test_25d.UnifiedGA.Validation.SharedChildDag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 菱形 DAG 的共享子节点不是环；预算须把两次抵达计作两次调用。 */
bool FUnifiedGraphSharedChild::RunTest(const FString&)
{
    UAbilityGraphDefinition* Graph = MakeGraph();
    const FGuid Left = AddDelay(*Graph);
    const FGuid Right = AddDelay(*Graph);
    const FGuid Join = AddDelay(*Graph);
    Graph->Edges.Reset();
    AddEdge(*Graph, Graph->EntryNodeId, Left);
    AddEdge(*Graph, Graph->EntryNodeId, Right);
    AddEdge(*Graph, Left, Join);
    AddEdge(*Graph, Right, Join);
    AddEdge(*Graph, Join, Graph->Nodes[1].NodeId);
    TArray<FString> Errors;
    Graph->Budget.MaxInvocations = 7;
    TestTrue(TEXT("Two branches may invoke one shared child"), ValidateGraph(*Graph, Errors));
    Graph->Budget.MaxInvocations = 6;
    TestFalse(TEXT("Join and effect execute twice and exceed six-call budget"), ValidateGraph(*Graph, Errors));
    TestTrue(TEXT("Expanded count diagnostic identifies budget"), HasDiagnostic(Errors, TEXT("MaxInvocations")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphDurations, "test_25d.UnifiedGA.Validation.FinitePositiveDurations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获 NaN/Inf 绕过大小比较，以及零/负等待和持续时间。 */
bool FUnifiedGraphDurations::RunTest(const FString&)
{
    TArray<FString> Errors;
    for (const float Invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        UAbilityGraphDefinition* Graph = MakeGraph();
        CastChecked<UAbilityDelayNodeConfig>(Graph->Nodes[0].Config)->DelaySeconds = Invalid;
        TestFalse(TEXT("Invalid DelaySeconds rejected"), ValidateGraph(*Graph, Errors));
        TestTrue(TEXT("Delay diagnostic identifies node"), HasDiagnostic(Errors, Graph->Nodes[0].NodeId.ToString()));
        Graph = MakeGraph();
        CastChecked<UAbilityPetrifyNodeConfig>(Graph->Nodes[1].Config)->DurationSeconds = Invalid;
        TestFalse(TEXT("Invalid DurationSeconds rejected"), ValidateGraph(*Graph, Errors));
        TestTrue(TEXT("Duration diagnostic identifies node"), HasDiagnostic(Errors, Graph->Nodes[1].NodeId.ToString()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphBudgets, "test_25d.UnifiedGA.Validation.Budgets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获禁用预算、节点调用/事件总量溢出和超深度；不把每帧限流误当成总量上限。 */
bool FUnifiedGraphBudgets::RunTest(const FString&)
{
    TArray<FString> Errors;
    for (int32 FAbilityGraphBudget::* Field :
         {&FAbilityGraphBudget::MaxInvocations, &FAbilityGraphBudget::MaxEvents, &FAbilityGraphBudget::MaxActiveNodes,
          &FAbilityGraphBudget::MaxDepth, &FAbilityGraphBudget::MaxEventsPerTick})
    {
        UAbilityGraphDefinition* Graph = MakeGraph();
        Graph->Budget.*Field = 0;
        TestFalse(TEXT("Zero budget rejected"), ValidateGraph(*Graph, Errors));
        Graph->Budget.*Field = -1;
        TestFalse(TEXT("Negative budget rejected"), ValidateGraph(*Graph, Errors));
    }
    const TPair<int32 FAbilityGraphBudget::*, int32> OverLimits[] = {{&FAbilityGraphBudget::MaxInvocations, 65},
                                                                     {&FAbilityGraphBudget::MaxEvents, 257},
                                                                     {&FAbilityGraphBudget::MaxActiveNodes, 33},
                                                                     {&FAbilityGraphBudget::MaxDepth, 9},
                                                                     {&FAbilityGraphBudget::MaxEventsPerTick, 65}};
    for (const auto& OverLimit : OverLimits)
    {
        UAbilityGraphDefinition* OverBudget = MakeGraph();
        OverBudget->Budget.*OverLimit.Key = OverLimit.Value;
        TestFalse(TEXT("Budget above V1 hard limit rejected"), ValidateGraph(*OverBudget, Errors));
    }
    UAbilityGraphDefinition* Graph = MakeGraph();
    Graph->Budget.MaxInvocations = 1;
    TestFalse(TEXT("Two-node chain exceeds invocation budget"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    Graph->Budget.MaxEvents = 1;
    TestFalse(TEXT("Two completions exceed event budget"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    Graph->Budget.MaxDepth = 1;
    TestFalse(TEXT("Two-node chain exceeds depth budget"), ValidateGraph(*Graph, Errors));
    Graph = MakeGraph();
    Graph->Budget.MaxActiveNodes = 1;
    Graph->Budget.MaxEventsPerTick = 1;
    TestTrue(TEXT("Sequential graph can progress one event and one active node at a time"),
             ValidateGraph(*Graph, Errors));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphVersionScope, "test_25d.UnifiedGA.Validation.VersionAndCancellationScope",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 捕获未实现 schema/兼容模式/取消范围被静默解释为 V1。 */
bool FUnifiedGraphVersionScope::RunTest(const FString&)
{
    TArray<FString> Errors;
    UAbilityGraphDefinition* Graph = MakeGraph();
    Graph->SchemaVersion = 2;
    TestFalse(TEXT("Unknown schema rejected"), ValidateGraph(*Graph, Errors));
    Graph->SchemaVersion = 1;
    Graph->CompatibilityProfile = TEXT("LegacyActor");
    TestFalse(TEXT("Unknown compatibility profile rejected"), ValidateGraph(*Graph, Errors));
    Graph->CompatibilityProfile = TEXT("UnifiedV1");
    for (const EAbilityGraphCancelScope Scope : {EAbilityGraphCancelScope::Node, EAbilityGraphCancelScope::Subtree,
                                                 static_cast<EAbilityGraphCancelScope>(255)})
    {
        Graph->CancelScope = Scope;
        TestFalse(TEXT("Unimplemented cancellation scope rejected"), ValidateGraph(*Graph, Errors));
    }
    return true;
}
#endif
