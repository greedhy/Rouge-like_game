#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Mass/MonsterSpatialIndex.h"
#include "MonsterManagementSubsystem.generated.h"

class UMonsterManagementConfig;
class UMassEntitySubsystem;
class UBrainComponent;
class AEnemyCharacter;
struct FMonsterFragment;

USTRUCT(BlueprintType)
struct FMonsterManagementStats
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Registered = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Active = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Sleeping = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 PausedBrains = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 OccupiedCells = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 UpdateCount = 0;
    UPROPERTY(BlueprintReadOnly)
    bool bEnabled = false;
};

UCLASS()
class TEST_25D_API UMonsterManagementSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;

    void Enable(UMonsterManagementConfig* InConfig);
    void RegisterEnemy(AEnemyCharacter* Enemy);
    void UnregisterEnemy(AEnemyCharacter* Enemy);
    void NotifyDamaged(AEnemyCharacter* Enemy);

    UFUNCTION(BlueprintCallable, Category = "Mass Monsters")
    TArray<AEnemyCharacter*> FindMonstersInRadius(FVector Center, float Radius, FName RoomId = NAME_None) const;
    UFUNCTION(BlueprintPure, Category = "Mass Monsters")
    FMonsterManagementStats GetStats() const;
    UFUNCTION(BlueprintPure, Category = "Mass Monsters")
    bool IsMonsterActive(AEnemyCharacter* Enemy) const;

    // Called exclusively by the Mass processor on the game thread.
    bool BeginUpdate(float DeltaSeconds);
    void UpdateEnemy(FMonsterFragment& Fragment);
    void FinishUpdate();
    FMassEntityHandle GetEntityForEnemy(const AEnemyCharacter* Enemy) const;

private:
    struct FRecord
    {
        TWeakObjectPtr<AEnemyCharacter> Enemy;
        FMassEntityHandle Entity;
        TWeakObjectPtr<UBrainComponent> PausedBrain;
        double ProtectedUntil = 0;
        bool bActive = true;
    };
    void SetActive(FRecord& Record, bool bActive);
    void RemoveRecord(uint64 Id);
    UPROPERTY(Transient)
    TObjectPtr<UMonsterManagementConfig> Config;
    UPROPERTY(Transient)
    TObjectPtr<UMassEntitySubsystem> Mass;
    FMassArchetypeHandle Archetype;
    TMap<TWeakObjectPtr<AEnemyCharacter>, uint64> ActorIds;
    TMap<uint64, FRecord> Records;
    TArray<TWeakObjectPtr<AEnemyCharacter>> PendingRegistrations;
    TArray<FVector> PlayerLocations;
    FMonsterSpatialIndex Grid;
    uint64 NextId = 1;
    float AccumulatedTime = 0;
    int32 UpdateCount = 0;
    bool bEnabled = false;
};
