// 调用链：RootScope -> 本地发布器 -> 真实ChildScope；累计图准入与Context生成计数分别验证。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityPrototypeLogicalBudgetTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "MyGameplayTags.h"

namespace AbilityPrototypeLogicalAdmissionTests
{
    bool Run(FAutomationTestBase& Test, bool bRequest)
    {
        FAbilityGraphTestFixture F;
        auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
        Definition->SchemaVersion = 2;
        Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
        Definition->CompatibilityProfile = TEXT("PrototypeFine.Mixed.v1");
        auto Add = [&](FName Type, UAbilityNodeConfig* Config)
        {
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            Node.Config = Config;
            Definition->Nodes.Add(Node);
            return Node.NodeId;
        };
        auto Scope = [&](int32 Depth)
        {
            auto* Config = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Config->Parameters = NewObject<UAbilityPrototype>(Config);
            Config->Parameters->Kind = EAbilityPrototypeKind::Burst;
            Config->Parameters->Cooldown = 0.f;
            Config->Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Config->LogicalDepth = Depth;
            Config->Profile = Definition->CompatibilityProfile;
            return Add(TEXT("Scope.Prototype"), Config);
        };
        const FGuid Root = Scope(0);
        auto* PublisherConfig = NewObject<UAbilityPrototypeLogicalBudgetTestConfig>(Definition);
        PublisherConfig->bRequestChildren = bRequest;
        const FGuid Publisher = Add(TEXT("Test.LogicalPublisher"), PublisherConfig);
        const FGuid Child = Scope(1);
        Definition->EntryNodeId = Root;
        FAbilityGraphEdge Ready;
        Ready.From = Root;
        Ready.OutputPort = TEXT("Ready");
        Ready.To = Publisher;
        Definition->Edges.Add(Ready);
        FAbilityGraphEdge Pulse;
        Pulse.From = Publisher;
        Pulse.OutputPort = TEXT("Pulse");
        Pulse.To = Child;
        Definition->Edges.Add(Pulse);
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = TEXT("Test.LogicalPublisher");
        Descriptor.ConfigClass = UAbilityPrototypeLogicalBudgetTestConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityPrototypeLogicalBudgetTestPublisher::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.bPrototypeCompatibilitySafe = true;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.Ports = {{TEXT("Pulse"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        if (!Test.TestTrue(TEXT("Test-local producer registers with actual typed contracts"),
                           Registry.Register(Descriptor, Error)))
        {
            return false;
        }
        auto* Context = NewObject<UAbilityPrototypeLogicalBudgetTestContext>(F.World);
        Context->Caster = F.Source;
        Context->Level = 1;
        Context->DamageParams.SourceAbilitySystemComponent = F.ASC;
        Context->CaptureSourceLife(Error);
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        Input.DamageParams = Context->DamageParams;
        auto* Instance = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Definition->CompatibilityProfile;
        Options.BudgetPolicy = Definition->BudgetPolicy;
        if (!Test.TestTrue(TEXT("The valid graph is accepted before actual node execution"),
                           Instance->StartWithInput(Definition, Input, Options, Registry, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        Test.TestEqual(TEXT("Helper producer does not consume one of the original64 logical entries"), Context->Spawned,
                       64);
        Test.TestTrue(TEXT("The finite graph reaches its required terminal outcome"), Instance->IsFinished());
        Test.TestEqual(TEXT("RequestChild rejects excess before execution; normalPublish keeps hard failure"),
                       Instance->GetEndReason(),
                       bRequest ? EAbilityGraphEndReason::Completed : EAbilityGraphEndReason::BudgetExceeded);
        Test.TestEqual(TEXT("RequestChild admits only63 child scopes after original root; rawPublish queues70"),
                       Context->AcceptedRequests, bRequest ? 63 : 70);
        return true;
    }
} // namespace AbilityPrototypeLogicalAdmissionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeRequestAdmission64,
                                 "test_25d.UnifiedGA.DynamicMage.LogicalAdmission.RequestChildKeepsCumulative64",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeRequestAdmission64::RunTest(const FString&)
{
    return AbilityPrototypeLogicalAdmissionTests::Run(*this, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypePublishAdmission64,
                                 "test_25d.UnifiedGA.DynamicMage.LogicalAdmission.PublishKeepsHardCumulativeFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePublishAdmission64::RunTest(const FString&)
{
    return AbilityPrototypeLogicalAdmissionTests::Run(*this, false);
}
#endif
