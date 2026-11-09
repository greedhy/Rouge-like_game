// 调用链：Automation -> Schema2 流式策略 -> 子生成准入/来源和目标分域校验。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityNodeExecutionTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Misc/AutomationTest.h"

namespace
{
    /** 独立测试注册表支持位置型的持续事件，可连接同类子节点。 */
    FAbilityNodeRegistry StreamingGraph(FAbilityGraphTestFixture& F)
    {
        FAbilityNodeRegistry Registry;
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = TEXT("Test.Streaming");
        Descriptor.ConfigClass = UAbilityDelayNodeConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityNodeExecutionTestStrategy::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.Ports = {{TEXT("PeriodElapsed"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                            {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        Registry.Register(Descriptor, Error);
        F.Graph->SchemaVersion = 2;
        F.Graph->CompatibilityProfile = TEXT("LegacyActor.v1");
        F.Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        F.Graph->Nodes.SetNum(1);
        F.Graph->Nodes[0].TypeId = Descriptor.TypeId;
        F.Graph->Edges.Reset();
        return Registry;
    }

    FAbilityNodeInput StreamInput(FAbilityGraphTestFixture& F)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        auto* Context = NewObject<UAbilityNodeExecutionTestContext>(F.World);
        Context->bStream = true;
        Input.Context = Context;
        return Input;
    }

    FAbilityExecutionOptions StreamOptions()
    {
        FAbilityExecutionOptions Options;
        Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        return Options;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreamingQueueBound, "test_25d.UnifiedGA.Events.StreamingQueueBound",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 累计事件允许增长不等于允许瞬时队列无限增长。 */
bool FStreamingQueueBound::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = StreamingGraph(F);
    auto Input = StreamInput(F);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FGuid InvocationId;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            InvocationId = Event.NodeInvocationId;
        });
    FString Error;
    TestTrue(TEXT("Start streaming"), Run->StartWithInput(F.Graph, Input, StreamOptions(), Registry, Error));
    Run->Advance(.3f);
    for (int32 Index = 0; Index < 256; ++Index)
    {
        TestTrue(TEXT("Queue admits within hard bound"), Run->Publish(InvocationId, TEXT("PeriodElapsed"), Input));
    }
    TestFalse(TEXT("Queue rejects event 257 before next tick"),
              Run->Publish(InvocationId, TEXT("PeriodElapsed"), Input));
    TestTrue(TEXT("Physical overflow cancels graph"), Run->GetEndReason() == EAbilityGraphEndReason::BudgetExceeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChildAdmissionBound, "test_25d.UnifiedGA.Events.ChildAdmissionReservesBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 子生成次数只在额度已预留时增长；旧生成拒绝不能取消持续主行为。 */
bool FChildAdmissionBound::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = StreamingGraph(F);
    FAbilityGraphNode Child = F.Graph->Nodes[0];
    Child.NodeId = FGuid::NewGuid();
    Child.Config = DuplicateObject<UAbilityNodeConfig>(Child.Config, F.Graph);
    F.Graph->Nodes.Add(Child);
    FAbilityGraphEdge Edge;
    Edge.From = F.Graph->EntryNodeId;
    Edge.To = Child.NodeId;
    Edge.OutputPort = TEXT("PeriodElapsed");
    F.Graph->Edges.Add(Edge);
    F.Graph->Budget.MaxInvocations = 2;
    F.Graph->Budget.MaxActiveNodes = 2;
    auto Input = StreamInput(F);
    auto* Context = CastChecked<UAbilityNodeExecutionTestContext>(Input.Context.Get());
    Context->bRequestChildren = true;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Start root and child topology"),
             Run->StartWithInput(F.Graph, Input, StreamOptions(), Registry, Error));
    Run->Advance(.3f);
    TestEqual(TEXT("One admission has actual reserved child"), Context->AcceptedChildren, 1);
    TestEqual(TEXT("Reserved child constructed"), Context->Starts, 2);
    Run->Advance(.3f);
    TestEqual(TEXT("Further spawn rejected without false acknowledgement"), Context->AcceptedChildren, 1);
    TestFalse(TEXT("Existing root and child survive spawn rejection"), Run->IsFinished());
    Run->Cancel();
    TestEqual(TEXT("Both owned strategies stop"), Context->Stops, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPositionSurvivesTarget, "test_25d.UnifiedGA.Events.PositionInputSurvivesLostTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 位置行为不依赖过期的可选目标，但所有行为仍受原来源生命约束。 */
bool FPositionSurvivesTarget::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Registry = StreamingGraph(F);
    auto Input = StreamInput(F);
    Input.LiveTargetToken = F.TargetToken;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Origin input accepts optional live target"),
             Run->StartWithInput(F.Graph, Input, StreamOptions(), Registry, Error));
    F.Target->Destroy();
    Run->Advance(.3f);
    TestFalse(TEXT("Lost optional target does not cancel origin behavior"), Run->IsFinished());
    TestEqual(TEXT("Origin event still emitted"), Run->GetAcceptedEventCount(), uint64(1));
    F.ASC->InitAbilityActorInfo(F.Source, F.World->SpawnActor<AEnemyCharacter>());
    Run->Advance(.3f);
    TestTrue(TEXT("Source replacement closes independent behavior"),
             Run->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    return true;
}
#endif
