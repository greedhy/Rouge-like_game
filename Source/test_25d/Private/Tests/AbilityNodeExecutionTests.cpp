// 调用链：Automation -> 测试专用策略注册 -> GraphInstance -> 实际 Petrify 组件及资源清理断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityNodeExecutionTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Misc/AutomationTest.h"

namespace
{
    /** 构造不依赖任何内建 TypeId 的可实例化策略描述。 */
    FAbilityNodeDescriptor TestDescriptor()
    {
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = TEXT("Test.RegisteredStrategy");
        Descriptor.ConfigClass = UAbilityDelayNodeConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityNodeExecutionTestStrategy::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bSupportsActorTargets = true;
        Descriptor.Ports = {{TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                            {TEXT("PeriodElapsed"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                            {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                            {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
        return Descriptor;
    }

    /** 每次创建独立上下文，避免测试通过共享策略状态假装调用隔离。 */
    FAbilityNodeInput TestInput(FAbilityGraphTestFixture& F, bool bStream = false)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.Origin = FVector(20, 30, 0);
        auto* Context = NewObject<UAbilityNodeExecutionTestContext>(F.World);
        Context->Targets.Add(F.TargetToken);
        Context->bStream = bStream;
        Input.Context = Context;
        return Input;
    }

    /** Schema2 使用有类型的目标事件驱动状态，Completed 只关闭测试策略。 */
    FAbilityNodeRegistry Setup(FAbilityGraphTestFixture& F, bool bSingle = false)
    {
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        FString Error;
        Registry.Register(TestDescriptor(), Error);
        F.Graph->SchemaVersion = 2;
        F.Graph->CompatibilityProfile = TEXT("UnifiedV2");
        F.Graph->Nodes[0].TypeId = TEXT("Test.RegisteredStrategy");
        F.Graph->Edges[0].OutputPort = TEXT("TargetFound");
        if (bSingle)
        {
            F.Graph->Nodes.SetNum(1);
            F.Graph->Edges.Reset();
        }
        return Registry;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegisteredNodeRuns, "test_25d.UnifiedGA.Execution.RegisteredStrategyActuallyRuns",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 新 TypeId 仅注册即可执行并施加真实状态；注册表生命周期不限制运行图。 */
bool FRegisteredNodeRuns::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FAbilityNodeInput Input = TestInput(F);
    auto* Context = CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get());
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    {
        auto Registry = Setup(F);
        TestTrue(TEXT("Custom strategy starts from registry"),
                 Run->StartWithInput(F.Graph, Input, {}, Registry, Error));
    }
    Run->Advance(.3f);
    TestTrue(TEXT("Registered implementation applied actual petrify"), F.IsPetrified());
    TestEqual(TEXT("Strategy constructed once"), Context->Starts, 1);
    TestEqual(TEXT("Strategy stopped once"), Context->Stops, 1);
    TestEqual(TEXT("Stop cannot publish after terminal gate"), Context->ReentrantAccepted, 0);
    TestTrue(TEXT("Graph completed"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissingNodeExecutor, "test_25d.UnifiedGA.Execution.MissingExecutorRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 声明必须同时具备配置、执行类和有类型端口。 */
bool FMissingNodeExecutor::RunTest(const FString&)
{
    FAbilityNodeRegistry Registry;
    auto Descriptor = TestDescriptor();
    Descriptor.ExecutionClass = nullptr;
    FString Error;
    TestFalse(TEXT("Description without implementation rejected"), Registry.Register(Descriptor, Error));
    Descriptor.ExecutionClass = UAbilityNodeExecution::StaticClass();
    TestFalse(TEXT("Abstract executor rejected"), Registry.Register(Descriptor, Error));
    TestNull(TEXT("Invalid registration leaves no entry"), Registry.Find(Descriptor.TypeId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeStateIsolation, "test_25d.UnifiedGA.Execution.InvocationStateIsIsolated",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 共享配置的两个施法拥有不同等待时钟，不会被另一施法推进。 */
bool FNodeStateIsolation::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F, true);
    auto InputA = TestInput(F);
    auto InputB = TestInput(F);
    auto* RunA = NewObject<UAbilityGraphInstance>(F.World);
    auto* RunB = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("First starts"), RunA->StartWithInput(F.Graph, InputA, {}, Registry, Error));
    TestTrue(TEXT("Second starts"), RunB->StartWithInput(F.Graph, InputB, {}, Registry, Error));
    RunA->Advance(.15f);
    RunB->Advance(.15f);
    TestFalse(TEXT("First wait independent"), RunA->IsFinished());
    TestFalse(TEXT("Second wait independent"), RunB->IsFinished());
    RunA->Advance(.11f);
    TestTrue(TEXT("Only first reaches deadline"), RunA->IsFinished());
    TestFalse(TEXT("Second clock unchanged"), RunB->IsFinished());
    RunB->Cancel();
    TestTrue(TEXT("Execution identities differ"), RunA->GetExecutionId() != RunB->GetExecutionId());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeMultipleTargets, "test_25d.UnifiedGA.Events.MultipleTargetsAndSequences",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同调用多目标均独立触发后继，重复事件拒绝且不提前结束父调用。 */
bool FNodeMultipleTargets::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F);
    auto Input = TestInput(F);
    auto* Second = F.World->SpawnActor<AEnemyCharacter>();
    Second->Tags.AddUnique(TEXT("Enemy"));
    Second->GetAbilitySystemComponent()->InitAbilityActorInfo(Second, Second);
    Second->GetAbilitySystemComponent()->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Second));
    FAbilityTargetToken SecondToken;
    FString Error;
    TestTrue(TEXT("Second life captured"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget::FromActor(Second),
                                                     SecondToken, Error));
    CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get())->Targets.Add(SecondToken);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Targets = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            TestFalse(TEXT("Event replay rejected"), Run->EnqueueEvent(Event));
            if (Event.OutputPort == TEXT("TargetFound"))
            {
                ++Targets;
            }
        });
    TestTrue(TEXT("Multi-target graph starts"), Run->StartWithInput(F.Graph, Input, {}, Registry, Error));
    Run->Advance(.3f);
    TestEqual(TEXT("Both target events delivered"), Targets, 2);
    TestTrue(TEXT("First actual target controlled"), F.IsPetrified());
    TestTrue(TEXT("Second actual target controlled"), Second->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Graph completes once outputs drain"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeObserverCancel, "test_25d.UnifiedGA.Events.CancelDuringObserver",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 观察第一个非终态事件时取消，所有已排队目标和后继都关闭。 */
bool FNodeObserverCancel::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F);
    auto Input = TestInput(F);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Run->Cancel();
        });
    FString Error;
    TestTrue(TEXT("Graph starts"), Run->StartWithInput(F.Graph, Input, {}, Registry, Error));
    Run->Advance(.3f);
    TestFalse(TEXT("Cancellation suppresses downstream state"), F.IsPetrified());
    TestTrue(TEXT("Terminal reason remains cancellation"), Run->GetEndReason() == EAbilityGraphEndReason::Cancelled);
    TestEqual(TEXT("Stop remains once"), CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get())->Stops, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotNotLive, "test_25d.UnifiedGA.Events.SnapshotCannotFeedPetrify",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Origin/确认快照不能在拓扑或入口处提升为活目标令牌。 */
bool FSnapshotNotLive::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F);
    F.Graph->Edges[0].OutputPort = TEXT("Completed");
    TArray<FString> Errors;
    TestFalse(TEXT("Origin cannot wire into live-target state"), F.Graph->Validate(Registry, Errors));
    F.Graph->Nodes.RemoveAt(0);
    F.Graph->EntryNodeId = F.Graph->Nodes[0].NodeId;
    F.Graph->Edges.Reset();
    auto Input = TestInput(F);
    FAbilityImpactSnapshot Impact;
    Impact.ConfirmedTarget = F.TargetToken;
    Input.Impact = Impact;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestFalse(TEXT("Impact identity alone cannot start Petrify"),
              Run->StartWithInput(F.Graph, Input, {}, Registry, Error));
    TestFalse(TEXT("No effect after rejected input"), F.IsPetrified());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNodeStreamingBudget, "test_25d.UnifiedGA.Events.StreamingKeepsPhysicalBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 明确兼容策略可累计超过256事件；队列与节点仍受限，累计计数从不重置。 */
