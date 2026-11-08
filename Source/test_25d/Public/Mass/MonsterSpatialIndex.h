#pragma once

#include "CoreMinimal.h"

/** Broad phase in XY; exact sphere and room filtering use the last published sample. */
class FMonsterSpatialIndex
{
public:
    struct FEntry
    {
        FVector Location;
        FName Room;
        FIntPoint Cell;
    };
    void Reset(double InCellSize = 1000.0);
    void Update(uint64 Id, const FVector& Location, FName Room);
    void Remove(uint64 Id);
    TArray<uint64> Query(const FVector& Center, double Radius, FName Room = NAME_None) const;
    int32 Num() const
    {
        return Entries.Num();
    }
    int32 NumCells() const
    {
        return Cells.Num();
    }
    FIntPoint CellFor(const FVector& Location) const;

private:
    double CellSize = 1000.0;
    TMap<uint64, FEntry> Entries;
    TMap<FIntPoint, TSet<uint64>> Cells;
};

struct FMonsterActivityPolicy
{
    static bool ShouldStayActive(bool bWasActive, double NearestPlayerDistanceSquared, bool bHasPlayers,
                                 bool bCombatProtected, double WakeRadius, double SleepRadius);
};
