// 测试链：未选职业的真实法师 Avatar/PlayerState ASC → 工作台 Begin/草稿/提交 → 真实图快照 → 准确关闭。
// 不通过把职业改为 Mage 绕过直接 PIE 的缺陷；所有资源在临时世界中，永不写入存档。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeRecipeDirectPieMage,
    "test_25d.DynamicMage.Workshop.DirectPIE.UnselectedMageCanComposeWithoutProfessionWrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDirectPieMage::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    F.State->Profession = EPlayerProfession::Unselected;
    FString Error;
    const auto* Row = F.Definition(TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"), Error);
    if (!TestNotNull(TEXT("Actual default Mage definition exists"), Row))
    {
        return false;
    }
    const FName DefinitionId = Row->DefinitionId;
    const FGuid Session = FGuid::NewGuid();
    F.Developer->ServerBeginGraphPreview(DefinitionId, Session);
    const auto& State = F.Developer->GetDeveloperState();
    if (!TestTrue(TEXT("Direct PIE's Unselected Mage gets an accepted real graph"),
                  State.bPreviewActive && State.AcceptedGraph.bReady && !State.AcceptedGraph.Nodes.IsEmpty()))
    {
        AddInfo(State.Error);
        return false;
    }
    const auto Handle = F.Developer->GetPreviewHandle();
    const auto* Query = State.AcceptedGraph.Nodes.FindByPredicate(
        [](const FAbilityRuntimeGraphNodeView& Node)
        {
            return Node.TypeId == TEXT("Query.PetrifyArea");
        });
    if (!TestNotNull(TEXT("Real radius node is selectable"), Query))
    {
        return false;
    }
    FAbilityRuntimeDeveloperEdit Edit;
    Edit.DefinitionId = DefinitionId;
    Edit.ModifierId = TEXT("Scale.Radius");
    Edit.TargetNodeId = Query->NodeId;
    Edit.Value = 2.f;
    Edit.ExpectedPreviewSessionId = Session;
    Edit.ExpectedRevision = State.Revision;
    Edit.ExpectedBaseHash = State.BaseHash;
    Edit.ExpectedAcceptedHash = State.ContentHash;
    TestTrue(TEXT("Unselected Mage can preview a real node edit"), F.Developer->PreviewGraphEdit(Edit, Error));
    const FGuid Token = State.DraftToken;
    TestTrue(TEXT("Exact draft applies with no selected career"),
             F.Developer->ApplyGraphDraft(Token, State.Revision, Error));
    TestTrue(TEXT("Applied graph retains the exact temporary spec"), F.Developer->GetPreviewHandle() == Handle);
    TestEqual(TEXT("Composing never selects or persists a profession"), F.State->Profession,
              EPlayerProfession::Unselected);
    F.Developer->ServerCloseGraphPreview(FGuid::NewGuid(), Session, false);
    TestFalse(TEXT("Exact close ends the preview"), State.bPreviewActive);
    TestNull(TEXT("Temporary spec is actually removed"), F.PlayerASC->FindAbilitySpecFromHandle(Handle));
    TestEqual(TEXT("Closing never changes profession"), F.State->Profession, EPlayerProfession::Unselected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDirectPieRejectsWrongAvatar,
                                 "test_25d.DynamicMage.Workshop.DirectPIE.NonMageAndForeignAvatarRemainRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDirectPieRejectsWrongAvatar::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"), Error);
    if (!TestNotNull(TEXT("Actual Mage definition exists"), Row))
    {
        return false;
    }
    F.State->Profession = EPlayerProfession::Stander;
    TestFalse(TEXT("Explicit Stander cannot bypass the Mage limit using Elementalist metadata"),
              F.Developer->BeginPreview(Row->DefinitionId, Error));
    F.State->Profession = EPlayerProfession::Unselected;
    F.Pawn->SetCharacterClass(ECharacterClass::Warrior);
    TestFalse(TEXT("Unselected melee Avatar is not accepted as the default Mage"),
              F.Developer->BeginPreview(Row->DefinitionId, Error));
    F.Pawn->SetCharacterClass(ECharacterClass::Elementalist);
    F.PlayerASC->InitAbilityActorInfo(F.State, F.Target);
    TestFalse(TEXT("An enemy Elementalist/foreign Avatar cannot borrow the player's workbench"),
              F.Developer->BeginPreview(Row->DefinitionId, Error));
    F.PlayerASC->InitAbilityActorInfo(F.State, F.Pawn);
    return true;
}
#endif
