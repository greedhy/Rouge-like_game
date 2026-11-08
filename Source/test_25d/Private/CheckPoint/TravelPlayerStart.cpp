#include "CheckPoint/TravelPlayerStart.h"
#include "CheckPoint/MapEntrance.h"

APlayerStart* ATravelPlayerStart::FindForSlot(const TArray<APlayerStart*>& Starts, FName Group, int32 Slot,
                                              FString& OutError)
{
    OutError.Reset();
    APlayerStart* Result = nullptr;
    if (Group.IsNone() || Slot < 0)
    {
        OutError = TEXT("Arrival group or player slot is not configured.");
        return nullptr;
    }
    for (APlayerStart* Start : Starts)
    {
        if (!IsValid(Start) || Start->IsA<AMapEntrance>() || Start->PlayerStartTag != Group)
            continue;
        const ATravelPlayerStart* Indexed = Cast<ATravelPlayerStart>(Start);
        if ((Indexed ? Indexed->PlayerSlot : 0) != Slot)
            continue;
        if (Result)
        {
            OutError = FString::Printf(TEXT("Duplicate arrival group '%s', player slot %d."), *Group.ToString(), Slot);
            return nullptr;
        }
        Result = Start;
    }
    if (!Result)
        OutError = FString::Printf(TEXT("Missing arrival group '%s', player slot %d."), *Group.ToString(), Slot);
    return Result;
}
