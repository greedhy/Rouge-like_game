// 测试链：真实火球目录/已接受的命中边 -> 工作坊草稿和配置快照 -> 单目标状态策略与到期。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    /** 按画布当前版本发出选中节点或边的请求。 */
    FAbilityRuntimeDeveloperEdit EditRequest(const FAbilityRuntimeDeveloperState& State, FGuid Node, FName Modifier)
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = State.SelectedId;
        Edit.TargetNodeId = Node;
        Edit.ModifierId = Modifier;
        Edit.ExpectedRevision = State.Revision;
        Edit.ExpectedBaseHash = State.BaseHash;
        Edit.ExpectedAcceptedHash = State.AcceptedGraph.ContentHash;
        Edit.ExpectedPreviewSessionId = State.PreviewSessionId;
        return Edit;
    }

    /** 用工作坊提交的真实配置单独运行状态节点，核对原状态组件持有的期限。 */
    bool CheckPetrifyLifetime(FAutomationTestBase& Test, FAbilityActualMageFrontendFixture& Fixture,
                              const FAbilityGraphNode& StateNode, FString& Error)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Fixture.World);
        Graph->EntryNodeId = StateNode.NodeId;
        Graph->Nodes = {{StateNode.NodeId, StateNode.TypeId,
                         DuplicateObject<UAbilityPetrifyNodeConfig>(
                             CastChecked<UAbilityPetrifyNodeConfig>(StateNode.Config), Graph)}};
        FAbilitySourceToken Source;
        FAbilityTargetToken Target;
        if (!Test.TestTrue(TEXT("Capture the authority source and original living target"),
                           AbilityExecutionIdentity::CaptureSource(Fixture.Pawn, Source, Error) &&
                               AbilityExecutionIdentity::CaptureTarget(
                                   Source, FPureMassCombatTarget::FromActor(Fixture.Target), Target, Error)))
        {
            return false;
        }
        auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
        if (!Test.TestTrue(TEXT("Run the actual edited state strategy"), Run->Start(Graph, Source, Target, Error)))
        {
            return false;
        }
        Test.TestTrue(TEXT("The real target is petrified"), Fixture.IsPetrified());
        auto* Control = Fixture.Target->FindComponentByClass<UBatch6PetrifyComponent>();
        if (!Test.TestNotNull(TEXT("The target owns the actual petrify component"), Control))
        {
            return false;
        }
        Fixture.Step(1.5f);
        Control->TickComponent(1.5f, LEVELTICK_All, nullptr);
        Test.TestTrue(TEXT("Control remains after the original 1.25 seconds"), Fixture.IsPetrified());
        Fixture.Step(1.15f);
        Control->TickComponent(1.15f, LEVELTICK_All, nullptr);
        Test.TestFalse(TEXT("The original state service restores the target after 2.5 seconds"), Fixture.IsPetrified());
        return true;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeWorkshopFireBoltSinglePetrifyDuration,
                                 "test_25d.DynamicMage.Workshop.SingleState.FireBoltPetrifyDuration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 在真正的原生火球命中边插入石化后，用新增 GUID 继续编辑其状态时长。 */
