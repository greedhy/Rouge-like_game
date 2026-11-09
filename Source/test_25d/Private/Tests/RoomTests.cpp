/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Rooms.SwordTrialOverridesLegacySaveArrival
 * - test_25d.Rooms.StanderTrialOverridesLegacySaveArrival
 * - test_25d.Rooms.PasswordValidationAndFingerprint
 * - test_25d.Rooms.MatchFiltersAndAmbiguousPassword
 * - test_25d.Rooms.OnlyOwnerCanManageLobby
 * - test_25d.Rooms.InvalidSelectionNeverStartsTravel
 * - test_25d.Rooms.MemberArrivalPreservesPendingHostOperations
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Online/RoomSubsystem.h"
#include "Online/RoomLobby.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Game/MyGameInstance.h"
#include "Engine/Engine.h"
#include "Game/MyGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "Character/StanderPlayer.h"
#include "CheckPoint/TravelPlayerStart.h"
#include "Player/MyPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavedProfessionTest, "test_25d.Rooms.SavedProfession",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// Verify serialization and the same per-controller pawn selection used after seamless travel.
bool FSavedProfessionTest::RunTest(const FString&)
{
    auto* Save = NewObject<ULoadScreenSaveGame>();
    TestTrue(TEXT("Legacy default is Mage"), Save->Profession == EPlayerProfession::Mage);
    Save->Profession = EPlayerProfession::Stander;
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Save serialized"), UGameplayStatics::SaveGameToMemory(Save, Bytes)))
    {
        return false;
    }
    const auto* Loaded = Cast<ULoadScreenSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Save loaded"), Loaded))
    {
        return false;
    }
    TestTrue(TEXT("Standard survives reload"), Loaded->Profession == EPlayerProfession::Stander);

    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Mode = World->SpawnActor<AMyGameModeBase>();
    auto* Mage = World->SpawnActor<APlayerController>();
    auto* Standard = World->SpawnActor<APlayerController>();
    Mage->PlayerState = World->SpawnActor<AMyPlayerState>();
    Standard->PlayerState = World->SpawnActor<AMyPlayerState>();
    auto* LobbyState = World->SpawnActor<AMyPlayerState>();
    LobbyState->Profession = Loaded->Profession;
    LobbyState->SeamlessTravelTo(Standard->PlayerState);
    Mage->GetPlayerState<AMyPlayerState>()->Profession = EPlayerProfession::Mage;

    const UClass* StandardPawn = Mode->GetDefaultPawnClassForController(Standard);
    TestTrue(TEXT("Standard uses authored pawn after state copy"),
             StandardPawn && StandardPawn->IsChildOf(AStanderPlayer::StaticClass()));
    TestEqual(TEXT("Mage keeps the game mode pawn in the same room"),
              Mode->GetDefaultPawnClassForController(Mage), Mode->DefaultPawnClass.Get());
    TestNotNull(TEXT("Standard resolves its own ability catalog"), Mode->GetAbilityInfoForActor(Standard->PlayerState));
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwordTrialArrival, "test_25d.Rooms.SwordTrialOverridesLegacySaveArrival",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.SwordTrialOverridesLegacySaveArrival 回归，失败信息由下方断言交给自动化框架收集。 */
bool FSwordTrialArrival::RunTest(const FString&)
{
    auto* GI = NewObject<UMyGameInstance>(GEngine);
    GI->InitializeStandalone();
    auto* W = GI->GetWorld();
    auto* Context = GI->GetWorldContext();
    GI->LoadSlotName = TEXT("SwordShield_Automation");
    GI->PlayerStartTag = TEXT("Dungeon1PlayerStart1");
    auto* Rooms = GI->GetSubsystem<URoomSubsystem>();
    Rooms->ConfigureSelection(
        TEXT("NewSave"),
        TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/RoguelikeGeneration/Maps/L_RoguelikeRun.L_RoguelikeRun"))));
    Rooms->StartSwordShieldTrial();
    const FString TrialURL = Context->TravelURL;
    AddInfo(FString::Printf(TEXT("Selection=%d State=%d Error=%s URL=%s"), Rooms->HasSelection(), int32(Rooms->State),
                            *Rooms->LastError.ToString(), *TrialURL));
    const FURL ParsedTrial(nullptr, *TrialURL, TRAVEL_Absolute);
    TestEqual(TEXT("Trial requests destination entry despite legacy save tag"),
              FString(ParsedTrial.GetOption(TEXT("ArrivalGroup="), TEXT(""))), FString(TEXT("RG_Entrance")));
    auto* Mode = W->SpawnActor<AMyGameModeBase>();
    // InitGame receives the option suffix, not the complete map URL.
    const int32 Query = TrialURL.Find(TEXT("?"));
    Mode->OptionsString = Query == INDEX_NONE ? FString() : TrialURL.Mid(Query);
    auto* PC = W->SpawnActor<APlayerController>();
    PC->PlayerState = W->SpawnActor<AMyPlayerState>();
    auto* Start = W->SpawnActor<ATravelPlayerStart>();
    Start->PlayerStartTag = TEXT("RG_Entrance");
    Start->bDefaultEntryGroup = true;
    TestEqual(TEXT("Real arrival resolver finds player zero"), Mode->FindPlayerStart_Implementation(PC, TEXT("")),
              static_cast<AActor*>(Start));
    TestEqual(TEXT("Trial does not overwrite selected save arrival"), GI->PlayerStartTag,
              FName(TEXT("Dungeon1PlayerStart1")));
    Rooms->State = ERoomFlowState::Idle;
    Rooms->StartSinglePlayer();
    TestFalse(TEXT("Original single-player retains legacy save spawn selection"),
              FURL(nullptr, *Context->TravelURL, TRAVEL_Absolute).HasOption(TEXT("ArrivalGroup=")));
    Context->TravelURL.Empty();
    GI->Shutdown();
    GEngine->DestroyWorldContext(W);
    W->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStanderTrialArrival, "test_25d.Rooms.StanderTrialOverridesLegacySaveArrival",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.StanderTrialOverridesLegacySaveArrival 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStanderTrialArrival::RunTest(const FString&)
{
    auto* GI = NewObject<UMyGameInstance>(GEngine);
    GI->InitializeStandalone();
    auto* W = GI->GetWorld();
    auto* Context = GI->GetWorldContext();
    GI->LoadSlotName = TEXT("Stander_Automation");
    GI->PlayerStartTag = TEXT("Dungeon1PlayerStart1");
    auto* Rooms = GI->GetSubsystem<URoomSubsystem>();
    Rooms->ConfigureSelection(
        TEXT("NewSave"),
        TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/RoguelikeGeneration/Maps/L_RoguelikeRun.L_RoguelikeRun"))));
    Rooms->StartStanderTrial();
    const FString TrialURL = Context->TravelURL;
    AddInfo(FString::Printf(TEXT("Selection=%d State=%d Error=%s URL=%s"), Rooms->HasSelection(), int32(Rooms->State),
                            *Rooms->LastError.ToString(), *TrialURL));
    const FURL ParsedTrial(nullptr, *TrialURL, TRAVEL_Absolute);
    TestEqual(TEXT("Trial requests destination entry despite legacy save tag"),
              FString(ParsedTrial.GetOption(TEXT("ArrivalGroup="), TEXT(""))), FString(TEXT("RG_Entrance")));
    auto* Mode = W->SpawnActor<AMyGameModeBase>();
    // InitGame receives the option suffix, not the complete map URL.
    const int32 Query = TrialURL.Find(TEXT("?"));
    Mode->OptionsString = Query == INDEX_NONE ? FString() : TrialURL.Mid(Query);
    auto* PC = W->SpawnActor<APlayerController>();
    PC->PlayerState = W->SpawnActor<AMyPlayerState>();
    auto* Start = W->SpawnActor<ATravelPlayerStart>();
    Start->PlayerStartTag = TEXT("RG_Entrance");
    Start->bDefaultEntryGroup = true;
    TestEqual(TEXT("Real arrival resolver finds player zero"), Mode->FindPlayerStart_Implementation(PC, TEXT("")),
              static_cast<AActor*>(Start));
    TestEqual(TEXT("Trial does not overwrite selected save arrival"), GI->PlayerStartTag,
              FName(TEXT("Dungeon1PlayerStart1")));
    Rooms->State = ERoomFlowState::Idle;
    Rooms->StartSinglePlayer();
    TestFalse(TEXT("Original single-player retains legacy save spawn selection"),
              FURL(nullptr, *Context->TravelURL, TRAVEL_Absolute).HasOption(TEXT("ArrivalGroup=")));
    Context->TravelURL.Empty();
    GI->Shutdown();
    GEngine->DestroyWorldContext(W);
    W->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomPasswordRules, "test_25d.Rooms.PasswordValidationAndFingerprint",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.PasswordValidationAndFingerprint 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRoomPasswordRules::RunTest(const FString&)
{
    TestTrue(TEXT("Generated room password is valid"),
             URoomSubsystem::IsValidPassword(URoomSubsystem::GeneratePassword()));
    TestTrue(TEXT("Six digits"), URoomSubsystem::IsValidPassword(TEXT("123456")));
    TestTrue(TEXT("Twelve mixed ASCII characters"), URoomSubsystem::IsValidPassword(TEXT("AbCd12345678")));
    for (const FString& Invalid : TArray<FString>{TEXT(""), TEXT("12345"), TEXT("1234567890123"), TEXT("123?45"),
                                                  TEXT("123 45"), TEXT("123&45"), TEXT("１２３４５６")})
        TestFalse(TEXT("Reject invalid or URL-injecting password"), URoomSubsystem::IsValidPassword(Invalid));
    TestNotEqual(TEXT("Password case is significant"), URoomSubsystem::PasswordFingerprint(TEXT("ABC123")),
                 URoomSubsystem::PasswordFingerprint(TEXT("abc123")));
    TestNotEqual(TEXT("Never advertise the plain password"), URoomSubsystem::PasswordFingerprint(TEXT("123456")),
                 FString(TEXT("123456")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomMatchRules, "test_25d.Rooms.MatchFiltersAndAmbiguousPassword",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.MatchFiltersAndAmbiguousPassword 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRoomMatchRules::RunTest(const FString&)
{
    auto Make = [](const FString& Password, bool Open = true, int32 Free = 1)
    {
        FOnlineSessionSearchResult R;
        R.Session.NumOpenPublicConnections = Free;
        R.Session.SessionSettings.Set(URoomSubsystem::ProtocolKey, FString(TEXT("test25d-lan-v1")),
                                      EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        R.Session.SessionSettings.Set(URoomSubsystem::PasswordKey, URoomSubsystem::PasswordFingerprint(Password),
                                      EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        R.Session.SessionSettings.Set(URoomSubsystem::OpenKey, Open,
                                      EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        return R;
    };
    const auto Good = Make(TEXT("123456"));
    TestEqual(TEXT("Correct password selects one room"),
              URoomSubsystem::MatchingRoomIndices({Make(TEXT("654321")), Good}, TEXT("123456")).Num(), 1);
    TestEqual(TEXT("Full and started rooms are excluded"),
              URoomSubsystem::MatchingRoomIndices({Make(TEXT("123456"), true, 0), Make(TEXT("123456"), false)},
                                                  TEXT("123456"))
                  .Num(),
              0);
    TestEqual(TEXT("Duplicates remain detectable; never silently choose first"),
              URoomSubsystem::MatchingRoomIndices({Good, Good}, TEXT("123456")).Num(), 2);
    FOnlineSessionSearchResult Foreign;
    TestEqual(TEXT("Ignore unrelated games"), URoomSubsystem::MatchingRoomIndices({Foreign}, TEXT("123456")).Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomHostAuthority, "test_25d.Rooms.OnlyOwnerCanManageLobby",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.OnlyOwnerCanManageLobby 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRoomHostAuthority::RunTest(const FString&)
{
    UWorld* W = UWorld::CreateWorld(EWorldType::Game, false);
    auto* State = W->SpawnActor<ARoomLobbyGameState>();
    auto* Host = W->SpawnActor<ARoomLobbyController>();
    auto* Guest = W->SpawnActor<ARoomLobbyController>();
    Host->PlayerState = W->SpawnActor<APlayerState>();
    Guest->PlayerState = W->SpawnActor<APlayerState>();
    State->RoomOwner = Host->PlayerState;
    TestTrue(TEXT("Room owner can manage"), State->CanManage(Host));
    TestFalse(TEXT("Guest cannot manage"), State->CanManage(Guest));
    TestFalse(TEXT("Missing requester cannot manage"), State->CanManage(nullptr));
    State->bStarting = true;
    TestFalse(TEXT("Cannot change room during travel"), State->CanManage(Host));
    W->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomInvalidSelection, "test_25d.Rooms.InvalidSelectionNeverStartsTravel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.InvalidSelectionNeverStartsTravel 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRoomInvalidSelection::RunTest(const FString&)
{
    auto* Rooms = NewObject<URoomSubsystem>(NewObject<UMyGameInstance>());
    TestFalse(TEXT("No selected save"), Rooms->HasSelection());
    Rooms->StartSinglePlayer();
    TestFalse(TEXT("Failure is retryable"), Rooms->IsBusy());
    TestFalse(TEXT("Explains missing selection"), Rooms->LastError.IsEmpty());
    Rooms->JoinRoom(TEXT("bad?pw"));
    TestFalse(TEXT("Invalid password never starts search"), Rooms->IsBusy());
    TestEqual(TEXT("State remains idle"), Rooms->State, ERoomFlowState::Idle);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomReentrancy, "test_25d.Rooms.MemberArrivalPreservesPendingHostOperations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Rooms.MemberArrivalPreservesPendingHostOperations 回归，失败信息由下方断言交给自动化框架收集。 */
bool FRoomReentrancy::RunTest(const FString&)
{
    auto* Rooms = NewObject<URoomSubsystem>(NewObject<UMyGameInstance>());
    Rooms->State = ERoomFlowState::Updating;
    Rooms->NotifyLobbyReady();
    TestEqual(TEXT("An arriving member cannot cancel password update"), Rooms->State, ERoomFlowState::Updating);
    Rooms->State = ERoomFlowState::Starting;
    Rooms->NotifyLobbyReady();
    TestEqual(TEXT("An arriving member cannot cancel starting the game"), Rooms->State, ERoomFlowState::Starting);
    Rooms->CreateRoom();
    Rooms->JoinRoom(TEXT("123456"));
    Rooms->StartSinglePlayer();
    TestEqual(TEXT("Repeated menu actions do not replace an active operation"), Rooms->State, ERoomFlowState::Starting);
    Rooms->State = ERoomFlowState::Joining;
    Rooms->NotifyLobbyReady();
    TestEqual(TEXT("A joining local player transitions to lobby"), Rooms->State, ERoomFlowState::InLobby);
    return true;
}
#endif
