#pragma once

#include "CoreMinimal.h"
#include "Game/MyGameModeBase.h"
#include "PureMassGameMode.generated.h"

class UPureMassMonsterType;

/** Opt-in mode. Existing maps and the hybrid Mass mode are deliberately independent. */
UCLASS()
class TEST_25D_API APureMassGameMode : public AMyGameModeBase
{
    GENERATED_BODY()
public:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

protected:
    virtual void BeginPlay() override;
};

/** One authoring actor for a group; spawned monsters themselves are Mass entities. */
UCLASS()
class TEST_25D_API APureMassSpawnRegion : public AActor
{
    GENERATED_BODY()
public:
    APureMassSpawnRegion();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pure Mass")
    TObjectPtr<UPureMassMonsterType> MonsterType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pure Mass", meta = (ClampMin = "1", ClampMax = "1000"))
    int32 Count = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pure Mass", meta = (ClampMin = "1"))
    float Spacing = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pure Mass")
    FName RoomId;

protected:
    virtual void BeginPlay() override;
};
