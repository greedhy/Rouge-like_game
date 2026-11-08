#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonGenerationProfile.generated.h"

UCLASS(BlueprintType)
class TEST_25D_API UDungeonGenerationProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    float RoomSpacing = 2400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    FDungeonRoomTemplate Entrance;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    TArray<FDungeonRoomTemplate> Templates;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    float ReadyTimeout = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    bool bGenerateFullFloor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    FDungeonFloorSettings FloorSettings;
};
