// 主要调用链：生成布局 → 本地装配与导航检查 → 控制器上报 → 屏障确认 → 放行移动；失败关闭门禁。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonGenerationRuntime.generated.h"

class UDungeonGenerationProfile;
class ULevelStreamingDynamic;
class UStaticMeshComponent;
class ADungeonGenerationController;

UENUM(BlueprintType)
enum class EDungeonGenerationPhase : uint8
{
    Pending,
    Loading,
    WaitingForPlayers,
    Ready,
    Failed
};

USTRUCT(BlueprintType)
struct FDungeonGenerationManifest
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FGuid GenerationId;
    UPROPERTY(BlueprintReadOnly)
    FDungeonLayout Layout;
    UPROPERTY(BlueprintReadOnly)
    FString Digest;
    UPROPERTY(BlueprintReadOnly)
    float RoomSpacing = 2400.f;
    UPROPERTY(BlueprintReadOnly)
    int32 ExpectedPlayers = 1;
};

/** Authority-owned assembly host. Place exactly once at the persistent entrance origin. */
UCLASS()
class TEST_25D_API ADungeonGenerationRuntime : public AActor
{
    GENERATED_BODY()
public:
    // 建立复制宿主及低频装配检查周期。
    ADungeonGenerationRuntime();
    // 轮询装配及导航验证，等待全员确认后开门，超时则转入失败。
    virtual void Tick(float DeltaSeconds) override;
    // 登记需要同步给客户端的运行状态字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // 退出时释放本对象持有的资源与状态，并调用父类结束生命周期。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    TObjectPtr<UDungeonGenerationProfile> Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    int32 Seed = 42;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon", meta = (ClampMin = "1", ClampMax = "2"))
    int32 ExpectedPlayers = 1;
    UPROPERTY(ReplicatedUsing = OnRep_Manifest, BlueprintReadOnly, Category = "Dungeon")
    FDungeonGenerationManifest Manifest;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dungeon")
    EDungeonGenerationPhase Phase = EDungeonGenerationPhase::Pending;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dungeon")
    FString FailureReason;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dungeon")
    int32 ReadyPlayers = 0;
    // 序列化布局、连接、加载进度和门禁状态用于诊断。
    UFUNCTION(BlueprintPure, Category = "Dungeon")
    FString GetGenerationReport() const;
    // 返回本机流式加载与导航检查的完成状态。
    UFUNCTION(BlueprintPure, Category = "Dungeon")
    bool IsLocalReady() const
    {
        return bLocalReady;
    }
    // 本机验证完成且服务器全员就绪、无本地错误时允许移动。
    bool CanLocalPlayerMove() const
    {
        return bLocalReady && Phase == EDungeonGenerationPhase::Ready && LocalFailure.IsEmpty();
    }
    // 返回本机装配错误供控制器上报。
    const FString& GetLocalFailure() const
    {
        return LocalFailure;
    }
    // 校验当前控制器后提交就绪屏障，成功时同步就绪人数。
    void ReportReady(ADungeonGenerationController* Controller, FGuid Generation, const FString& Digest);
    // 仅接收当前玩家对本次生成的失败报告。
    void ReportFailure(ADungeonGenerationController* Controller, FGuid Generation, const FString& Reason);

protected:
    // 验证原点和人数，由服务器生成布局摘要并重置就绪屏障后开始装配。
    virtual void BeginPlay() override;

private:
    // 清单复制到客户端后尝试启动本地装配。
    UFUNCTION()
    void OnRep_Manifest();
    // 验证布局与摘要，创建门禁并请求房间流式加载；生成编号阻止重复装配。
    void StartLocalAssembly();
    // 封闭未连接出口，并在入口连接处建立等待全员就绪的安全门。
    void CreateDoorBarriers();
    // 检查动态导航、房间地面、连接路径及实际门洞胶囊通行条件。
    bool CheckNavigation(FString& Reason) const;
    // 记录本地失败并关闭安全门，服务器同时复制失败阶段。
    void Fail(const FString& Reason);
    // 按清单格坐标和房间间距计算世界中心。
    FVector RoomCenter(int32 Index) const;
    // 确认上报者仍属于当前服务器世界的玩家列表。
    bool IsCurrentController(const ADungeonGenerationController* Controller) const;
    UPROPERTY(Transient)
    TArray<TObjectPtr<ULevelStreamingDynamic>> StreamingRooms;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> DoorBarriers;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> SafetyGate;
    FGuid AssembledGeneration;
    FDungeonReadyBarrier Barrier;
    double StartedAt = 0;
    double GenerationMilliseconds = 0;
    bool bLocalReady = false;
    FString LocalFailure;
    FString PendingReason;
};
