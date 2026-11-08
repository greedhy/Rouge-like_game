#pragma once
#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
class UBackpackItemDefinitions;
struct FBackpackItemDefEntry;
struct TEST_25D_API FEquipmentGenerator
{
    static bool Generate(const UBackpackItemDefinitions& Catalog, const FBackpackItemDefEntry& Def, int32 ItemLevel,
                         int32 Seed, FEquipmentRollData& Out, FString& Error);
    static bool ValidateRoll(const FEquipmentRollData& Roll, FString* Error = nullptr);
    static bool ValidateForDefinition(const FBackpackItemDefEntry* Def, const FEquipmentRollData& Roll);
    static bool IsSupportedStat(FGameplayTag Tag);
    static TMap<FGameplayTag, float> GetTotals(const FEquipmentRollData& Roll);
    static FText MakeDisplayName(const FText& Base, const FEquipmentRollData& Roll);
    static FText MakeDescription(const FEquipmentRollData& Roll);
};
