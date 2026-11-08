#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "TravelPlayerStart.generated.h"

/** Player Start Tag is the arrival group; move each slot to author together/separate spawns. */
UCLASS()
class TEST_25D_API ATravelPlayerStart : public APlayerStart
{
    GENERATED_BODY()
public:
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = "Travel",
        meta =
            (ClampMin = "0",
             ToolTip =
                 "Stable player slot within Player Start Tag. Each group needs a unique point for every supported player."))
    int32 PlayerSlot = 0;

    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = "Travel",
        meta =
            (ToolTip =
                 "Use this point's Player Start Tag for direct entry when no portal destination was supplied. All marked points must share one tag."))
    bool bDefaultEntryGroup = false;

    static APlayerStart* FindForSlot(const TArray<APlayerStart*>& Starts, FName Group, int32 Slot, FString& OutError);
};
