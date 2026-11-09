/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 作用域：UGameInstanceSubsystem，由引擎随每个GameInstance创建/销毁；同一进程多个PIE实例各有一份，不是跨进程单例，普通地图旅行期间仍存活。
 * 主链路：模式/房间UI -> CreateRoom或JoinRoom -> OnlineSession异步完成委托 -> ServerTravel/ClientTravel -> LoadedMap更新本地房间流程状态。
 * 同步边界：子系统自身不复制。会话发现与连接走OnlineSubsystem；State/LastError及OnChanged是本地流程状态，不能当作服务端复制的大厅业务状态。
 * 生命周期：Initialize绑定引擎网络失败、旅行失败、地图加载及0.25秒Ticker；各会话操作保存委托句柄，完成/失败/超时移除。Deinitialize清除会话委托、发起会话销毁、解绑引擎事件和Ticker，防止旅行或退出后旧回调继续使用对象。
 */
#include "Online/RoomSubsystem.h"
#include "Online/RoomLobby.h"
#include "Game/MyGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystemUtils.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PendingNetGame.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "LanRooms"
#if WITH_DEV_AUTOMATION_TESTS
void TickRoomNetworkProbe(URoomSubsystem* Rooms);
#endif
const FName URoomSubsystem::ProtocolKey(TEXT("ROOM_PROTOCOL"));
const FName URoomSubsystem::PasswordKey(TEXT("ROOM_PASSWORD_HASH"));
const FName URoomSubsystem::OpenKey(TEXT("ROOM_OPEN"));

