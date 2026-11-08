#include "Online/RoomLobby.h"
#include "Online/RoomSubsystem.h"
#include "UI/Widget/RoomMenuWidget.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"
#include "Net/UnrealNetwork.h"

bool ARoomLobbyGameState::CanManage(const APlayerController* Requester) const
{
    return !bStarting && Requester && RoomOwner && Requester->PlayerState == RoomOwner;
}
void ARoomLobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARoomLobbyGameState, RoomPassword);
    DOREPLIFETIME(ARoomLobbyGameState, Capacity);
    DOREPLIFETIME(ARoomLobbyGameState, RoomOwner);
    DOREPLIFETIME(ARoomLobbyGameState, bStarting);
}
ARoomLobbyGameMode::ARoomLobbyGameMode()
{
    GameStateClass = ARoomLobbyGameState::StaticClass();
    PlayerControllerClass = ARoomLobbyController::StaticClass();
    DefaultPawnClass = nullptr;
    bUseSeamlessTravel = true;
}
void ARoomLobbyGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                                  FString& Error)
{
    Super::PreLogin(Options, Address, UniqueId, Error);
    if (Error.IsEmpty())
        if (auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>())
            Rooms->AuthorizeIncoming(Options, GetNumPlayers(), Error);
}
void ARoomLobbyGameMode::PostLogin(APlayerController* PC)
{
    Super::PostLogin(PC);
    auto* GS = GetGameState<ARoomLobbyGameState>();
    if (GS && PC->IsLocalController())
        GS->RoomOwner = PC->PlayerState;
    if (auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>())
        Rooms->NotifyLobbyReady();
}
void ARoomLobbyGameMode::HandleStartingNewPlayer_Implementation(APlayerController*)
{ /* Lobby has UI only, no combat pawn. */
}
void ARoomLobbyController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController())
        return;
    auto* Rooms = GetGameInstance()->GetSubsystem<URoomSubsystem>();
    ServerSetDisplayName(Rooms->SelectedPlayerName);
    Rooms->NotifyLobbyReady();
    RoomWidget = CreateWidget<URoomMenuWidget>(this, URoomMenuWidget::StaticClass());
    RoomWidget->AddToViewport(50);
    RoomWidget->ShowLobby();
    bShowMouseCursor = true;
    SetInputMode(FInputModeUIOnly().SetWidgetToFocus(RoomWidget->TakeWidget()));
}
void ARoomLobbyController::ServerSetDisplayName_Implementation(const FString& Name)
{
    FString Clean = Name.TrimStartAndEnd().Left(24);
    Clean.ReplaceInline(TEXT("\n"), TEXT(" "));
    Clean.ReplaceInline(TEXT("\r"), TEXT(" "));
    if (PlayerState)
        PlayerState->SetPlayerName(Clean.IsEmpty() ? TEXT("玩家") : Clean);
}
void ARoomLobbyController::ServerChangePassword_Implementation(const FString& Password)
{
    const auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>();
    if (GS && GS->CanManage(this))
        GetGameInstance()->GetSubsystem<URoomSubsystem>()->ChangeRoomPassword(Password);
}
void ARoomLobbyController::ServerStartGame_Implementation()
{
    const auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>();
    if (GS && GS->CanManage(this))
        GetGameInstance()->GetSubsystem<URoomSubsystem>()->StartRoomGame();
}
void ARoomLobbyController::PreClientTravel(const FString& URL, ETravelType Type, bool bSeamless)
{
    if (RoomWidget)
        RoomWidget->RemoveFromParent();
    if (IsLocalController())
    {
        SetInputMode(FInputModeGameOnly());
        bShowMouseCursor = false;
    }
    Super::PreClientTravel(URL, Type, bSeamless);
}
void ARoomLobbyController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (RoomWidget)
        RoomWidget->RemoveFromParent();
    Super::EndPlay(Reason);
}
