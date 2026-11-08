/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 作用域：UGameInstanceSubsystem，由引擎随每个GameInstance创建/销毁；同一进程多个PIE实例各有一份，不是跨进程单例，普通地图旅行期间仍存活。
 * 主链路：模式/房间UI -> CreateRoom或JoinRoom -> OnlineSession异步完成委托 -> ServerTravel/ClientTravel -> LoadedMap更新本地房间流程状态。
 * 同步边界：子系统自身不复制。会话发现与连接走OnlineSubsystem；State/LastError及OnChanged是本地流程状态，不能当作服务端复制的大厅业务状态。
 * 生命周期：Initialize绑定引擎网络失败、旅行失败、地图加载及0.25秒Ticker；各会话操作保存委托句柄，完成/失败/超时移除。Deinitialize清除会话委托、发起会话销毁、解绑引擎事件和Ticker，防止旅行或退出后旧回调继续使用对象。
 */
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/Ticker.h"
#include "RoomSubsystem.generated.h"

UENUM(BlueprintType)
enum class ERoomFlowState : uint8
{
    Idle,
    Creating,
    Searching,
    Joining,
    InLobby,
    Updating,
    Starting,
    Leaving
};
DECLARE_MULTICAST_DELEGATE(FRoomStateChanged);
class UNetDriver;

/** One LAN session per game instance. Owns delegates across map travel. */
UCLASS(Config = Game)
class TEST_25D_API URoomSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable)
    void StartSinglePlayer();
    UFUNCTION(BlueprintCallable)
    void StartSwordShieldTrial();
    UFUNCTION(BlueprintCallable)
    void StartStanderTrial();
    UFUNCTION(BlueprintCallable)
    void CreateRoom();
    UFUNCTION(BlueprintCallable)
    void JoinRoom(const FString& Password);
    UFUNCTION(BlueprintCallable)
    void LeaveRoom();
    UFUNCTION(BlueprintCallable)
    void ChangeRoomPassword(const FString& Password);
    UFUNCTION(BlueprintCallable)
    void StartRoomGame();
    UFUNCTION(BlueprintPure)
    bool IsBusy() const;
    UFUNCTION(BlueprintPure)
    bool HasSelection() const;
    UFUNCTION(BlueprintPure)
    FString GetDefaultMapPackage() const;
    UFUNCTION(BlueprintPure)
    bool IsHosting() const
    {
        return bHosting;
    }
    UFUNCTION(BlueprintCallable)
    void ClearError();
    void ConfigureSelection(const FString& PlayerName, const TSoftObjectPtr<UWorld>& Map);
    bool AuthorizeIncoming(const FString& Options, int32 PlayerCount, FString& Error) const;
    void NotifyLobbyReady();
    FString GetHostPassword() const
    {
        return HostPassword;
    }
    FRoomStateChanged OnChanged;

    UPROPERTY(BlueprintReadOnly)
    ERoomFlowState State = ERoomFlowState::Idle;
    UPROPERTY(BlueprintReadOnly)
    FText LastError;
    UPROPERTY(BlueprintReadOnly)
    FString SelectedPlayerName;
    UPROPERTY(BlueprintReadOnly)
    bool bReturnToMode = false;
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2", ClampMax = "8"))
    int32 MaxPlayers = 2;
    static bool IsValidPassword(const FString& Password);
    static FString PasswordFingerprint(const FString& Password);
    static FString GeneratePassword();
    static TArray<int32> MatchingRoomIndices(const TArray<FOnlineSessionSearchResult>& Results,
                                             const FString& Password);
    static const FName ProtocolKey;
    static const FName PasswordKey;
    static const FName OpenKey;

private:
    IOnlineSessionPtr Sessions;
    TSharedPtr<FOnlineSessionSearch> Search;
    UPROPERTY()
    TSoftObjectPtr<UWorld> DefaultMap;
    FString HostPassword, PendingPassword;
    bool bHosting = false, bAcceptingPlayers = false, bReturnScheduled = false;
    bool bSwordShieldTrial = false;
    bool bStanderTrial = false;
    double Deadline = 0, ReturnAt = 0;
    enum class EAfterDestroy : uint8
    {
        None,
        Create,
        Search,
        Single,
        Return
    };
    EAfterDestroy AfterDestroy = EAfterDestroy::None;
    FDelegateHandle CreateHandle, FindHandle, JoinHandle, UpdateHandle, DestroyHandle, NetworkHandle, TravelHandle,
        MapHandle;
    FTSTicker::FDelegateHandle TickerHandle;
    bool EnsureSessions();
    void ClearSessionDelegates();
    void SetState(ERoomFlowState NewState, double Timeout = 0);
    void Fail(const FText& Message, bool bKeepLobby = false);
    void DestroyThen(EAfterDestroy Next);
    void ContinueAfterDestroy();
    void BeginCreate();
    void BeginSearch();
    void Created(FName Name, bool bSuccess);
    void Found(bool bSuccess);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Updated(FName Name, bool bSuccess);
    void Destroyed(FName Name, bool bSuccess);
    void LoadedMap(UWorld* World);
    void NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Error);
    void TravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error);
    bool Tick(float Delta);
    void OpenLoadMenu();
    void OpenSinglePlayer();
    void PublishLobby();
};
