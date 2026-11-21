// 调用链：Automation -> 正式PlayerState/ASC与实际目录预览 -> 服务端施法快照 -> GUI值状态。
// 测试不保存资产、不伪造客户端图；校验界面公开的拓扑确实对应下一次预览施法使用的定义。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Player/MyPlayerState.h"
#include "UObject/UnrealType.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeGraphWorkshopPublishedTopology,
                                 "test_25d.DynamicMage.Workshop.ServerStatePublishesActualTopology",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeGraphWorkshopPublishedTopology::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* State = Fixture.World->SpawnActor<AMyPlayerState>();
    State->Profession = EPlayerProfession::Mage;
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, Fixture.Source);
    auto* Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
    FString Error;
    if (!TestNotNull(TEXT("Existing PlayerState owns the real recipe component"), Developer) ||
        !TestTrue(TEXT("Read the actual Mage catalog"), Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto* Definition = Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            return GetPathNameSafe(Row.Base.AbilityClass.Get()) ==
                   TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");
        });
    if (!TestNotNull(TEXT("Actual Petrify class is selectable"), Definition) ||
        !TestTrue(TEXT("Begin free preview through the server entry"),
                  Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    const auto& Published = Developer->GetDeveloperState();
    const bool bHasSnapshot = Developer->CreateCastSnapshot(Published.AbilityTag, Developer, Snapshot, Error);
    TestTrue(TEXT("Real next-cast definition is available"), bHasSnapshot);
    TestTrue(TEXT("Real next-cast graph is nonempty"), Snapshot.Graph && !Snapshot.Graph->Nodes.IsEmpty());

    // 通过反射检查公共网络值合同，使初始测试可在旧接口上编译，并真实观察缺少图视图的失败。
    const auto* GraphProperty =
        FindFProperty<FStructProperty>(FAbilityRuntimeDeveloperState::StaticStruct(), TEXT("AcceptedGraph"));
    if (TestNotNull(TEXT("Server state must publish the accepted graph, not only hash/base dropdown"), GraphProperty))
    {
        const void* GraphValue = GraphProperty->ContainerPtrToValuePtr<void>(&Published);
        const auto* Nodes = FindFProperty<FArrayProperty>(GraphProperty->Struct, TEXT("Nodes"));
        const auto* Edges = FindFProperty<FArrayProperty>(GraphProperty->Struct, TEXT("Edges"));
        if (TestNotNull(TEXT("Accepted graph exposes actual nodes"), Nodes) &&
            TestNotNull(TEXT("Accepted graph exposes actual edges"), Edges) && Snapshot.Graph)
        {
            const FScriptArrayHelper NodeValues(Nodes, Nodes->ContainerPtrToValuePtr<void>(GraphValue));
            const FScriptArrayHelper EdgeValues(Edges, Edges->ContainerPtrToValuePtr<void>(GraphValue));
            TestEqual(TEXT("Published topology contains every actual node"), NodeValues.Num(),
                      Snapshot.Graph->Nodes.Num());
            TestEqual(TEXT("Published topology contains every actual edge"), EdgeValues.Num(),
                      Snapshot.Graph->Edges.Num());
        }
    }
    TestTrue(TEXT("End owns only the temporary preview"), Developer->EndPreview(Error));
    return true;
}
namespace RuntimeGraphWorkshopTests
{
    /** 每个夹具使用实际PlayerState和目录，构造期不授予或激活业务技能。 */
    struct FPlayer
    {
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;

        explicit FPlayer(FAbilityGraphTestFixture& Fixture)
        {
            auto* State = Fixture.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->InitAbilityActorInfo(State, Fixture.Source);
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
        }

        /** 从真实目录选择查询/批状态两节点，不手工注册一张图掩盖默认导出错误。 */
        FName FindDefinition(FString& Error)
        {
            if (!Developer || !Developer->InitializeCatalog(nullptr, Error))
            {
                return NAME_None;
            }
            const auto* Row = Developer->GetDefinitions().FindByPredicate(
                [](const FAbilityRuntimeDeveloperDefinition& Item)
                {
                    return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                           TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");
                });
            return Row ? Row->DefinitionId : NAME_None;
        }
    };

    /** 生成当前画布精确请求；不接受旧基础索引或别的玩家所见版本替代准确节点。 */
    FAbilityRuntimeDeveloperEdit Request(UAbilityRuntimeRecipeDeveloperComponent& Component, FGuid Node, FName Modifier)
    {
        const auto& State = Component.GetDeveloperState();
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

    TSet<FGuid> NodeIds(const FAbilityRuntimeGraphView& View)
    {
        TSet<FGuid> Result;
        for (const auto& Node : View.Nodes)
        {
            Result.Add(Node.NodeId);
        }
        return Result;
    }
} // namespace RuntimeGraphWorkshopTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeGraphWorkshopDraftTransactions,
                                 "test_25d.DynamicMage.Workshop.DraftNewNodeIdentityAndUndoDependencies",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeGraphWorkshopDraftTransactions::RunTest(const FString&)
{
    using namespace RuntimeGraphWorkshopTests;
    FAbilityGraphTestFixture Fixture;
    FPlayer Player(Fixture);
    FString Error;
    const auto Definition = Player.FindDefinition(Error);
    if (!TestTrue(TEXT("Actual class starts free preview"), Player.Developer->BeginPreview(Definition, Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto& Component = *Player.Developer;
    const auto Original = Component.GetDeveloperState();
    const auto* Query = Original.AcceptedGraph.Nodes.FindByPredicate(
        [](const FAbilityRuntimeGraphNodeView& Node)
        {
            return Node.TypeId == TEXT("Query.PetrifyArea");
        });
    if (!TestNotNull(TEXT("Canvas projects the real area query"), Query))
    {
        return false;
    }
    auto Edit = Request(Component, Query->NodeId, TEXT("Append.Registered"));
    Edit.OutputPortId = TEXT("Completed");
    Edit.FragmentId = TEXT("Target.Petrify");
    Edit.Value = 1.25f;
    if (!TestTrue(TEXT("Preview compiles a registered branch"), Component.PreviewGraphEdit(Edit, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Draft = Component.GetDeveloperState();
    TestEqual(TEXT("Preview does not publish a new cast revision"), Draft.Revision, Original.Revision);
    TestEqual(TEXT("Accepted hash stays unchanged during preview"), Draft.ContentHash, Original.ContentHash);
    TestEqual(TEXT("Draft has exactly one actual added batch-state node"), Draft.DraftGraph.Nodes.Num(),
              Original.AcceptedGraph.Nodes.Num() + 1);
    const auto OriginalIds = NodeIds(Original.AcceptedGraph);
    FGuid Added;
    for (const auto& Node : Draft.DraftGraph.Nodes)
    {
        if (!OriginalIds.Contains(Node.NodeId))
        {
            Added = Node.NodeId;
        }
    }
    TestTrue(TEXT("New node has a selectable actual identity"), Added.IsValid());
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (!TestTrue(TEXT("Frozen draft survives GC and applies once"),
                  Component.ApplyGraphDraft(Draft.DraftToken, Draft.Revision, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Apply preserves the exact new GUID, not just semantic hash"),
             NodeIds(Component.GetDeveloperState().AcceptedGraph).Contains(Added));
    auto Second = Request(Component, Added, TEXT("Scale.Duration"));
    Second.Value = 2.f;
    if (!TestTrue(TEXT("Next edit targets the node introduced by the first"),
                  Component.PreviewGraphEdit(Second, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto SecondDraft = Component.GetDeveloperState();
    TestFalse(TEXT("Old draft cannot replace a newer pending candidate"),
              Component.ApplyGraphDraft(Draft.DraftToken, SecondDraft.Revision, Error));
    TestEqual(TEXT("Stale apply keeps the correct pending token"), Component.GetDeveloperState().DraftToken,
              SecondDraft.DraftToken);
    if (!TestTrue(TEXT("Exact second candidate applies"),
                  Component.ApplyGraphDraft(SecondDraft.DraftToken, SecondDraft.Revision, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    Component.CreateCastSnapshot(Original.AbilityTag, &Component, Snapshot, Error);
    const auto* Node = Snapshot.Graph ? Snapshot.Graph->Nodes.FindByPredicate(
                                            [Added](const FAbilityGraphNode& Item)
                                            {
                                                return Item.NodeId == Added;
                                            })
                                      : nullptr;
    const auto* StateConfig = Node ? Cast<UAbilityPetrifyBatchConfig>(Node->Config) : nullptr;
    if (TestNotNull(TEXT("The actual new state config is modified"), StateConfig))
    {
        TestEqual(TEXT("Second rule consumes the real state duration"), StateConfig->Duration, 2.5f);
    }
    const auto Applied = Component.GetDeveloperState();
    const auto* AddedView = Applied.AcceptedGraph.Nodes.FindByPredicate(
        [Added](const FAbilityRuntimeGraphNodeView& Item)
        {
            return Item.NodeId == Added;
        });
    if (TestNotNull(TEXT("Inserted state remains a selectable accepted node"), AddedView))
    {
        TestTrue(TEXT("Menu exposes the actual duration scalar"),
                 AddedView->SupportedModifiers.Contains(TEXT("Scale.Duration")));
        TestFalse(TEXT("State menu does not claim projectile-only homing"),
                  AddedView->SupportedModifiers.Contains(TEXT("Set.HomingTurnRate")));
        TestFalse(TEXT("State menu does not claim pellet target routing"),
                  AddedView->SupportedModifiers.Contains(TEXT("Set.PelletTargetPolicy")));
    }
    if (!TestEqual(TEXT("History has two chronological owned sources"), Applied.AcceptedGraph.AppliedSources.Num(), 2))
    {
        Component.EndPreview(Error);
        return false;
    }
    TestFalse(TEXT("Removing a parent cannot silently discard its dependent rule"),
              Component.RemoveGraphSource(Applied.AcceptedGraph.AppliedSources[0], Applied.Revision, Error));
    TestEqual(TEXT("Rejected undo preserves the accepted revision"), Component.GetDeveloperState().Revision,
              Applied.Revision);
    TestTrue(TEXT("Undo the leaf rule first"),
             Component.RemoveGraphSource(Applied.AcceptedGraph.AppliedSources.Last(), Applied.Revision, Error));
    const auto LeafRemoved = Component.GetDeveloperState();
    TestTrue(TEXT("Then undo the original append"),
             Component.RemoveGraphSource(LeafRemoved.AcceptedGraph.AppliedSources.Last(), LeafRemoved.Revision, Error));
    TestEqual(TEXT("All undo restores original content"), Component.GetDeveloperState().ContentHash,
              Original.ContentHash);
    TestEqual(TEXT("All undo restores original node count"), Component.GetDeveloperState().AcceptedGraph.Nodes.Num(),
              Original.AcceptedGraph.Nodes.Num());
    TestTrue(TEXT("Preview closes after undo"), Component.EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeGraphWorkshopSessionGuards,
                                 "test_25d.DynamicMage.Workshop.ExactSessionAndOwnerGuards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeGraphWorkshopSessionGuards::RunTest(const FString&)
{
    using namespace RuntimeGraphWorkshopTests;
    FAbilityGraphTestFixture Fixture;
    FPlayer One(Fixture);
    FPlayer Two(Fixture);
    FString Error;
    const auto Definition = One.FindDefinition(Error);
    Two.FindDefinition(Error);
    const FGuid FirstSession = FGuid::NewGuid();
    const FGuid SecondSession = FGuid::NewGuid();
    One.Developer->ServerBeginGraphPreview(Definition, FirstSession);
    Two.Developer->ServerBeginGraphPreview(Definition, SecondSession);
    const auto OneState = One.Developer->GetDeveloperState();
    if (!TestTrue(TEXT("Real graph session began with a nonempty accepted graph"),
                  OneState.bPreviewActive && !OneState.AcceptedGraph.Nodes.IsEmpty()))
    {
        One.Developer->EndPreview(Error);
        Two.Developer->EndPreview(Error);
        return false;
    }
    TestEqual(TEXT("Opening identity is known before a remote acknowledgment"), OneState.PreviewSessionId,
              FirstSession);
    auto Edit = Request(*One.Developer, OneState.AcceptedGraph.Nodes[0].NodeId, TEXT("Scale.Radius"));
    Edit.Value = 1.5f;
    TestFalse(TEXT("Another owner cannot replay this graph selection"), Two.Developer->PreviewGraphEdit(Edit, Error));
    One.Developer->ServerCloseGraphPreview(FGuid::NewGuid(), SecondSession, false);
    TestTrue(TEXT("Wrong session cannot close the current preview"), One.Developer->GetDeveloperState().bPreviewActive);
    One.Developer->ServerClosePreview(FGuid::NewGuid(), false);
    TestTrue(TEXT("Unbound legacy close cannot tear down a graph session"),
             One.Developer->GetDeveloperState().bPreviewActive);
    One.Developer->ServerEndPreview();
    One.Developer->ServerAbandonPreview();
    One.Developer->ServerResetPreview(OneState.SelectedId, OneState.Revision, OneState.BaseHash);
    TestTrue(TEXT("All sessionless legacy mutations leave the graph session alive"),
             One.Developer->GetDeveloperState().bPreviewActive);
    One.Developer->ServerCloseGraphPreview(FGuid::NewGuid(), FirstSession, false);
    TestFalse(TEXT("Matching session closes its own preview"), One.Developer->GetDeveloperState().bPreviewActive);
    TestTrue(TEXT("The other player's preview remains active"), Two.Developer->GetDeveloperState().bPreviewActive);
    Two.Developer->ServerCloseGraphPreview(FGuid::NewGuid(), SecondSession, false);
    TestFalse(TEXT("Second owner also closes cleanly"), Two.Developer->GetDeveloperState().bPreviewActive);
    return true;
}
#endif
