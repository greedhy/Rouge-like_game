// 主要调用链：等待地图就绪 → 初始化 → 开始遭遇 → 分帧生成 → 死亡结算 → 清场与退出。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Roguelike/DungeonRunModel.h"
#include "Roguelike/DungeonEnemyPool.h"
#include "PureMass/PureMassEncounterTypes.h"
#include "DungeonRunDirector.generated.h"

class ADungeonGenerationRuntime;
class ADungeonRunController;
class ADungeonPlayerCharacter;
class AEnemyCharacter;
class UStaticMeshComponent;
class UDungeonRunEconomy;
class UDungeonRunProfile;
class UPureMassSubsystem;
struct FStreamableHandle;

/** Opt-in gameplay host, separate from geometry/readiness validation. */
UCLASS()
class TEST_25D_API ADungeonRunDirector : public AActor
{
    GENERATED_BODY()
public:
    // 建立复制宿主和逐帧调度，等待地图就绪后初始化本局。
    ADungeonRunDirector();
    // 推进服务器逻辑并采样性能，本地生成就绪后才创建和更新遭遇表现。
    virtual void Tick(float DeltaSeconds) override;
    // 退出时释放本对象持有的资源与状态，并调用父类结束生命周期。
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    // 登记需要同步给客户端的运行状态字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UDungeonRunProfile> Profile;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FGuid RunId;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EDungeonRunPhase Phase = EDungeonRunPhase::Loading;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FDungeonRoomProgress> Rooms;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<bool> OpenDoors;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FString FailureReason;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 ActiveRoom = INDEX_NONE;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TObjectPtr<AEnemyCharacter> Boss;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FVector PulseCenter = FVector::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bPulseWarning = false;
    // 汇总房间、玩家、敌人目标与性能诊断信息。
    UFUNCTION(BlueprintPure)
    FString GetRunReport() const;
    // 以运行、世界、网络角色和进程标识命名并保存性能报告。
    UFUNCTION(BlueprintCallable)
    bool ExportPerformanceReport() const;
    // 优先返回缓存的生成宿主，否则遍历当前世界查找。
    UFUNCTION(BlueprintPure)
    ADungeonGenerationRuntime* GetGeneration() const;
    // 校验坐标范围后量化网格并查找对应房间。
    UFUNCTION(BlueprintPure)
    int32 GetRoomIndex(const FVector& Location) const;
    // 将有效房间格坐标换算为世界中心。
    UFUNCTION(BlueprintPure)
    FVector GetRoomCenter(int32 Index) const;
    // 结合玩家身份、存活状态和地图就绪阶段判断操作资格。
    bool CanPlayerAct(const ADungeonRunController* Controller) const;
    // 服务器校验身份、局号和距离后分派宝箱、购买、休息及出口事务。
    void HandleInteraction(ADungeonRunController* Controller, FGuid RequestRun, FName Room, EDungeonInteraction Action,
                           int32 OfferIndex);
    // 死亡回调仅催促视图刷新，实际遭遇推进留在逐帧循环。
    void NotifyPlayerDown(ADungeonPlayerCharacter* Player);
    // 在指定世界查找局内导演供战斗和交互入口使用。
    static ADungeonRunDirector* Find(const UWorld* World);
    // 检查存活、世界、阶段和房间，仅放行当前遭遇的玩家与敌人交战。
    static bool AllowsCombat(const AActor* Source, const AActor* Target);
    // 依据遭遇标志识别受本局管理的敌人。
    static bool IsManagedEnemy(const AActor* Actor);
    // Source-owned summons count toward the same encounter cap but never consume ordinary pool slots.
    // 校验来源和房间，将召唤物登记为占用遭遇容量的非必杀目标。
    AEnemyCharacter* SpawnSummonedEnemy(AEnemyCharacter* Summoner, TSubclassOf<AEnemyCharacter> Class,
                                        const FTransform& Transform);
    // 验证召唤归属，移除遭遇计数与安全位置后销毁召唤物。
    void RetireSummonedEnemy(AEnemyCharacter* Summoner, AEnemyCharacter* Minion);

private:
    // 回归夹具直接设置遭遇账本，测试真实的延迟生成/碰撞失败与死亡结算入口。
    friend class FDungeonSummonSpawnLifecycleTest;
    // 校验配置，初始化模型与经济服务，绑定实体移除及效果生成监听后进入探索。
    bool InitializeRun();
    // 等待资源和地图就绪，检查团灭与边界，推进波次并按预算消费生成队列。
    void TickAuthority();
    // 本地生成就绪后创建遭遇门、服务标记及首领范围提示。
    void BuildLocalPresentation();
    // 依据复制快照更新门碰撞、房间标记与首领预警表现。
    void ApplyDoorPresentation();
    // 将模型的房间进度、剩余目标与门状态写入复制快照。
    void SynchronizeState();
    // 按各玩家经济记录与房间状态刷新专属交互视图。
    void UpdatePlayerViews();
    // 初始化房间遭遇和随机数量，设置边界、关闭通道并提交首波。
    void StartEncounter(int32 Room);
    // 按房间类型提交本波角色和实体目标，并向遭遇模型反馈提交结果。
    bool SpawnWave();
    // 预登记实体目标再放入待生成队列，使等待生成的目标也参与清场判断。
    bool SpawnMassGroup(int32 Count, bool bRequired);
    // 按确定性普通怪组合逐个提交角色生成请求。
    bool SpawnActorGroup(int32 Count, bool bRequired);
    // 查找出生点后借用跨房间闲置对象或延迟生成新对象，绑定结算并设置生命属性。
    bool SpawnActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired = true, bool bIsBoss = false,
                          FName ReservedId = NAME_None, bool bPoolEligible = false);
    // 检查容量并预留目标编号，保存本局与房间身份以防旧请求生效。
    bool QueueActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired, bool bIsBoss = false,
                          bool bPoolEligible = false);
    // 按数量及耗时预算生成目标，拒绝旧局请求并跳过首领死亡后的可选目标。
    void PumpSpawnQueue();
    // 结算金币、复活倒地玩家并清理遭遇，转入探索或胜利。
    void CompleteEncounter();
    // 服务器置为失败，解除边界并统一清理和同步错误状态。
    void FailRun(const FString& Reason);
    // 清空待生成请求并抑制清理回调结算，回收敌人及销毁旧遭遇效果。
    void CleanupEncounter();
    // 回退越界角色到安全位置，并销毁离开出生房间的技能实体。
    void EnforceBoundaries();
    // 处理首领死亡后的可选目标回收，再推进旧版召唤与预警后伤害。
    void UpdateBoss();
    // 移除实体映射并把击杀、回收或异常原因交给遭遇模型。
    void OnMassRemoved(int32 Id, FName Room, EPureMassRemovalReason Reason);
    // 先移除目标确保单次击杀结算，再将池化尸体加入延迟归还队列。
    UFUNCTION()
    void OnActorDied(AActor* Actor);
    // 将仍在目标表中的销毁判为异常；已死亡结算的对象不会重复计数。
    UFUNCTION()
    void OnActorDestroyed(AActor* Actor);
    // 标记本局结束并通知远端返回，短暂延迟后退出会话。
    void ReturnPartyToMenu();
    // 结合导航投影、房间边界、玩家间距和胶囊碰撞寻找出生位置。
    bool FindSpawnPosition(float Radius, float HalfHeight, FVector& OutPosition, int32 RoomOverride = INDEX_NONE);
    // 记录技能实体出生房间，供越界检测与清场销毁。
    void TrackEffect(AActor* Actor);
    UPROPERTY(Transient)
    TObjectPtr<ADungeonGenerationRuntime> Generation;
    UPROPERTY(Transient)
    TObjectPtr<UDungeonRunEconomy> Economy;
    UPROPERTY(Transient)
    TObjectPtr<UPureMassSubsystem> Mass;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> GameplayDoors;
    UPROPERTY(Transient)
    TArray<TObjectPtr<USceneComponent>> Markers;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> PulseMesh;
    struct FPendingEnemy
    {
        TSubclassOf<AEnemyCharacter> Class;
        float Health;
        bool Required;
        bool IsBoss;
        FName Id;
        FGuid Run;
        int32 Room;
        bool PoolEligible;
        bool IsMass = false;
    };
    TArray<FPendingEnemy> SpawnQueue;
    TSharedPtr<FStreamableHandle> EnemyLoadHandle;
    bool bEnemyLoadRequested = false;
    double EnemyLoadStarted = 0;
    double LastSpawnMs = 0;
    int32 LastSpawnCount = 0;
    FDungeonEnemyPool EnemyPool;
    TArray<TWeakObjectPtr<AEnemyCharacter>> PendingPoolReturns;
    // 非池对象直接销毁；池化对象登记闲置后休眠并累计归还耗时。
    void ReturnEnemyToPool(AEnemyCharacter* Enemy);
    // 先转移队列避免重入，再准备休眠并等待尸体阶段完成后正式归还。
    void DrainPoolReturns();
    // 按间隔采样进程内存、帧耗时和池占用，保留有限长度快照。
    void SamplePerformance(float DeltaSeconds);
    // 汇总池分类占用、命中统计和创建复用归还耗时。
    TSharedPtr<class FJsonObject> MakePerformanceReport() const;
    struct FTiming
    {
        int32 Count = 0;
        double TotalMs = 0, MaxMs = 0;
        // 累计操作次数、总耗时和最大耗时。
        void Add(double Ms)
        {
            ++Count;
            TotalMs += Ms;
            MaxMs = FMath::Max(MaxMs, Ms);
        }
    };
    FTiming CreateTiming, ReuseTiming, ReturnTiming, ResetTiming;
    int32 PoolHits = 0, PoolMisses = 0, PoolOverflow = 0, PoolPeak = 0;
    int32 PerformanceSampleCount = 0;
    TArray<TSharedPtr<class FJsonValue>> PerformanceSamples;
    double NextPerformanceSample = 0;
    double MaxFrameMs = 0, FrameTotalMs = 0;
    int32 FrameCount = 0;
    uint64 PeakObservedProcessBytes = 0;
    // 验证怪物名单并异步加载类资源，轮询完成状态及超时。
    bool PrepareEnemyAssets();
    FDungeonRunModel Model;
    TMap<int32, FName> MassTargets;
    TMap<TWeakObjectPtr<AActor>, FName> ActorTargets;
    TSet<int32> OptionalMass;
    TSet<TWeakObjectPtr<AActor>> OptionalActors;
    TMap<TWeakObjectPtr<AActor>, FVector> LastSafeLocations;
    TMap<TWeakObjectPtr<AActor>, int32> EffectRooms;
    TMap<int32, FPrimaryAssetId> ChestItems;
    TMap<int32, int32> ChestQuantities;
    FDelegateHandle MassRemovedHandle;
    FDelegateHandle EffectSpawnHandle;
    int32 TotalNormalCount = 0;
    int32 NormalGroupSequence = 0;
    int32 SpawnSequence = 0;
    int32 ActorSequence = 0;
    bool bLocalBuilt = false;
    bool bCleaningUp = false;
    bool bBossDead = false;
    bool bBossSummoned = false;
    double NextBossPulse = 0;
    double PulseImpactAt = 0;
    double NextViewsAt = 0;
    FTimerHandle ReturnTimer;
};
