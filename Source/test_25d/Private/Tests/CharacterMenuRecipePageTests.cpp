// 测试链：真实人物窗口/页签状态 → 内嵌工作台 → 真实目录 Begin/Close RPC → 原 ASC 临时 Spec 归还。
// 临时世界只验证生命周期与事务，不把无渲染测试声称为游戏内点击或视觉验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "UI/Widget/CharacterMenuWidget.h"
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "Components/Button.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/LocalPlayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterMenuFourthPage,
                                 "test_25d.DynamicMage.Workshop.CharacterMenu.FourthPageAndLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterMenuFourthPage::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    // 正式 Controller/Pawn/ASC 链可解析同一个 PlayerState 开发组件；不创建第二个管理器。
    auto* Controller = F.World->SpawnActor<AMyPlayerController>();
    auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    LocalPlayer->PlayerController = Controller;
    Controller->Player = LocalPlayer;
    Controller->SetPawn(F.Pawn);
    // 临时世界不走 BeginPlay 的控制器登记；FLocalPlayerContext 按世界控制器列表解析所有者。
    F.World->AddController(Controller);
    const TStrongObjectPtr<UCharacterMenuWidget> Menu(CreateWidget<UCharacterMenuWidget>(Controller));
    if (!TestNotNull(TEXT("Real local controller can own the menu"), Menu.Get()))
    {
        return false;
    }
    Menu->SetPlayerContext(FLocalPlayerContext(LocalPlayer, F.World));
    if (!TestTrue(TEXT("Embedded menu resolves the exact local controller"), Menu->GetOwningPlayer() == Controller) ||
        !TestTrue(TEXT("Local menu controller resolves the fixture pawn"), Controller->GetPawn() == F.Pawn))
    {
        return false;
    }
    const auto Slate = Menu->TakeWidget();
    Menu->ShowPage(3);
    TestEqual(TEXT("Fourth page remains selected instead of clamping to Skills"), Menu->GetActivePage(), 3);
    auto* Panel = Menu->GetRuntimeRecipePanel();
    if (!TestNotNull(TEXT("Fourth page contains the original complete graph workshop"), Panel))
    {
        return false;
    }
    auto* Begin = Cast<UButton>(Panel->GetWidgetFromName(TEXT("RecipeBegin")));
    if (!TestNotNull(TEXT("Embedded workshop builds actual Begin button"), Begin))
    {
        return false;
    }
    Begin->OnClicked.Broadcast();
    const auto Handle = F.Developer->GetPreviewHandle();
    const FGuid Session = F.Developer->GetDeveloperState().PreviewSessionId;
    TestTrue(TEXT("Begin grants a real preview and exact session"), Handle.IsValid() && Session.IsValid());
    Menu->ShowPage(0);
    Menu->ShowPage(3);
    TestTrue(TEXT("Tab switching reuses the same panel and temporary spec"),
             Menu->GetRuntimeRecipePanel() == Panel && F.Developer->GetPreviewHandle() == Handle &&
                 F.Developer->GetDeveloperState().PreviewSessionId == Session);
    Panel->HideForCasting();
    TestFalse(TEXT("Hide for casting hides the parent input menu"), Menu->IsVisible());
    TestTrue(TEXT("Hide for casting retains the same actual spec"), F.Developer->GetPreviewHandle() == Handle);
    Menu->ShowPage(3);
    TestTrue(TEXT("Reopen restores embedded content"), Panel->IsVisible() && Menu->IsVisible());
    Menu->RequestCloseMenu();
    TestFalse(TEXT("Exact close acknowledgement hides the parent"), Menu->IsVisible());
    TestTrue(TEXT("The child records acknowledged close"), Panel->IsSessionClosed());
    TestFalse(TEXT("Normal parent close removes temporary ability"), F.Developer->GetPreviewHandle().IsValid());
    TestNull(TEXT("Original temporary spec is actually gone"), F.PlayerASC->FindAbilitySpecFromHandle(Handle));
    Menu->ShowPage(3);
    TestTrue(TEXT("Closed embedded page can start a new session"), Panel->IsVisible() && !Panel->IsSessionClosed());
    Begin->OnClicked.Broadcast();
    TestTrue(TEXT("Reopened Begin gets a distinct nonce"),
             F.Developer->GetDeveloperState().PreviewSessionId.IsValid() &&
                 F.Developer->GetDeveloperState().PreviewSessionId != Session);
    Menu->ShowPage(1);
    Menu->SetVisibility(ESlateVisibility::Collapsed);
    TestFalse(TEXT("Closing another tab also cleans the workshop lease"), F.Developer->GetPreviewHandle().IsValid());
    return true;
}
#endif
