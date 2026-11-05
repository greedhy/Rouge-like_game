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
    ADungeonRunDirector();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UDungeonRunProfile> Profile;
    UPROPERTY(Replicated, BlueprintReadOnly) FGuid RunId;
    UPROPERTY(Replicated, BlueprintReadOnly) EDungeonRunPhase Phase = EDungeonRunPhase::Loading;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<FDungeonRoomProgress> Rooms;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<bool> OpenDoors;
    UPROPERTY(Replicated, BlueprintReadOnly) FString FailureReason;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 ActiveRoom = INDEX_NONE;
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AEnemyCharacter> Boss;
    UPROPERTY(Replicated, BlueprintReadOnly) FVector PulseCenter = FVector::ZeroVector;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bPulseWarning = false;
    UFUNCTION(BlueprintPure) FString GetRunReport() const;
    UFUNCTION(BlueprintCallable) bool ExportPerformanceReport() const;
    UFUNCTION(BlueprintPure) ADungeonGenerationRuntime* GetGeneration() const;
    UFUNCTION(BlueprintPure) int32 GetRoomIndex(const FVector& Location) const;
    UFUNCTION(BlueprintPure) FVector GetRoomCenter(int32 Index) const;
    bool CanPlayerAct(const ADungeonRunController* Controller) const;
    void HandleInteraction(ADungeonRunController* Controller, FGuid RequestRun, FName Room,
        EDungeonInteraction Action, int32 OfferIndex);
    void NotifyPlayerDown(ADungeonPlayerCharacter* Player);
    static ADungeonRunDirector* Find(const UWorld* World);
    static bool AllowsCombat(const AActor* Source, const AActor* Target);
    static bool IsManagedEnemy(const AActor* Actor);
    // Source-owned summons count toward the same encounter cap but never consume ordinary pool slots.
    AEnemyCharacter* SpawnSummonedEnemy(AEnemyCharacter* Summoner,TSubclassOf<AEnemyCharacter> Class,const FTransform& Transform);
    void RetireSummonedEnemy(AEnemyCharacter* Summoner,AEnemyCharacter* Minion);
private:
    bool InitializeRun();
    void TickAuthority();
    void BuildLocalPresentation();
    void ApplyDoorPresentation();
    void SynchronizeState();
    void UpdatePlayerViews();
    void StartEncounter(int32 Room);
    bool SpawnWave();
    bool SpawnMassGroup(int32 Count, bool bRequired);
    bool SpawnActorGroup(int32 Count, bool bRequired);
    bool SpawnActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired = true, bool bIsBoss = false, FName ReservedId = NAME_None, bool bPoolEligible = false);
    bool QueueActorTarget(TSubclassOf<AEnemyCharacter> Class, float Health, bool bRequired, bool bIsBoss = false, bool bPoolEligible = false);
    void PumpSpawnQueue();
    void CompleteEncounter();
    void FailRun(const FString& Reason);
    void CleanupEncounter();
    void EnforceBoundaries();
    void UpdateBoss();
    void OnMassRemoved(int32 Id, FName Room, EPureMassRemovalReason Reason);
    UFUNCTION() void OnActorDied(AActor* Actor);
    UFUNCTION() void OnActorDestroyed(AActor* Actor);
    void ReturnPartyToMenu();
    bool FindSpawnPosition(float Radius, float HalfHeight, FVector& OutPosition, int32 RoomOverride = INDEX_NONE);
    void TrackEffect(AActor* Actor);
    UPROPERTY(Transient) TObjectPtr<ADungeonGenerationRuntime> Generation;
    UPROPERTY(Transient) TObjectPtr<UDungeonRunEconomy> Economy;
    UPROPERTY(Transient) TObjectPtr<UPureMassSubsystem> Mass;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> GameplayDoors;
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Markers;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> PulseMesh;
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
    void ReturnEnemyToPool(AEnemyCharacter* Enemy);
    void DrainPoolReturns();
    void SamplePerformance(float DeltaSeconds);
    TSharedPtr<class FJsonObject> MakePerformanceReport() const;
    struct FTiming
    {
        int32 Count = 0;
        double TotalMs = 0, MaxMs = 0;
        void Add(double Ms) { ++Count; TotalMs += Ms; MaxMs = FMath::Max(MaxMs, Ms); }
    };
    FTiming CreateTiming, ReuseTiming, ReturnTiming, ResetTiming;
    int32 PoolHits = 0, PoolMisses = 0, PoolOverflow = 0, PoolPeak = 0;
    int32 PerformanceSampleCount = 0;
    TArray<TSharedPtr<class FJsonValue>> PerformanceSamples;
    double NextPerformanceSample = 0;
    double MaxFrameMs = 0, FrameTotalMs = 0;
    int32 FrameCount = 0;
    uint64 PeakObservedProcessBytes = 0;
    bool PrepareEnemyAssets();
    FDungeonRunModel Model;
    TMap<int32,FName> MassTargets;
    TMap<TWeakObjectPtr<AActor>,FName> ActorTargets;
    TSet<int32> OptionalMass;
    TSet<TWeakObjectPtr<AActor>> OptionalActors;
    TMap<TWeakObjectPtr<AActor>,FVector> LastSafeLocations;
    TMap<TWeakObjectPtr<AActor>,int32> EffectRooms;
    TMap<int32,FPrimaryAssetId> ChestItems;
    TMap<int32,int32> ChestQuantities;
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