bool FNodeStreamingBudget::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F, true);
    auto Input = TestInput(F, true);
    F.Graph->CompatibilityProfile = TEXT("LegacyActor.v1");
    F.Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Explicit streaming profile accepted"),
             Run->StartWithInput(F.Graph, Input, Options, Registry, Error));
    for (int32 Index = 0; Index < 300; ++Index)
    {
        Run->Advance(.3f);
    }
    TestFalse(TEXT("Long-lived root remains active"), Run->IsFinished());
    TestEqual(TEXT("All accepted events remain counted"), Run->GetAcceptedEventCount(), uint64(300));
    TestEqual(TEXT("Only one active strategy"), Run->GetActiveInvocationCount(), 1);
    Run->Cancel();
    TestEqual(TEXT("Stop blocks reentrant event"),
              CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get())->ReentrantAccepted, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSchema2InitialQueue, "test_25d.UnifiedGA.Events.Schema2InitialQueueOrdering",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 固定启动帧的迭代队列轨迹；这是新协议顺序，不把它标为旧递归子执行的相同轨迹。 */
bool FSchema2InitialQueue::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = Setup(F);
    auto Input = TestInput(F);
    CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get())->bImmediateStart = true;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TArray<FName> Order;
    FGuid RootInvocation;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("TargetFound"))
            {
                RootInvocation = Event.NodeInvocationId;
                TestFalse(TEXT("Observation precedes queued child action"), F.IsPetrified());
                Order.Add(TEXT("TargetFound"));
            }
            else
            {
                Order.Add(Event.NodeInvocationId == RootInvocation ? TEXT("RootCompleted") : TEXT("ChildCompleted"));
            }
        });
    FString Error;
    TestTrue(TEXT("Synchronous strategy starts"), Run->StartWithInput(F.Graph, Input, {}, Registry, Error));
    TestTrue(TEXT("Initial child effect applies before Start returns"), F.IsPetrified());
    const TArray<FName> Expected = {TEXT("TargetFound"), TEXT("RootCompleted"), TEXT("ChildCompleted")};
    TestTrue(TEXT("Declared FIFO initial event trace is stable"), Order == Expected);
    TestTrue(TEXT("Initial queue finished within budget"), Run->IsFinished());
    return true;
}
#endif
