#pragma once
#include "CoreMinimal.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonRunTypes.generated.h"

UENUM(BlueprintType)
enum class EDungeonRoomPhase : uint8
{
    Hidden,
    Discovered,
    Gathering,
    Active,
    Cleared,
    Failed
};
UENUM(BlueprintType)
enum class EDungeonRunPhase : uint8
{
    Loading,
    Exploring,
    Encounter,
    Victory,
    Finished,
    Failed
};
UENUM(BlueprintType)
enum class EDungeonInteraction : uint8
{
    ClaimTreasure,
    Purchase,
    Rest,
    Exit
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonRoomProgress
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FName RoomId;
    UPROPERTY(BlueprintReadOnly)
    EDungeonRoomRole Role = EDungeonRoomRole::Entrance;
    UPROPERTY(BlueprintReadOnly)
    EDungeonRoomPhase Phase = EDungeonRoomPhase::Hidden;
    UPROPERTY(BlueprintReadOnly)
    bool bVisited = false;
    UPROPERTY(BlueprintReadOnly)
    int32 Wave = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 PendingWaves = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 RemainingRequired = 0;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonPlayerRunView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FGuid RunId;
    UPROPERTY(BlueprintReadOnly)
    int32 Coins = 0;
    UPROPERTY(BlueprintReadOnly)
    FName CurrentRoom;
    UPROPERTY(BlueprintReadOnly)
    bool bCanInteract = false;
    UPROPERTY(BlueprintReadOnly)
    int32 ChestRemaining = -1;
    UPROPERTY(BlueprintReadOnly)
    bool bRested = false;
    UPROPERTY(BlueprintReadOnly)
    TArray<bool> Purchased;
    UPROPERTY(BlueprintReadOnly)
    FString Message;
};