// GameInstance初始化时绑定跨地图事件与核心Ticker；这些订阅的生命周期长于单个World，必须在Deinitialize按句柄解除。
void URoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::NetworkFailure);
    TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::TravelFailure);
    MapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::LoadedMap);
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), .25f);
}
// 先断开会话回调，再发起已有会话销毁；不等待销毁异步结果来维持本对象。随后移除全局引擎/地图委托和Ticker。
void URoomSubsystem::Deinitialize()
{
    ClearSessionDelegates();
    if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
        Sessions->DestroySession(NAME_GameSession);
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkHandle);
        GEngine->OnTravelFailure().Remove(TravelHandle);
    }
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    Super::Deinitialize();
}
bool URoomSubsystem::IsValidPassword(const FString& P)
{
    if (P.Len() < 6 || P.Len() > 12)
        return false;
    for (TCHAR C : P)
        if (!((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')))
            return false;
    return true;
}
FString URoomSubsystem::PasswordFingerprint(const FString& P)
{
    // Discovery identifier, not an internet authentication scheme. Admission is checked again by the server.
    FTCHARToUTF8 Bytes(*(TEXT("test25d-lan-v1:") + P));
    return FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
}
FString URoomSubsystem::GeneratePassword()
{
    return FString::Printf(TEXT("%06u"), GetTypeHash(FGuid::NewGuid()) % 1000000);
}
TArray<int32> URoomSubsystem::MatchingRoomIndices(const TArray<FOnlineSessionSearchResult>& Results,
                                                  const FString& Password)
{
    TArray<int32> Matches;
    const FString Fingerprint = PasswordFingerprint(Password);
    for (int32 I = 0; I < Results.Num(); ++I)
    {
        const auto& R = Results[I];
        FString Protocol, Hash;
        bool Open = false;
        R.Session.SessionSettings.Get(ProtocolKey, Protocol);
        R.Session.SessionSettings.Get(PasswordKey, Hash);
        R.Session.SessionSettings.Get(OpenKey, Open);
        if (Protocol == TEXT("test25d-lan-v1") && Hash == Fingerprint && Open && R.Session.NumOpenPublicConnections > 0)
            Matches.Add(I);
    }
    return Matches;
}
bool URoomSubsystem::HasSelection() const
{
    const auto* GI = Cast<UMyGameInstance>(GetGameInstance());
    return GI && !GI->LoadSlotName.IsEmpty() && !SelectedPlayerName.IsEmpty() && !DefaultMap.IsNull();
}
void URoomSubsystem::ConfigureSelection(const FString& PlayerName, const TSoftObjectPtr<UWorld>& Map)
{
    SelectedPlayerName = PlayerName.Left(24);
    DefaultMap = Map;
    bReturnToMode = false;
    ClearError();
}
FString URoomSubsystem::GetDefaultMapPackage() const
{
    return FSoftObjectPath(UWorld::RemovePIEPrefix(DefaultMap.ToSoftObjectPath().ToString())).GetLongPackageName();
}
bool URoomSubsystem::IsBusy() const
{
    return State != ERoomFlowState::Idle && State != ERoomFlowState::InLobby;
}
// 记录本地状态及墙钟截止时间，并广播OnChanged供本地UI刷新；此广播不跨网络。
void URoomSubsystem::SetState(ERoomFlowState NewState, double Timeout)
{
    State = NewState;
    Deadline = Timeout > 0 ? FPlatformTime::Seconds() + Timeout : 0;
    OnChanged.Broadcast();
}
void URoomSubsystem::ClearError()
{
    LastError = FText::GetEmpty();
    OnChanged.Broadcast();
}
void URoomSubsystem::Fail(const FText& Message, bool bKeepLobby)
{
    LastError = Message;
    SetState(bKeepLobby ? ERoomFlowState::InLobby : ERoomFlowState::Idle);
}
bool URoomSubsystem::EnsureSessions()
{
    if (!Sessions.IsValid() && GetWorld())
        Sessions = Online::GetSessionInterface(GetWorld(), FName(TEXT("NULL")));
    if (!Sessions.IsValid())
    {
        Fail(LOCTEXT("Unavailable", "局域网联机服务不可用，请重新启动游戏。"));
        return false;
    }
    return true;
}
// 集中解除已登记的OnlineSession委托；切换操作、失败和退出都调用，避免旧操作完成后误推进新流程。
void URoomSubsystem::ClearSessionDelegates()
{
    if (!Sessions.IsValid())
        return;
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
    Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
    Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
    Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
    Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
    CreateHandle.Reset();
    FindHandle.Reset();
    JoinHandle.Reset();
    UpdateHandle.Reset();
    DestroyHandle.Reset();
}
void URoomSubsystem::StartSinglePlayer()
{
    if (IsBusy())
        return;
    if (!HasSelection())
    {
        Fail(LOCTEXT("SelectSave", "请先选择一个有效存档。"));
        return;
    }
    bSwordShieldTrial = false;
    bStanderTrial = false;
    ClearError();
    DestroyThen(EAfterDestroy::Single);
}
void URoomSubsystem::StartSwordShieldTrial()
{
    if (IsBusy() || State == ERoomFlowState::InLobby)
        return;
    if (!HasSelection())
    {
        Fail(LOCTEXT("SelectSwordSave", "请先选择一个有效存档。"));
        return;
    }
    bSwordShieldTrial = true;
    bStanderTrial = false;
    ClearError();
    DestroyThen(EAfterDestroy::Single);
}
#if WITH_DEV_AUTOMATION_TESTS
void URoomSubsystem::StartStanderTrial()
{
    if (IsBusy() || State == ERoomFlowState::InLobby)
        return;
    if (!HasSelection())
    {
        Fail(LOCTEXT("SelectStanderSave", "请先选择一个有效存档。"));
        return;
    }
    bSwordShieldTrial = false;
    bStanderTrial = true;
    ClearError();
    DestroyThen(EAfterDestroy::Single);
}
#endif
void URoomSubsystem::CreateRoom()
{
    if (IsBusy() || State == ERoomFlowState::InLobby)
        return;
    if (!HasSelection())
    {
        Fail(LOCTEXT("SelectSave", "请先选择一个有效存档。"));
        return;
    }
    if (!EnsureSessions())
        return;
    ClearError();
    PendingPassword = GeneratePassword();
    DestroyThen(EAfterDestroy::Create);
}
void URoomSubsystem::JoinRoom(const FString& Password)
{
    if (IsBusy() || State == ERoomFlowState::InLobby)
        return;
    if (!IsValidPassword(Password))
    {
        Fail(LOCTEXT("PasswordFormat", "房间密码需要 6–12 位字母或数字，区分大小写。"));
        return;
    }
    if (!HasSelection())
    {
        Fail(LOCTEXT("SelectSave", "请先选择一个有效存档。"));
        return;
    }
    if (!EnsureSessions())
        return;
    ClearError();
    PendingPassword = Password;
    DestroyThen(EAfterDestroy::Search);
}
void URoomSubsystem::DestroyThen(EAfterDestroy Next)
{
    AfterDestroy = Next;
    ClearSessionDelegates();
    if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
    {
        SetState(ERoomFlowState::Leaving, 12);
        DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
            FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::Destroyed));
        if (!Sessions->DestroySession(NAME_GameSession))
            Destroyed(NAME_GameSession, false);
    }
    else
        ContinueAfterDestroy();
}
void URoomSubsystem::Destroyed(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession)
        return;
    Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
    DestroyHandle.Reset();
    if (!bSuccess && AfterDestroy != EAfterDestroy::Return)
    {
        AfterDestroy = EAfterDestroy::None;
        Fail(LOCTEXT("CloseFailed", "旧房间尚未关闭，请稍后重试。"));
        return;
    }
    ContinueAfterDestroy();
}
void URoomSubsystem::ContinueAfterDestroy()
{
    const EAfterDestroy Next = AfterDestroy;
    AfterDestroy = EAfterDestroy::None;
    bHosting = false;
    bAcceptingPlayers = false;
    HostPassword.Reset();
    switch (Next)
    {
    case EAfterDestroy::Create:
        BeginCreate();
        break;
    case EAfterDestroy::Search:
        BeginSearch();
        break;
    case EAfterDestroy::Single:
        OpenSinglePlayer();
        break;
    case EAfterDestroy::Return:
        OpenLoadMenu();
        break;
    default:
        SetState(ERoomFlowState::Idle);
    }
}
void URoomSubsystem::BeginCreate()
{
    FOnlineSessionSettings S;
    S.bIsLANMatch = true;
    S.NumPublicConnections = FMath::Clamp(MaxPlayers, 2, 8);
    S.bShouldAdvertise = true;
    S.bAllowJoinInProgress = true;
    S.bUsesPresence = false;
    S.bUseLobbiesIfAvailable = false;
    S.Set(ProtocolKey, FString(TEXT("test25d-lan-v1")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(PasswordKey, PasswordFingerprint(PendingPassword), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(OpenKey, true, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    SetState(ERoomFlowState::Creating, 20);
    CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
    if (!Sessions->CreateSession(0, NAME_GameSession, S))
        Created(NAME_GameSession, false);
}
void URoomSubsystem::Created(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession || State != ERoomFlowState::Creating)
        return;
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
    CreateHandle.Reset();
    if (!bSuccess)
    {
        Fail(LOCTEXT("CreateFailed", "创建房间失败，请检查网络后重试。"));
        return;
    }
    const auto* GI = Cast<UMyGameInstance>(GetGameInstance());
    if (!GI || GI->RoomLobbyMap.IsNull())
    {
        Fail(LOCTEXT("MissingLobby", "房间地图尚未配置，请检查 GameInstance 的 RoomLobbyMap。"));
        return;
    }
    HostPassword = PendingPassword;
    bHosting = true;
    bAcceptingPlayers = true;
    UGameplayStatics::OpenLevelBySoftObjectPtr(this, GI->RoomLobbyMap, true, TEXT("listen"));
}
void URoomSubsystem::BeginSearch()
{
    Search = MakeShared<FOnlineSessionSearch>();
    Search->bIsLanQuery = true;
    Search->MaxSearchResults = 100;
    Search->TimeoutInSeconds = 6;
    SetState(ERoomFlowState::Searching, 12);
    FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
        FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::Found));
    if (!Sessions->FindSessions(0, Search.ToSharedRef()))
        Found(false);
}
void URoomSubsystem::Found(bool bSuccess)
{
    if (State != ERoomFlowState::Searching)
        return;
    Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
    FindHandle.Reset();
    if (!bSuccess)
    {
        Fail(LOCTEXT("SearchFailed", "搜索房间失败，请确认已连接同一局域网。"));
        return;
    }
    const TArray<int32> Matches = MatchingRoomIndices(Search->SearchResults, PendingPassword);
    if (Matches.Num() == 0)
    {
        Fail(LOCTEXT("NotFound", "未找到可加入的房间。请检查密码，确认房间未满且尚未开始游戏。"));
        return;
    }
    if (Matches.Num() > 1)
    {
        Fail(LOCTEXT("Ambiguous", "发现多个相同密码的房间，请让房主修改密码后重试。"));
        return;
    }
    SetState(ERoomFlowState::Joining, 25);
    JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
        FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
    if (!Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[Matches[0]]))
        Joined(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
}
void URoomSubsystem::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    if (Name != NAME_GameSession || State != ERoomFlowState::Joining)
        return;
    Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
    JoinHandle.Reset();
    FString Address;
    APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
    if (Result != EOnJoinSessionCompleteResult::Success || !PC || !Sessions->GetResolvedConnectString(Name, Address))
    {
        Fail(LOCTEXT("JoinFailed", "加入房间失败，房间可能已满或已经关闭。"));
        return;
    }
    const auto* GI = Cast<UMyGameInstance>(GetGameInstance());
    const bool bStander = GI && GI->GetSelectedProfession() == EPlayerProfession::Stander;
    const FString ProfessionOption = bStander ? TEXT("?PlayerProfession=Stander") : TEXT("?PlayerProfession=Mage");
    PC->ClientTravel(Address + TEXT("?RoomPassword=") + PendingPassword + ProfessionOption, TRAVEL_Absolute);
}
bool URoomSubsystem::AuthorizeIncoming(const FString& Options, int32 PlayerCount, FString& Error) const
{
    if (!bHosting)
        return true;
    if (!bAcceptingPlayers)
    {
        Error = TEXT("room_started");
        return false;
    }
    if (PlayerCount >= FMath::Clamp(MaxPlayers, 2, 8))
    {
        Error = TEXT("room_full");
        return false;
    }
    if (UGameplayStatics::ParseOption(Options, TEXT("RoomPassword")) != HostPassword)
    {
        Error = TEXT("room_password_invalid");
        return false;
    }
    return true;
}
void URoomSubsystem::NotifyLobbyReady()
{
    // A new member logging in must not cancel a host's pending password update/start operation.
    if (State == ERoomFlowState::Idle || State == ERoomFlowState::Creating || State == ERoomFlowState::Joining)
        SetState(ERoomFlowState::InLobby);
    PublishLobby();
}
void URoomSubsystem::PublishLobby()
{
    if (bHosting && GetWorld())
        if (auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>())
        {
            GS->RoomPassword = HostPassword;
            GS->Capacity = FMath::Clamp(MaxPlayers, 2, 8);
            GS->ForceNetUpdate();
        }
    OnChanged.Broadcast();
}
void URoomSubsystem::ChangeRoomPassword(const FString& Password)
{
    if (IsBusy() || !bHosting || !GetWorld())
        return;
    auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>();
    if (!GS || !GS->CanManage(GetGameInstance()->GetFirstLocalPlayerController()))
        return;
    if (!IsValidPassword(Password))
    {
        Fail(LOCTEXT("PasswordFormat", "房间密码需要 6–12 位字母或数字，区分大小写。"), true);
        return;
    }
    if (!EnsureSessions())
        return;
    const auto* Existing = Sessions->GetSessionSettings(NAME_GameSession);
    if (!Existing)
    {
        Fail(LOCTEXT("RoomMissing", "房间会话已经关闭，请重新创建。"), true);
        return;
    }
    FOnlineSessionSettings Settings = *Existing;
    PendingPassword = Password;
    Settings.Set(PasswordKey, PasswordFingerprint(Password), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    ClearError();
    SetState(ERoomFlowState::Updating, 12);
    UpdateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
        FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Updated));
    if (!Sessions->UpdateSession(NAME_GameSession, Settings, true))
        Updated(NAME_GameSession, false);
}
void URoomSubsystem::StartRoomGame()
{
    if (IsBusy() || !bHosting || !GetWorld())
        return;
    auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>();
    if (!GS || !GS->CanManage(GetGameInstance()->GetFirstLocalPlayerController()))
        return;
    if (!FPackageName::DoesPackageExist(GetDefaultMapPackage()))
    {
        Fail(LOCTEXT("MissingMap", "默认游戏地图不存在，请检查地图配置。"), true);
        return;
    }
    if (!EnsureSessions())
        return;
    const auto* Existing = Sessions->GetSessionSettings(NAME_GameSession);
    if (!Existing)
        return;
    FOnlineSessionSettings Settings = *Existing;
    Settings.bShouldAdvertise = false;
    Settings.bAllowJoinInProgress = false;
    Settings.Set(OpenKey, false, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    ClearError();
    SetState(ERoomFlowState::Starting, 30);
    UpdateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
        FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Updated));
    if (!Sessions->UpdateSession(NAME_GameSession, Settings, true))
        Updated(NAME_GameSession, false);
}
void URoomSubsystem::Updated(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession || (State != ERoomFlowState::Updating && State != ERoomFlowState::Starting))
        return;
    Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
    UpdateHandle.Reset();
    if (!bSuccess)
    {
        Fail(LOCTEXT("UpdateFailed", "房间更新失败，请重试。"), true);
        return;
    }
    if (State == ERoomFlowState::Updating)
    {
        HostPassword = PendingPassword;
        SetState(ERoomFlowState::InLobby);
        PublishLobby();
        return;
    }
    bAcceptingPlayers = false;
    if (auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>())
    {
        GS->bStarting = true;
        GS->ForceNetUpdate();
    }
    if (!GetWorld()->ServerTravel(GetDefaultMapPackage(), true))
    {
        bAcceptingPlayers = true;
        if (auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>())
            GS->bStarting = false;
        Fail(LOCTEXT("StartFailed", "进入地图失败，请退出房间后重试。"), true);
    }
}
void URoomSubsystem::LeaveRoom()
{
    if (State == ERoomFlowState::Leaving)
        return;
    bReturnToMode = true;
    ClearError();
    DestroyThen(EAfterDestroy::Return);
}
void URoomSubsystem::OpenSinglePlayer()
{
    const FString Package = (bSwordShieldTrial || bStanderTrial) ? TEXT("/Game/RoguelikeGeneration/Maps/L_RoguelikeRun")
                                                                 : GetDefaultMapPackage();
    if (!FPackageName::DoesPackageExist(Package))
    {
        Fail(LOCTEXT("MissingMap", "默认游戏地图不存在，请检查地图配置。"));
        return;
    }
    bReturnToMode = false;
    SetState(ERoomFlowState::Starting, 30);
    // The menu seeds GI->PlayerStartTag from the selected save's original map.
    // This trial always enters the generated map; its explicit arrival option must
    // win over that legacy tag without modifying the selected save's spawn state.
    const FString Options =
        bStanderTrial
            ? TEXT(
                  "game=/Game/BluePrints/Characters/Stander/BP_StanderGameMode.BP_StanderGameMode_C?ArrivalGroup=RG_Entrance")
        : bSwordShieldTrial
            ? TEXT(
                  "game=/Game/BluePrints/Characters/SwordShield/BP_SwordShieldGameMode.BP_SwordShieldGameMode_C?ArrivalGroup=RG_Entrance")
            : TEXT("");
    bSwordShieldTrial = false;
    bStanderTrial = false;
    UGameplayStatics::OpenLevel(this, FName(*Package), true, Options);
}
void URoomSubsystem::OpenLoadMenu()
{
    bHosting = bAcceptingPlayers = false;
    bReturnToMode = true;
    SetState(ERoomFlowState::Idle);
    UGameplayStatics::OpenLevel(this, TEXT("/Game/levels/LoadMenu"), true);
}
// 全局地图加载委托入口；只处理属于本GameInstance的World，避免多个PIE实例互相更新流程状态。
void URoomSubsystem::LoadedMap(UWorld* World)
{
    if (!World || World->GetGameInstance() != GetGameInstance())
        return;
    if (World->GetAuthGameMode<ARoomLobbyGameMode>() || World->GetGameState<ARoomLobbyGameState>())
        NotifyLobbyReady();
    else if (State == ERoomFlowState::Starting)
        SetState(ERoomFlowState::Idle);
}
void URoomSubsystem::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type, const FString& Error)
{
    bool bOurConnection = World && World->GetGameInstance() == GetGameInstance();
    // PreLogin rejection comes from a PendingNetGame with a null World. Match its context,
    // rather than ignoring the rejection or broadcasting it into unrelated PIE instances.
    if (!World && Driver && GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.OwningGameInstance == GetGameInstance() && Context.PendingNetGame &&
                Context.PendingNetGame->NetDriver == Driver)
                bOurConnection = true;
    if (!bOurConnection || State == ERoomFlowState::Leaving || bReturnScheduled)
        return;
    const FText Message =
        Error.Contains(TEXT("room_password_invalid"))
            ? LOCTEXT("PasswordChanged", "密码错误或已被房主修改，请重新输入。")
        : Error.Contains(TEXT("room_full")) ? LOCTEXT("Full", "房间已满，请稍后重试。")
        : Error.Contains(TEXT("room_started"))
            ? LOCTEXT("Started", "房间已开始游戏，暂时无法加入。")
            : LOCTEXT("Disconnected", "连接失败或房主已离开，已返回模式选择。请检查局域网连接后重试。");
    ClearSessionDelegates();
    LastError = Message;
    bReturnToMode = true;
    bReturnScheduled = true;
    ReturnAt = FPlatformTime::Seconds() + .25;
    Deadline = 0;
}
void URoomSubsystem::TravelFailure(UWorld* World, ETravelFailure::Type, const FString& Error)
{
    NetworkFailure(World, nullptr, ENetworkFailure::FailureReceived, Error);
}
bool URoomSubsystem::Tick(float)
{
#if WITH_DEV_AUTOMATION_TESTS
    TickRoomNetworkProbe(this);
#endif
    // PostLoadMapWithWorld is not emitted for every seamless destination.
    if ((State == ERoomFlowState::Starting || State == ERoomFlowState::InLobby) && GetWorld() &&
        GetWorld()->HasBegunPlay() &&
        UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) == GetDefaultMapPackage())
        SetState(ERoomFlowState::Idle);
    const double Now = FPlatformTime::Seconds();
    if (bReturnScheduled && Now >= ReturnAt)
    {
        bReturnScheduled = false;
        DestroyThen(EAfterDestroy::Return);
    }
    if (Deadline > 0 && Now >= Deadline)
    {
        const ERoomFlowState Previous = State;
        ClearSessionDelegates();
        if (Sessions.IsValid() && Previous == ERoomFlowState::Searching)
            Sessions->CancelFindSessions();
        if (Previous == ERoomFlowState::Leaving && AfterDestroy == EAfterDestroy::Return)
        {
            OpenLoadMenu();
            return true;
        }
        Fail(LOCTEXT("Timeout", "操作超时，请检查网络后重试。"), Previous == ERoomFlowState::Updating);
        if (Previous == ERoomFlowState::Joining || Previous == ERoomFlowState::Starting ||
            Previous == ERoomFlowState::Creating)
        {
            bReturnToMode = true;
            DestroyThen(EAfterDestroy::Return);
        }
    }
    return true;
}
#undef LOCTEXT_NAMESPACE
