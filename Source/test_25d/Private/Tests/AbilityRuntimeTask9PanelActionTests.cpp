// 测试链：实际目录预览 -> 真正UMG选择器/点击委托 -> 原Developer RPC/Compile事务 -> 准确撤回。
// 这是无渲染的UMG交互逻辑验证，不冒充OS点击、真实焦点或网络；那些仍由PIE场景验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9ActualPanelRegisteredAction,
                                 "test_25d.DynamicMage.Task9.Panel.SelectRegisteredPortApplyAndRemoveOwnedEdit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9ActualPanelRegisteredAction::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row =
        F.Definition(TEXT("/Game/UnifiedAbilities/MageBatch/GA_ArcaneMeditation.GA_ArcaneMeditation_C"), Error);
    if (!TestNotNull(TEXT("Actual passive directory row exists"), Row) ||
        !TestTrue(TEXT("Actual preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        return false;
    }
    const auto Handle = F.Developer->GetPreviewHandle();
    const TStrongObjectPtr<UAbilityRuntimeRecipePanel> Panel(
        CreateWidget<UAbilityRuntimeRecipePanel>(F.World, UAbilityRuntimeRecipePanel::StaticClass()));
    if (!TestNotNull(TEXT("Real native panel can be constructed"), Panel.Get()))
    {
        return false;
    }
    Panel->InitializePanel(F.Developer);
    const auto Slate = Panel->TakeWidget();
    (void)Slate; // 保持真实Slate树存活，直到选择/点击/撤回完成。
    auto* Modifier = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipeModifier")));
    auto* Fragment = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipeFragment")));
    auto* Port = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipePort")));
    auto* Value = Cast<USpinBox>(Panel->GetWidgetFromName(TEXT("RecipeValue")));
    auto* Radius = Cast<USpinBox>(Panel->GetWidgetFromName(TEXT("RecipeRadius")));
    auto* Apply = Cast<UButton>(Panel->GetWidgetFromName(TEXT("RecipeApply")));
    auto* Remove = Cast<UButton>(Panel->GetWidgetFromName(TEXT("RecipeRemove")));
    if (!TestNotNull(TEXT("Modifier is a real semantic panel control"), Modifier) ||
        !TestNotNull(TEXT("Registered fragment selection is present"), Fragment) ||
        !TestNotNull(TEXT("Only declared typed ports are selectable"), Port) ||
        !TestNotNull(TEXT("Actual fragment duration control exists"), Value) ||
        !TestNotNull(TEXT("Actual area radius control exists"), Radius) ||
        !TestNotNull(TEXT("Actual Apply action exists"), Apply) ||
        !TestNotNull(TEXT("Actual Remove action exists"), Remove))
    {
        return false;
    }
    Modifier->SetSelectedOption(TEXT("Append.Registered"));
    Fragment->SetSelectedOption(TEXT("Area.Petrify"));
    Port->SetSelectedOption(TEXT("Applied"));
    Value->SetValue(1.5f);
    Radius->SetValue(600.f);
    TestEqual(TEXT("UMG selected a declared actual OwnedEffect port"), Port->GetSelectedOption(),
              FString(TEXT("Applied")));
    const auto Revision = F.Developer->GetDeveloperState().Revision;
    Apply->OnClicked.Broadcast();
    TestTrue(TEXT("Real UMG click submitted a successfully parsed server edit"),
             F.Developer->GetDeveloperState().Revision > Revision);
    TestTrue(TEXT("Actual selected ability owns the new contribution"),
             F.Developer->HasContributions(Row->Base.AbilityTag));
    Remove->OnClicked.Broadcast();
    TestFalse(TEXT("Remove sends the same exact fragment/port owned source identity"),
              F.Developer->HasContributions(Row->Base.AbilityTag));
    TestTrue(TEXT("UI edit withdrawal retains the same temporary spec"), F.Developer->GetPreviewHandle() == Handle);
    Panel->ClosePanel();
    TestTrue(TEXT("The panel exposes the exact request acknowledged by its original close handler"),
             Panel->GetLastCloseRequestId().IsValid() &&
                 Panel->GetLastCloseRequestId() == F.Developer->LastCloseAcknowledgedId);
    return true;
}
#endif
