#include "Mass/MonsterSpatialIndex.h"

void FMonsterSpatialIndex::Reset(double InCellSize)
{
    CellSize = FMath::IsFinite(InCellSize) ? FMath::Max(100.0, InCellSize) : 1000.0;
    Entries.Reset();
    Cells.Reset();
}

FIntPoint FMonsterSpatialIndex::CellFor(const FVector& Location) const
{
    auto Coordinate = [this](double Value)
    {
        return FMath::FloorToInt(FMath::Clamp(Value / CellSize, -1.e9, 1.e9));
    };
    return FIntPoint(Coordinate(Location.X), Coordinate(Location.Y));
}

void FMonsterSpatialIndex::Update(uint64 Id, const FVector& Location, FName Room)
{
    if (Location.ContainsNaN())
    {
        Remove(Id);
        return;
    }
    const FIntPoint Cell = CellFor(Location);
    if (const FEntry* Previous = Entries.Find(Id); Previous && Previous->Cell != Cell)
        Remove(Id);
    Entries.Add(Id, {Location, Room, Cell});
    Cells.FindOrAdd(Cell).Add(Id);
}

void FMonsterSpatialIndex::Remove(uint64 Id)
{
    if (const FEntry* Entry = Entries.Find(Id))
    {
        const FIntPoint Cell = Entry->Cell;
        if (TSet<uint64>* Bucket = Cells.Find(Cell))
        {
            Bucket->Remove(Id);
            if (Bucket->IsEmpty())
                Cells.Remove(Cell);
        }
        Entries.Remove(Id);
    }
}

TArray<uint64> FMonsterSpatialIndex::Query(const FVector& Center, double Radius, FName Room) const
{
    TArray<uint64> Result;
    if (Center.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0)
        return Result;
    auto Accept = [&](uint64 Id, const FEntry& Entry)
    {
        if ((Room.IsNone() || Entry.Room == Room) && FVector::DistSquared(Center, Entry.Location) <= Radius * Radius)
            Result.Add(Id);
    };
    const FIntPoint Min = CellFor(Center - FVector(Radius, Radius, 0));
    const FIntPoint Max = CellFor(Center + FVector(Radius, Radius, 0));
    const double CellCount = (double(Max.X) - Min.X + 1) * (double(Max.Y) - Min.Y + 1);
    if (CellCount > 4096)
    {
        for (const auto& Pair : Entries)
            Accept(Pair.Key, Pair.Value);
    }
    else
    {
        for (int32 X = Min.X; X <= Max.X; ++X)
            for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
                if (const auto* Bucket = Cells.Find(FIntPoint(X, Y)))
                    for (uint64 Id : *Bucket)
                        Accept(Id, Entries.FindChecked(Id));
    }
    Result.Sort();
    return Result;
}

bool FMonsterActivityPolicy::ShouldStayActive(bool bWasActive, double DistanceSquared, bool bHasPlayers,
                                              bool bCombatProtected, double WakeRadius, double SleepRadius)
{
    if (!bHasPlayers || bCombatProtected)
        return true;
    const double Radius = bWasActive ? FMath::Max(WakeRadius, SleepRadius) : WakeRadius;
    return DistanceSquared <= FMath::Square(FMath::Max(0.0, Radius));
}
