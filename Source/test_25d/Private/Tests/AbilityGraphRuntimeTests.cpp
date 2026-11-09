// 调用链：Automation -> FAbilityGraphTestFixture -> Graph Start/Advance/Cancel -> 真实石化组件。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeDelay, "test_25d.UnifiedGA.Runtime.DelaySnapshotAndDedup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 验证真实延迟、DA 快照、同调用去重、大帧与完成回调重入。 */
bool FAbilityGraphRuntimeDelay::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Events = 0;
    int32 Finishes = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            ++Events;
            TestFalse(TEXT("Replay of accepted invocation/sequence is rejected"), Run->EnqueueEvent(Event));
        });
    Run->OnFinished.AddLambda(
        [&](EAbilityGraphEndReason Reason)
        {
            ++Finishes;
            Run->Cancel(EAbilityGraphEndReason::Cancelled);
        });
    FString Error;
    TestTrue(TEXT("Valid graph starts"), Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    Cast<UAbilityDelayNodeConfig>(F.Graph->Nodes[0].Config)->DelaySeconds = 10.f;
    Run->Advance(.24f);
    TestFalse(TEXT("No early control"), F.IsPetrified());
    Run->Advance(.011f);
    TestTrue(TEXT("Original snapshot delay applies control"), F.IsPetrified());
    Run->Advance(10.f);
    TestEqual(TEXT("One event per node despite replay and large delta"), Events, 2);
    TestEqual(TEXT("Finish broadcasts once under reentrant cancel"), Finishes, 1);
    TestTrue(TEXT("Natural completion retained"), Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeCancel, "test_25d.UnifiedGA.Runtime.CancelAndLifeInvalidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 取消、换 Avatar、目标死亡均禁止延迟效果；每次重新创建运行实例。 */
bool FAbilityGraphRuntimeCancel::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FString Error;
    auto* Cancelled = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Start cancellable graph"), Cancelled->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    Cancelled->Cancel(EAbilityGraphEndReason::Cancelled);
    Cancelled->Advance(1.f);
    TestFalse(TEXT("Cancelled delay cannot apply control"), F.IsPetrified());
    auto* LostSource = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Start before avatar replacement"), LostSource->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    auto* Replacement = F.World->SpawnActor<AEnemyCharacter>();
    F.ASC->InitAbilityActorInfo(F.Source, Replacement);
    LostSource->Advance(1.f);
    TestTrue(TEXT("Avatar replacement invalidates source"),
             LostSource->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    TestFalse(TEXT("Replacement cannot inherit delayed cast"), F.IsPetrified());
    F.ASC->InitAbilityActorInfo(F.Source, F.Source);
    auto* LostTarget = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Start before target removal"), LostTarget->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    F.Target->Destroy();
    LostTarget->Advance(1.f);
    TestTrue(TEXT("Removed target closes graph"), LostTarget->GetEndReason() == EAbilityGraphEndReason::TargetLost);
    auto* Invalid = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Invalid target rejects start"), Invalid->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeReentrantCancel,
                                 "test_25d.UnifiedGA.Runtime.CancelClosesEventQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 节点完成诊断回调同步取消时，后继效果不得继续执行。 */
bool FAbilityGraphRuntimeReentrantCancel::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent&)
        {
            Run->Cancel(EAbilityGraphEndReason::Cancelled);
        });
    FString Error;
    TestTrue(TEXT("Start graph"), Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    Run->Advance(1.f);
    TestFalse(TEXT("No effect after event callback cancelled cast"), F.IsPetrified());
    TestTrue(TEXT("Queue closed"), Run->IsFinished());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeBudgets, "test_25d.UnifiedGA.Runtime.DagInvocationAndBudgets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同一共享节点的不同调用不能误去重；活动节点预算耗尽取消整图，逐帧事件预算保留待办。 */
bool FAbilityGraphRuntimeBudgets::RunTest(const FString&)
{
    for (bool bConstrain : {false, true})
    {
        FAbilityGraphTestFixture F;
        FAbilityGraphNode Branch = F.Graph->Nodes[0];
        Branch.NodeId = FGuid::NewGuid();
        Branch.Config = DuplicateObject<UAbilityNodeConfig>(Branch.Config, F.Graph);
        F.Graph->Nodes.Add(Branch);
        // Entry -> Petrify and Entry -> Delay -> same Petrify: each path is a separate invocation.
        FAbilityGraphEdge Fan;
        Fan.From = F.Graph->EntryNodeId;
        Fan.To = Branch.NodeId;
        Fan.OutputPort = TEXT("Completed");
        F.Graph->Edges.Add(Fan);
        Fan.From = Branch.NodeId;
        Fan.To = F.Graph->Nodes[1].NodeId;
        F.Graph->Edges.Add(Fan);
        F.Graph->Budget.MaxActiveNodes = bConstrain ? 1 : 32;
        F.Graph->Budget.MaxEventsPerTick = 1;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 PetrifyEvents = 0;
        TSet<FGuid> Calls;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Calls.Add(Event.NodeInvocationId);
                ++PetrifyEvents;
            });
        FString Error;
        TestTrue(TEXT("Valid DAG accepted"), Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
        Run->Advance(.251f);
        if (bConstrain)
        {
            TestTrue(TEXT("Fan-out exceeds active cap"), Run->GetEndReason() == EAbilityGraphEndReason::BudgetExceeded);
            TestTrue(TEXT("Partial already-applied target status retains its original owner"), F.IsPetrified());
        }
        else
        {
            TestFalse(TEXT("Per-frame cap leaves events queued"), Run->IsFinished());
            for (int32 Frame = 0; Frame < 4; ++Frame)
            {
                Run->Advance(.251f);
            }
            TestEqual(TEXT("Both invocations of shared config execute"), Calls.Num(), 4);
            TestEqual(TEXT("Events bounded per invocation"), PetrifyEvents, 4);
            TestTrue(TEXT("Real status applied through DAG"), F.IsPetrified());
            TestTrue(TEXT("DAG terminates"), Run->GetEndReason() == EAbilityGraphEndReason::Completed);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeReject, "test_25d.UnifiedGA.Runtime.InvalidStartHasNoEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 非法图与无来源令牌在任何节点执行前失败。 */
bool FAbilityGraphRuntimeReject::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FString Error;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Missing authority identity rejected"),
              Run->Start(F.Graph, FAbilitySourceToken(), F.TargetToken, Error));
    F.Graph->Nodes[0].TypeId = TEXT("Unimplemented.Node");
    TestFalse(TEXT("Unknown graph cannot start"), Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    Run->Advance(1.f);
    TestFalse(TEXT("Failed start never applies status"), F.IsPetrified());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphRuntimeDiagnostics, "test_25d.UnifiedGA.Runtime.TerminalDiagnostics",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 注册表宣称的终态端口必须可观察，但关闭后不会被重新排队或触发后继。 */
bool FAbilityGraphRuntimeDiagnostics::RunTest(const FString&)
{
    for (bool bCancel : {false, true})
    {
        FAbilityGraphTestFixture F;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 TerminalEvents = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                ++TerminalEvents;
                TestTrue(TEXT("Terminal diagnostic carries invocation identity"), Event.NodeInvocationId.IsValid());
                TestTrue(TEXT("Business gate already closed during diagnostic"), Run->IsFinished());
                TestEqual(TEXT("Advertised terminal port observed"), Event.OutputPort,
                          bCancel ? FName(TEXT("Cancelled")) : FName(TEXT("Failed")));
                TestFalse(TEXT("Terminal diagnostic cannot be requeued"), Run->EnqueueEvent(Event));
                Run->Cancel();
            });
        FString Error;
        TestTrue(TEXT("Start graph before failure"), Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
        if (bCancel)
        {
            Run->Cancel();
        }
        else
        {
            F.ASC->InitAbilityActorInfo(F.Source, F.World->SpawnActor<AEnemyCharacter>());
            Run->Advance(.3f);
        }
        TestEqual(TEXT("Active node terminal emitted exactly once"), TerminalEvents, 1);
        TestFalse(TEXT("Terminal ports never apply downstream control"), F.IsPetrified());
    }
    return true;
}
#endif
