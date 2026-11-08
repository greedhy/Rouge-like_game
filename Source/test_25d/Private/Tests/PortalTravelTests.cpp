/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Travel.NativePlayerStateUsesLegacyMenuStart
 * - test_25d.Travel.AllPlayersStayForThreeSeconds
 * - test_25d.Travel.LeavingAndRosterChangesResetCountdown
 * - test_25d.Travel.ExplicitSpawnSlotsAndInvalidConfiguration
 * - test_25d.Travel.PlayerSlotAndProgressSurviveSeamlessCopy
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "CheckPoint/PartyPortalCountdown.h"
#include "CheckPoint/TravelPlayerStart.h"
#include "CheckPoint/MapEntrance.h"
#include "Player/MyPlayerState.h"
#include "Game/LoadScreenSaveGame.h"
#include "Engine/World.h"
#include "Game/MyGameModeBase.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortalPreservesMenuStart, "test_25d.Travel.NativePlayerStateUsesLegacyMenuStart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Travel.NativePlayerStateUsesLegacyMenuStart 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPortalPreservesMenuStart::RunTest(const FString&)
{
    UWorld* W = UWorld::CreateWorld(EWorldType::Game, false);
    auto* GM = W->SpawnActor<AMyGameModeBase>();
    auto* Controller = W->SpawnActor<APlayerController>();
    Controller->PlayerState = W->SpawnActor<APlayerState>();
    auto* Start = W->SpawnActor<APlayerStart>();
    TestEqual(TEXT("Menu worlds without party PlayerStates keep their native spawn flow"),
              GM->FindPlayerStart_Implementation(Controller, TEXT("")), static_cast<AActor*>(Start));
    W->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortalWaitsForWholeParty, "test_25d.Travel.AllPlayersStayForThreeSeconds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Travel.AllPlayersStayForThreeSeconds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPortalWaitsForWholeParty::RunTest(const FString&)
{
    FPartyPortalCountdown C;
    TestFalse(TEXT("One absent player prevents travel indefinitely"), C.Update({10, 20}, false, 100, 3));
    TestFalse(TEXT("All ready starts a fresh countdown"), C.Update({10, 20}, true, 101, 3));
    TestFalse(TEXT("No travel before the full duration"), C.Update({20, 10}, true, 103.99, 3));
    TestTrue(TEXT("All players staying for three seconds permits travel"), C.Update({10, 20}, true, 104, 3));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortalResetsOnDeparture, "test_25d.Travel.LeavingAndRosterChangesResetCountdown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Travel.LeavingAndRosterChangesResetCountdown 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPortalResetsOnDeparture::RunTest(const FString&)
{
    FPartyPortalCountdown C;
    C.Update({1, 2}, true, 0, 3);
    TestFalse(TEXT("Leaving cancels"), C.Update({1, 2}, false, 2.9, 3));
    TestFalse(TEXT("Returning cannot resume old countdown"), C.Update({1, 2}, true, 3, 3));
    TestFalse(TEXT("Joined player restarts the full-party interval"), C.Update({1, 2, 3}, true, 5, 3));
    TestFalse(TEXT("Disconnected player restarts the remaining party interval"), C.Update({1, 2}, true, 7, 3));
    TestFalse(TEXT("Still waiting"), C.Update({1, 2}, true, 9.9, 3));
    TestTrue(TEXT("Fresh complete interval"), C.Update({1, 2}, true, 10, 3));
    TestFalse(TEXT("Empty party never travels"), C.Update({}, true, 100, 3));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortalResolvesExplicitSlots,
                                 "test_25d.Travel.ExplicitSpawnSlotsAndInvalidConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Travel.ExplicitSpawnSlotsAndInvalidConfiguration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPortalResolvesExplicitSlots::RunTest(const FString&)
{
    UWorld* W = UWorld::CreateWorld(EWorldType::Game, false);
    auto* First = W->SpawnActor<ATravelPlayerStart>(FVector(100, 200, 100), FRotator::ZeroRotator);
    auto* Second = W->SpawnActor<ATravelPlayerStart>(FVector(7000, 9000, 100), FRotator::ZeroRotator);
    First->PlayerStartTag = Second->PlayerStartTag = TEXT("Entry");
    Second->PlayerSlot = 1;
    TArray<APlayerStart*> Starts = {Second, First};
    FString Error;
    TestEqual(TEXT("Order of actors does not choose player zero"),
              ATravelPlayerStart::FindForSlot(Starts, TEXT("Entry"), 0, Error), static_cast<APlayerStart*>(First));
    TestEqual(TEXT("Second player receives the explicit remote location"),
              ATravelPlayerStart::FindForSlot(Starts, TEXT("Entry"), 1, Error), static_cast<APlayerStart*>(Second));
    TestNull(TEXT("Missing slot cannot pick a random spawn"),
             ATravelPlayerStart::FindForSlot(Starts, TEXT("Entry"), 2, Error));
    TestNull(TEXT("Missing group cannot pick a random spawn"),
             ATravelPlayerStart::FindForSlot(Starts, TEXT("Typo"), 0, Error));
    Second->PlayerSlot = 0;
    TestNull(TEXT("Duplicate group and slot is rejected"),
             ATravelPlayerStart::FindForSlot(Starts, TEXT("Entry"), 0, Error));
    auto* Portal = W->SpawnActor<AMapEntrance>();
    Portal->PlayerStartTag = TEXT("Exit");
    TestNull(TEXT("A portal is never an arrival point"),
             ATravelPlayerStart::FindForSlot({Portal}, TEXT("Exit"), 0, Error));
    W->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortalPlayerIdentitySurvivesTravel,
                                 "test_25d.Travel.PlayerSlotAndProgressSurviveSeamlessCopy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Travel.PlayerSlotAndProgressSurviveSeamlessCopy 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPortalPlayerIdentitySurvivesTravel::RunTest(const FString&)
{
    UWorld* W = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Source = W->SpawnActor<AMyPlayerState>();
    auto* Destination = W->SpawnActor<AMyPlayerState>();
    Source->SetTravelPlayerSlot(3);
    Source->SetLevel(8);
    Source->SetXP(725);
    auto* Snapshot = NewObject<ULoadScreenSaveGame>();
    Snapshot->PlayerLevel = 8;
    Source->SetTravelProgress(Snapshot, 37, 24);
    Source->CopyProperties(Destination);
    TestEqual(TEXT("Stable player slot is copied"), Destination->GetTravelPlayerSlot(), 3);
    TestEqual(TEXT("Per-player snapshot is copied"), Destination->GetTravelProgress(), Snapshot);
    TestEqual(TEXT("Stats are not replaced by another player's save"), Destination->GetPlayerLevel(), 8);
    TestEqual(TEXT("Current health survives travel"), Destination->GetTravelHealth(), 37.f);
    TestEqual(TEXT("Current mana survives travel"), Destination->GetTravelMana(), 24.f);
    W->DestroyWorld(false);
    return true;
}
#endif
