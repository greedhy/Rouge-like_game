#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "RoomSubsystem.generated.h"

UENUM(BlueprintType)
enum class ERoomFlowState:uint8 { Idle, Creating, Searching, Joining, InLobby, Updating, Starting, Leaving };
DECLARE_MULTICAST_DELEGATE(FRoomStateChanged);
class UNetDriver;

/** One LAN session per game instance. Owns delegates across map travel. */
UCLASS(Config=Game)
class TEST_25D_API URoomSubsystem:public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable) void StartSinglePlayer();
    UFUNCTION(BlueprintCallable) void StartSwordShieldTrial();
    UFUNCTION(BlueprintCallable) void StartStanderTrial();
    UFUNCTION(BlueprintCallable) void CreateRoom();
    UFUNCTION(BlueprintCallable) void JoinRoom(const FString& Password);
    UFUNCTION(BlueprintCallable) void LeaveRoom();
    UFUNCTION(BlueprintCallable) void ChangeRoomPassword(const FString& Password);
    UFUNCTION(BlueprintCallable) void StartRoomGame();
    UFUNCTION(BlueprintPure) bool IsBusy() const;
    UFUNCTION(BlueprintPure) bool HasSelection() const;
    UFUNCTION(BlueprintPure) FString GetDefaultMapPackage() const;
    UFUNCTION(BlueprintPure) bool IsHosting() const { return bHosting; }
    UFUNCTION(BlueprintCallable) void ClearError();
    void ConfigureSelection(const FString& PlayerName,const TSoftObjectPtr<UWorld>& Map);
    bool AuthorizeIncoming(const FString& Options,int32 PlayerCount,FString& Error) const;
    void NotifyLobbyReady();
    FString GetHostPassword() const { return HostPassword; }
    FRoomStateChanged OnChanged;

    UPROPERTY(BlueprintReadOnly) ERoomFlowState State=ERoomFlowState::Idle;
    UPROPERTY(BlueprintReadOnly) FText LastError;
    UPROPERTY(BlueprintReadOnly) FString SelectedPlayerName;
    UPROPERTY(BlueprintReadOnly) bool bReturnToMode=false;
    UPROPERTY(Config,EditAnywhere,BlueprintReadOnly,meta=(ClampMin="2",ClampMax="8")) int32 MaxPlayers=2;
    static bool IsValidPassword(const FString& Password);
    static FString PasswordFingerprint(const FString& Password);
    static FString GeneratePassword();
    static TArray<int32> MatchingRoomIndices(const TArray<FOnlineSessionSearchResult>& Results,const FString& Password);
    static const FName ProtocolKey;
    static const FName PasswordKey;
    static const FName OpenKey;
private:
    IOnlineSessionPtr Sessions;
    TSharedPtr<FOnlineSessionSearch> Search;
    UPROPERTY() TSoftObjectPtr<UWorld> DefaultMap;
    FString HostPassword,PendingPassword;
    bool bHosting=false,bAcceptingPlayers=false,bReturnScheduled=false;
    bool bSwordShieldTrial=false;
    bool bStanderTrial=false;
    double Deadline=0,ReturnAt=0;
    enum class EAfterDestroy:uint8 { None, Create, Search, Single, Return };
    EAfterDestroy AfterDestroy=EAfterDestroy::None;
    FDelegateHandle CreateHandle,FindHandle,JoinHandle,UpdateHandle,DestroyHandle,NetworkHandle,TravelHandle,MapHandle;
    FTSTicker::FDelegateHandle TickerHandle;
    bool EnsureSessions();
    void ClearSessionDelegates();
    void SetState(ERoomFlowState NewState,double Timeout=0);
    void Fail(const FText& Message,bool bKeepLobby=false);
    void DestroyThen(EAfterDestroy Next);
    void ContinueAfterDestroy();
    void BeginCreate();
    void BeginSearch();
    void Created(FName Name,bool bSuccess);
    void Found(bool bSuccess);
    void Joined(FName Name,EOnJoinSessionCompleteResult::Type Result);
    void Updated(FName Name,bool bSuccess);
    void Destroyed(FName Name,bool bSuccess);
    void LoadedMap(UWorld* World);
    void NetworkFailure(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type Failure,const FString& Error);
    void TravelFailure(UWorld* World,ETravelFailure::Type Failure,const FString& Error);
    bool Tick(float Delta);
    void OpenLoadMenu();
    void OpenSinglePlayer();
    void PublishLobby();
};
