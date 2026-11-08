#pragma once
#include "CoreMinimal.h"

// A continuous interval for the same complete party, independent of tick frequency.
struct FPartyPortalCountdown
{
    double EndTime = -1;
    TArray<int32> Roster;
    void Reset()
    {
        EndTime = -1;
        Roster.Reset();
    }
    bool Update(TArray<int32> Players, bool bEveryoneInside, double Now, float Duration)
    {
        Players.Sort();
        if (Players.IsEmpty() || !bEveryoneInside)
        {
            Reset();
            return false;
        }
        if (EndTime < 0 || Players != Roster)
        {
            Roster = MoveTemp(Players);
            EndTime = Now + FMath::Max(0.f, Duration);
        }
        return Now >= EndTime;
    }
};
