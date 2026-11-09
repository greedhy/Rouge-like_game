#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "RoomLobby.generated.h"
class URoomMenuWidget;

UCLASS()
class TEST_25D_API ARoomLobbyGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated, BlueprintReadOnly)
    FString RoomPassword;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Capacity = 2;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TObjectPtr<APlayerState> RoomOwner;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bStarting = false;
    bool CanManage(const APlayerController* Requester) const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class TEST_25D_API ARoomLobbyController : public APlayerController
{
    GENERATED_BODY()
public:
    UFUNCTION(Server, Reliable)
    void ServerSetDisplayName(const FString& Name);
    UFUNCTION(Server, Reliable)
    void ServerChangePassword(const FString& Password);
    UFUNCTION(Server, Reliable)
    void ServerStartGame();
    virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UPROPERTY()
    TObjectPtr<URoomMenuWidget> RoomWidget;
};

UCLASS()
class TEST_25D_API ARoomLobbyGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARoomLobbyGameMode();
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                          FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* Controller, const FUniqueNetIdRepl& UniqueId,
                                  const FString& Options, const FString& Portal) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
