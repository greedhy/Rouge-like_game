// 测试链：真实图源 -> 异步DelayOrigin -> 真实GE -> 有界continuation；观察属性/回调顺序及取消清理。
#include "AbilityGraphContinuationTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

void UGraphDeferredTestExecution::Advance(float)
{
    if (!bPublished && !bStopped)
    {
        bPublished = true;
        if (auto* Run = GraphOwner.Get())
        {
            Run->Publish(InvocationId, TEXT("Logical"), Input);
            Run->FlushImmediatePayloads(false);
        }
    }
}

bool UGraphContinuationTestExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Remaining = CastChecked<UGraphContinuationTestConfig>(&Config)->Count;
    Next();
    return true;
}

void UGraphContinuationTestExecution::Next()
{
    auto* Run = GraphOwner.Get();
    if (bStopped || !Run || Run->IsFinished())
    {
        return;
    }
    if (Remaining-- <= 0)
    {
        Run->Publish(InvocationId, TEXT("Completed"), Input);
        return;
    }
    const TWeakObjectPtr<UGraphContinuationTestExecution> Weak(this);
    if (!Run->PublishWithContinuation(InvocationId, TEXT("Contact"), Input,
                                      [Weak]()
                                      {
                                          if (auto* Self = Weak.Get())
                                          {
                                              Self->Next();
                                          }
                                      }))
    {
        Run->Cancel(EAbilityGraphEndReason::Failed);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

namespace AbilityGraphContinuationTests
{
    UAbilityGraphDefinition* Build(FAbilityGraphTestFixture& F, FAbilityNodeRegistry& Registry)
    {
        Registry.RegisterBuiltins();
        FAbilityNodeDescriptor Source;
        Source.TypeId = TEXT("Test.Continuation");
        Source.ConfigClass = UGraphContinuationTestConfig::StaticClass();
        Source.ExecutionClass = UGraphContinuationTestExecution::StaticClass();
        Source.InputPayload = EAbilityPortPayload::Origin;
        Source.bSupportsActorTargets = Source.bSupportsMassTargets = Source.bSupportsSnapshotTargets = true;
        Source.bIsAsync = true;
        Source.Ports = {{TEXT("Contact"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        check(Registry.Register(Source, Error));
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Root;
        Root.NodeId = FGuid::NewGuid();
        Root.TypeId = Source.TypeId;
        Root.Config = NewObject<UGraphContinuationTestConfig>(Graph);
        FAbilityGraphNode Delay;
        Delay.NodeId = FGuid::NewGuid();
        Delay.TypeId = TEXT("Control.DelayOrigin");
        auto* DelayConfig = NewObject<UAbilityDelayNodeConfig>(Graph);
        DelayConfig->DelaySeconds = .1f;
        Delay.Config = DelayConfig;
        FAbilityGraphNode Effect;
        Effect.NodeId = FGuid::NewGuid();
        Effect.TypeId = TEXT("Effect.GameplayEffect");
        auto* GE = NewObject<UAbilityGameplayEffectNodeConfig>(Graph);
        GE->EffectClass = UArcaneShardTestDamageEffect::StaticClass();
        Effect.Config = GE;
        Graph->Nodes = {Root, Delay, Effect};
        Graph->EntryNodeId = Root.NodeId;
        Graph->Edges = {{Root.NodeId, Delay.NodeId, TEXT("Contact")}, {Delay.NodeId, Effect.NodeId, TEXT("Completed")}};
        return Graph;
    }
} // namespace AbilityGraphContinuationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphDeferredFlushReceipt,
                                 "test_25d.DynamicMage.Execution.OrdinaryFlushReportsDeferredWorkAtFrameBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphDeferredFlushReceipt::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    FAbilityNodeDescriptor Producer;
    Producer.TypeId = TEXT("Test.DeferredProducer");
    Producer.ConfigClass = UGraphContinuationTestConfig::StaticClass();
    Producer.ExecutionClass = UGraphDeferredTestExecution::StaticClass();
    Producer.InputPayload = EAbilityPortPayload::Origin;
    Producer.bSupportsActorTargets = Producer.bSupportsMassTargets = Producer.bSupportsSnapshotTargets = true;
    Producer.bPrototypeCompatibilitySafe = true;
    Producer.bIsAsync = true;
    Producer.Ports = {{TEXT("Logical"), EAbilityPortPayload::Origin, false}};
    FString Error;
    TestTrue(TEXT("Only the test producer is added to the production registry"), Registry.Register(Producer, Error));
    auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
    Definition->SchemaVersion = 2;
    Definition->CompatibilityProfile = TEXT("PrototypeFine.Mixed.v1");
    Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    Definition->PrototypeHelperBudget.MaxEventsPerTick = 1;
    auto* Parameters = NewObject<UAbilityPrototype>(Definition);
    Parameters->Kind = EAbilityPrototypeKind::Burst;
    Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Parameters->Cooldown = 0.f;
    auto* Root = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
    Root->Parameters = Parameters;
    Root->Profile = Definition->CompatibilityProfile;
    auto* Child = DuplicateObject<UAbilityPrototypeScopeNodeConfig>(Root, Definition);
    Child->LogicalDepth = 1;
    const FGuid RootId = FGuid::NewGuid();
    const FGuid ProducerId = FGuid::NewGuid();
    const FGuid ChildId = FGuid::NewGuid();
    Definition->EntryNodeId = RootId;
    Definition->Nodes = {{RootId, TEXT("Scope.Prototype"), Root},
                         {ProducerId, Producer.TypeId, NewObject<UGraphContinuationTestConfig>(Definition)},
                         {ChildId, TEXT("Scope.Prototype"), Child}};
    Definition->Edges = {{RootId, ProducerId, TEXT("Ready")}, {ProducerId, ChildId, TEXT("Logical")}};
    auto* Context = NewObject<UPrototypeCastContext>(F.World);
    Context->Caster = F.Source;
    Context->Level = 1;
    Context->CastId = FGuid::NewGuid();
    Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
    TestTrue(TEXT("Actual source life is captured"), Context->CaptureSourceLife(Error));
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Context = Context;
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = Definition->CompatibilityProfile;
    Options.BudgetPolicy = Definition->BudgetPolicy;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    if (!TestTrue(TEXT("One-event-per-real-frame graph starts"),
                  Run->StartWithInput(Definition, Input, Options, Registry, Error)))
    {
        AddInfo(Error);
        return false;
    }
    ++GFrameCounter;
    Run->Advance(.1f);
    TestEqual(TEXT("Only the root is admitted before deferred work receives another real frame"), Context->Spawned, 1);
    TestFalse(TEXT("Ordinary Flush must not report deferred work as already drained"),
              Run->FlushPublishedEvents(false));
    TestFalse(TEXT("Non-immediate budget exhaustion keeps the exact root alive"), Run->IsFinished());
    ++GFrameCounter;
    Run->Advance(0.f);
    TestEqual(TEXT("Next actual frame admits the deferred child exactly once"), Context->Spawned, 2);
    Run->Cancel();
    TestEqual(TEXT("Cancellation clears all active and reserved ownership"), Run->GetActiveInvocationCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphContinuationAsync,
                                 "test_25d.DynamicMage.Execution.ContinuationAwaitsActualAsyncDescendants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphContinuationAsync::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilityNodeRegistry Registry;
    auto* Definition = AbilityGraphContinuationTests::Build(F, Registry);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Contacts = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Contacts += Event.OutputPort == TEXT("Contact") ? 1 : 0;
        });
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    FString Error;
    TestTrue(TEXT("Source starts under same actual registry"),
             Run->StartWithInput(Definition, Input, {}, Registry, Error));
    TestEqual(TEXT("Only first contact enters before its delayed child"), Contacts, 1);
    TestEqual(TEXT("No GE before delay"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        ++GFrameCounter;
        Run->Advance(.11f);
        TestEqual(TEXT("Each completed descendant applies exactly one actual GE"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f - 10.f * Index);
        TestEqual(TEXT("Next contact waits for previous complete descendant"), Contacts, FMath::Min(Index + 2, 3));
    }
    TestEqual(TEXT("Source finishes after all descendants"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);
    TestEqual(TEXT("No invocation resources remain"), Run->GetActiveInvocationCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphContinuationCancel,
                                 "test_25d.DynamicMage.Execution.ContinuationCancelNeverResumesOldSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphContinuationCancel::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilityNodeRegistry Registry;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    auto* Definition = AbilityGraphContinuationTests::Build(F, Registry);
    int32 Contacts = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Contacts += Event.OutputPort == TEXT("Contact") ? 1 : 0;
        });
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    FString Error;
    TestTrue(TEXT("Pending async source starts"), Run->StartWithInput(Definition, Input, {}, Registry, Error));
    TestEqual(TEXT("One original contact is waiting"), Contacts, 1);
    Run->Cancel();
    ++GFrameCounter;
    Run->Advance(10.f);
    TestEqual(TEXT("Cancellation suppresses all future contacts"), Contacts, 1);
    TestEqual(TEXT("Cancellation suppresses pending real GE"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestEqual(TEXT("Cancellation releases source and child"), Run->GetActiveInvocationCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphContinuationNested,
                                 "test_25d.DynamicMage.Execution.NestedContinuationsShareOneRootAndActualEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphContinuationNested::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilityNodeRegistry Registry;
    auto* Definition = AbilityGraphContinuationTests::Build(F, Registry);
    const FGuid Delay = Definition->Nodes[1].NodeId;
    FAbilityGraphNode Inner;
    Inner.NodeId = FGuid::NewGuid();
    Inner.TypeId = TEXT("Test.Continuation");
    Inner.Config = NewObject<UGraphContinuationTestConfig>(Definition);
    Definition->Nodes.Add(Inner);
    Definition->Edges[0].To = Inner.NodeId;
    Definition->Edges.Add({Inner.NodeId, Delay, TEXT("Contact")});
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Contacts = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Contacts += Event.OutputPort == TEXT("Contact") ? 1 : 0;
        });
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    FString Error;
    TestTrue(TEXT("Nested producers use the original registry and one runtime root"),
             Run->StartWithInput(Definition, Input, {}, Registry, Error));
    TestEqual(TEXT("Outer and inner each publish only their first pending contact"), Contacts, 2);
    for (int32 Index = 0; Index < 9; ++Index)
    {
        ++GFrameCounter;
        Run->Advance(.11f);
        TestEqual(TEXT("One real delayed GE per advance; no inner or outer early continuation"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f - Index * 10.f);
    }
    TestEqual(TEXT("Three outer contacts contain exactly three inner contacts each"), Contacts, 12);
    TestEqual(TEXT("All nested subtrees finish inside the same finite root"), Run->GetEndReason(),
              EAbilityGraphEndReason::Completed);
    TestEqual(TEXT("No parent or inner invocation remains"), Run->GetActiveInvocationCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphContinuationGECancel,
                                 "test_25d.DynamicMage.Execution.RealEffectCallbackCancelsPendingContinuation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphContinuationGECancel::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilityNodeRegistry Registry;
    auto* Definition = AbilityGraphContinuationTests::Build(F, Registry);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Effects = 0;
    const auto Hook = F.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
            {
                ++Effects;
                Run->Cancel();
            }
        });
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    FString Error;
    TestTrue(TEXT("Producer waits for a genuine delayed effect"),
             Run->StartWithInput(Definition, Input, {}, Registry, Error));
    ++GFrameCounter;
    Run->Advance(.11f);
    ++GFrameCounter;
    Run->Advance(1.f);
    F.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    TestEqual(TEXT("Actual effect callback ran exactly once"), Effects, 1);
    TestEqual(TEXT("Already landed effect stays, next contact is never resumed"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestEqual(TEXT("Callback preserves cancellation instead of normal completion"), Run->GetEndReason(),
              EAbilityGraphEndReason::Cancelled);
    TestEqual(TEXT("Cancellation releases the waiting producer and descendants"), Run->GetActiveInvocationCount(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreparedInventoryRequiresConsumer,
                                 "test_25d.DynamicMage.Execution.PreparedInventoryCannotBeIgnoredAsOrigin",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPreparedInventoryRequiresConsumer::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    FAbilityNodeDescriptor Producer;
    Producer.TypeId = TEXT("Test.PreparedProducer");
    Producer.ConfigClass = UGraphContinuationTestConfig::StaticClass();
    Producer.ExecutionClass = UGraphContinuationTestExecution::StaticClass();
    Producer.InputPayload = EAbilityPortPayload::Origin;
    Producer.bSupportsActorTargets = Producer.bSupportsMassTargets = Producer.bSupportsSnapshotTargets = true;
    Producer.Ports = {{TEXT("Prepared"), EAbilityPortPayload::PreparedInventory, true}};
    FAbilityNodeDescriptor Consumer = Producer;
    Consumer.TypeId = TEXT("Test.PreparedConsumer");
    Consumer.InputPayload = EAbilityPortPayload::PreparedInventory;
    Consumer.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    TestTrue(TEXT("Source declares its actual inventory capability"), Registry.Register(Producer, Error));
    TestTrue(TEXT("Consumer declares the same explicit capability"), Registry.Register(Consumer, Error));
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode From;
    From.NodeId = FGuid::NewGuid();
    From.TypeId = Producer.TypeId;
    From.Config = NewObject<UGraphContinuationTestConfig>(Graph);
    FAbilityGraphNode To;
    To.NodeId = FGuid::NewGuid();
    To.TypeId = Consumer.TypeId;
    To.Config = NewObject<UGraphContinuationTestConfig>(Graph);
    Graph->EntryNodeId = From.NodeId;
    Graph->Nodes = {From, To};
    Graph->Edges = {{From.NodeId, To.NodeId, TEXT("Prepared")}};
    TArray<FString> Errors;
    TestTrue(TEXT("Registered prepared capability connects to its declared consumer"),
             Graph->Validate(Registry, Errors));
    Graph->Nodes[1].TypeId = TEXT("Control.DelayOrigin");
    Graph->Nodes[1].Config = NewObject<UAbilityDelayNodeConfig>(Graph);
    TestFalse(TEXT("Origin cannot silently swallow an inventory consumption/return obligation"),
              Graph->Validate(Registry, Errors));
    return true;
}
#endif
