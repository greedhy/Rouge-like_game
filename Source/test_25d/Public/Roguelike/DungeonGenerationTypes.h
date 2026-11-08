#pragma once

#include "CoreMinimal.h"
#include "DungeonGenerationTypes.generated.h"

UENUM(BlueprintType)
enum class EDungeonRoomRole : uint8
{
    Entrance,
    Combat,
    Treasure,
    Elite,
    Merchant,
    Rest,
    Boss
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonRoomTemplate
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TemplateId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EDungeonRoomRole Role = EDungeonRoomRole::Combat;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UWorld> Level;
    // Local +X, +Y, -X, -Y. Templates use a shared square footprint in increment one.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DoorMask = 15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AllowedRotations = 15;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonRoomPlacement
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FName RoomId;
    UPROPERTY(BlueprintReadOnly)
    EDungeonRoomRole Role = EDungeonRoomRole::Combat;
    UPROPERTY(BlueprintReadOnly)
    FName TemplateId;
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UWorld> Level;
    UPROPERTY(BlueprintReadOnly)
    FIntPoint Cell = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly)
    int32 QuarterTurns = 0;
    UPROPERTY(BlueprintReadOnly)
    bool bPersistent = false;
    UPROPERTY(BlueprintReadOnly)
    int32 MainPathIndex = -1;
    UPROPERTY(BlueprintReadOnly)
    int32 BranchId = -1;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonRoomConnection
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 From = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 To = 0;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonLayout
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Seed = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Version = 1;
    UPROPERTY(BlueprintReadOnly)
    TArray<FDungeonRoomPlacement> Rooms;
    UPROPERTY(BlueprintReadOnly)
    TArray<FDungeonRoomConnection> Connections;
    UPROPERTY(BlueprintReadOnly)
    int32 MainPathLength = 0;
    UPROPERTY(BlueprintReadOnly)
    bool bUsedFallback = false;
    UPROPERTY(BlueprintReadOnly)
    bool bDiversityRelaxed = false;
    UPROPERTY(BlueprintReadOnly)
    int32 PlacementAttempts = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 SearchSteps = 0;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonFloorSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MainRoomsMin = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MainRoomsMax = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 SideRoomsMin = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 SideRoomsMax = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesMin = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesMax = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 GridRadius = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxPlacementAttempts = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxSearchStepsPerAttempt = 2000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bAllowFallback = true;
};

/** Server-side readiness ledger. Connection identity is supplied by the owning controller. */
struct TEST_25D_API FDungeonReadyBarrier
{
    void Reset(FGuid InGeneration, const FString& InDigest, int32 InExpected);
    bool Acknowledge(int32 PlayerId, FGuid InGeneration, const FString& InDigest);
    bool IsReady() const;
    int32 NumReady() const
    {
        return ReadyPlayers.Num();
    }

private:
    FGuid Generation;
    FString Digest;
    int32 Expected = 0;
    TSet<int32> ReadyPlayers;
};