bool FRuntimeWorkshopFireBoltSinglePetrifyDuration::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture Fixture;
    FString Error;
    const auto* Definition = Fixture.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
    auto& Developer = *Fixture.Developer;
    if (!TestNotNull(TEXT("Actual native FireBolt catalog variant exists"), Definition) ||
        !TestTrue(TEXT("Free preview begins for the actual variant"),
                  Developer.BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Original = Developer.GetDeveloperState();
    const auto* Resolve = Original.AcceptedGraph.Nodes.FindByPredicate(
        [](const FAbilityRuntimeGraphNodeView& Node)
        {
            return Node.TypeId == TEXT("Target.ResolveImpact");
        });
    const auto* Damage = Original.AcceptedGraph.Nodes.FindByPredicate(
        [](const FAbilityRuntimeGraphNodeView& Node)
        {
            return Node.TypeId == TEXT("Effect.InputDamage");
        });
    if (!TestNotNull(TEXT("Actual live-impact resolver is selectable"), Resolve) ||
        !TestNotNull(TEXT("Actual damage endpoint is selectable"), Damage))
    {
        return false;
    }
    auto Insert = EditRequest(Original, Resolve->NodeId, TEXT("Insert.Registered"));
    Insert.OutputPortId = TEXT("TargetFound");
    Insert.TargetEdgeTo = Damage->NodeId;
    Insert.FragmentId = TEXT("Target.Petrify");
    Insert.Value = 1.25f;
    if (!TestTrue(TEXT("Preview the exact ResolveImpact.TargetFound to InputDamage edge"),
                  Developer.PreviewGraphEdit(Insert, Error)) ||
        !TestTrue(TEXT("Apply the registered single-target petrify fragment"),
                  Developer.ApplyGraphDraft(Developer.GetDeveloperState().DraftToken, Original.Revision, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Inserted = Developer.GetDeveloperState();
    const auto* Petrify = Inserted.AcceptedGraph.Nodes.FindByPredicate(
        [&Original](const FAbilityRuntimeGraphNodeView& Node)
        {
            return Node.TypeId == TEXT("Status.Petrify") && !Original.AcceptedGraph.Nodes.ContainsByPredicate(
                                                                [&Node](const FAbilityRuntimeGraphNodeView& Old)
                                                                {
                                                                    return Old.NodeId == Node.NodeId;
                                                                });
        });
    if (!TestNotNull(TEXT("The accepted graph exposes a new single-target Status.Petrify node"), Petrify))
    {
        return false;
    }
    TestTrue(TEXT("The new node offers duration scaling"),
             Petrify->SupportedModifiers.Contains(TEXT("Scale.Duration")));
    TestTrue(TEXT("The new node offers absolute duration"), Petrify->SupportedModifiers.Contains(TEXT("Set.Duration")));
    for (const FName Unsupported : {FName(TEXT("Scale.Damage")), FName(TEXT("Scale.Radius")),
                                    FName(TEXT("Set.HomingTurnRate")), FName(TEXT("Set.PelletTargetPolicy"))})
    {
        TestFalse(FString::Printf(TEXT("Single-target status does not offer %s"), *Unsupported.ToString()),
                  Petrify->SupportedModifiers.Contains(Unsupported));
    }

    auto Scale = EditRequest(Inserted, Petrify->NodeId, TEXT("Scale.Duration"));
    for (const float Invalid : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()})
    {
        Scale.Value = Invalid;
        TestFalse(TEXT("Invalid multipliers do not produce a preview"), Developer.PreviewGraphEdit(Scale, Error));
        TestEqual(TEXT("Rejected edit keeps the accepted revision"), Developer.GetDeveloperState().Revision,
                  Inserted.Revision);
        TestEqual(TEXT("Rejected edit keeps the accepted content"), Developer.GetDeveloperState().ContentHash,
                  Inserted.ContentHash);
    }
    Scale.Value = 2.f;
    if (!TestTrue(TEXT("Preview duration scaling of the exact inserted GUID"),
                  Developer.PreviewGraphEdit(Scale, Error)) ||
        !TestTrue(TEXT("Apply the duration draft"),
                  Developer.ApplyGraphDraft(Developer.GetDeveloperState().DraftToken, Inserted.Revision, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    if (!TestTrue(TEXT("Create the accepted next-cast snapshot"),
                  Developer.CreateCastSnapshot(Inserted.AbilityTag, &Developer, Snapshot, Error)) ||
        !TestNotNull(TEXT("The cast owns an actual graph"), Snapshot.Graph.Get()))
    {
        AddInfo(Error);
        return false;
    }
    const auto* StateNode = Snapshot.Graph->Nodes.FindByPredicate(
        [NodeId = Petrify->NodeId](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == NodeId;
        });
    const auto* Config = StateNode ? Cast<UAbilityPetrifyNodeConfig>(StateNode->Config) : nullptr;
    if (!TestNotNull(TEXT("Next cast contains the edited single-target config"), Config))
    {
        return false;
    }
    TestEqual(TEXT("1.25 seconds multiplied by two is consumed as 2.5 seconds"), Config->DurationSeconds, 2.5f);
    if (!CheckPetrifyLifetime(*this, Fixture, *StateNode, Error))
    {
        AddInfo(Error);
        return false;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeWorkshopSingleStatusParameterBounds,
                                 "test_25d.DynamicMage.Workshop.SingleState.DurationParameterBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 参数消费边界拒绝非法输入后保持原状态字段，覆盖绕过工作坊模板的调用方。 */
bool FRuntimeWorkshopSingleStatusParameterBounds::RunTest(const FString&)
{
    auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>();
    auto* Slow = NewObject<UAbilitySlowNodeConfig>();
    Slow->Multiplier = .4f;
    FString Error;
    const auto CheckDuration = [this, &Error](UAbilityNodeConfig& Config, float& Duration)
    {
        TestTrue(TEXT("Set the inclusive upper duration bound"),
                 Config.TrySetRecipeParameter(EAbilityConfigParameter::Duration, 60.f, Error));
        TestEqual(TEXT("The actual duration is set absolutely"), Duration, 60.f);
        TestFalse(TEXT("Scaling to a non-finite result is rejected"),
                  Config.TryScaleRecipeParameter(EAbilityConfigScalar::Duration, std::numeric_limits<float>::max()));
        for (const float Invalid : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()})
        {
            TestFalse(TEXT("Invalid multiplier is rejected by the config"),
                      Config.TryScaleRecipeParameter(EAbilityConfigScalar::Duration, Invalid));
        }
        for (const float Invalid : {0.001f, 60.01f, std::numeric_limits<float>::quiet_NaN()})
        {
            TestFalse(TEXT("Invalid absolute duration is rejected by the config"),
                      Config.TrySetRecipeParameter(EAbilityConfigParameter::Duration, Invalid, Error));
        }
        TestFalse(TEXT("No homing parameter is consumed"),
                  Config.TrySetRecipeParameter(EAbilityConfigParameter::HomingTurnRate, 5.f, Error));
        TestFalse(TEXT("No damage scalar is consumed"),
                  Config.TryScaleRecipeParameter(EAbilityConfigScalar::Damage, 2.f));
        TestEqual(TEXT("Failed edits leave the original duration intact"), Duration, 60.f);
        TestTrue(TEXT("Set the inclusive lower duration bound"),
                 Config.TrySetRecipeParameter(EAbilityConfigParameter::Duration, .01f, Error));
        TestTrue(TEXT("Scale the actual duration"),
                 Config.TryScaleRecipeParameter(EAbilityConfigScalar::Duration, 2.f));
        TestEqual(TEXT("The resulting duration is stored"), Duration, .02f);
    };
    CheckDuration(*Petrify, Petrify->DurationSeconds);
    CheckDuration(*Slow, Slow->Duration);
    TestFalse(TEXT("Slow cannot be scaled outside its existing valid range"),
              Slow->TryScaleRecipeParameter(EAbilityConfigScalar::Duration, 5000.f));
    TestEqual(TEXT("Rejected slow scaling leaves its duration intact"), Slow->Duration, .02f);
    TestEqual(TEXT("Duration editing preserves the authored speed multiplier"), Slow->Multiplier, .4f);
    return true;
}
#endif
