#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonRunTypes.h"

enum class EDungeonTargetResolution : uint8
{
    Killed,
    Recycled,
    Unexpected
};

/** Pure server ledger: a missing entity is never evidence of a kill. */
struct TEST_25D_API FDungeonEncounterLedger
{
    bool Start(int32 Waves);
    bool BeginWave();
    bool RegisterTarget(FName Id, bool bRequired);
    bool EndWave(bool bSpawnSucceeded);
    bool ResolveTarget(FName Id, EDungeonTargetResolution Reason);
    bool IsClear() const;
    bool CanSpawnNextWave() const;
    int32 NumRequired() const;
    int32 NumLiving() const
    {
        return Living.Num();
    }
    int32 PendingWaves = 0;
    int32 Wave = 0;
    bool bSpawning = false;
    FString Error;
    TMap<FName, bool> Living;

private:
    bool bStarted = false;
    int32 WaveRegistrations = 0;
    TSet<FName> Registered;
};

/** World-free graph policy, shared by authority, client door presentation and tests. */
struct TEST_25D_API FDungeonRunModel
{
    bool Initialize(const FDungeonLayout& InLayout);
    static bool IsCombat(EDungeonRoomRole Role);
    int32 Observe(const TArray<int32>& LivingPlayerRooms, const TArray<bool>& InSafeArea);
    bool BeginEncounter(int32 Room, int32 Waves);
    int32 FinishEncounter();
    void Fail(const FString& Reason);
    bool CanCross(int32 A, int32 B) const;
    bool CanMove(const FVector& From, const FVector& To, float Spacing) const;
    int32 FindRoom(const FVector& Location, float Spacing) const;
    TArray<FDungeonRoomProgress> Rooms;
    FDungeonEncounterLedger Encounter;
    int32 GatheringRoom = INDEX_NONE;
    int32 ActiveRoom = INDEX_NONE;
    FString Error;

private:
    FDungeonLayout Layout;
    TArray<int32> Parents;
    int32 ReadyRoom = INDEX_NONE;
};
