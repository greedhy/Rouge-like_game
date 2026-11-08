#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonLayoutLibrary.generated.h"

class UDungeonGenerationProfile;

UCLASS()
class TEST_25D_API UDungeonLayoutLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    static bool BuildAssemblyProbe(const UDungeonGenerationProfile* Profile, int32 Seed, FDungeonLayout& OutLayout,
                                   FString& OutError);
    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    static bool BuildFloor(const UDungeonGenerationProfile* Profile, int32 Seed, FDungeonLayout& OutLayout,
                           FString& OutError);
    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    static bool ValidateFloor(const UDungeonGenerationProfile* Profile, const FDungeonLayout& Layout,
                              FString& OutError);
    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    static bool ValidateLayout(const UDungeonGenerationProfile* Profile, const FDungeonLayout& Layout,
                               FString& OutError);
    UFUNCTION(BlueprintPure, Category = "Dungeon")
    static FString CanonicalLayout(const FDungeonLayout& Layout);
    static FIntPoint Direction(int32 Index);
    static int32 DirectionIndex(FIntPoint Delta);
    static int32 RotateDoorMask(int32 Mask, int32 QuarterTurns);
};
