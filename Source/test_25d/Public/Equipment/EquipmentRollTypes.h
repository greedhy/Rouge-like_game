#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "EquipmentRollTypes.generated.h"

USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentRolledStat
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag StatTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float Value = 0;
    bool operator==(const FEquipmentRolledStat& R) const
    {
        return StatTag == R.StatTag && Value == R.Value;
    }
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentRolledAffix
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName AffixId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName ExclusiveGroup;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 Tier = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bPrefix = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FEquipmentRolledStat> Stats;
    bool operator==(const FEquipmentRolledAffix& R) const
    {
        return ExclusiveGroup == R.ExclusiveGroup && AffixId == R.AffixId && Tier == R.Tier &&
               DisplayName.EqualTo(R.DisplayName) && bPrefix == R.bPrefix && Stats == R.Stats;
    }
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentRollData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 GenerationVersion = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName ProfileId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ItemLevel = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 RandomSeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName RarityId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText RarityName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FEquipmentRolledStat> BaseStats;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FEquipmentRolledAffix> Affixes;
    bool operator==(const FEquipmentRollData& R) const
    {
        return ProfileId == R.ProfileId && GenerationVersion == R.GenerationVersion && ItemLevel == R.ItemLevel &&
               RandomSeed == R.RandomSeed && RarityId == R.RarityId && RarityName.EqualTo(R.RarityName) &&
               BaseStats == R.BaseStats && Affixes == R.Affixes;
    }
    bool operator!=(const FEquipmentRollData& R) const
    {
        return !(*this == R);
    }
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentStatRange
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag StatTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FScalableFloat MinValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FScalableFloat MaxValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DecimalPlaces = 0;
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentGenerationProfileRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DefaultItemLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FEquipmentStatRange> BaseStats;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> RarityIds;
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentRarityRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Weight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinAffixes = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxAffixes = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxPrefixes = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxSuffixes = 3;
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentAffixTier
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Tier = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MinItemLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxItemLevel = 10000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FEquipmentStatRange> Stats;
};
USTRUCT(BlueprintType)
struct TEST_25D_API FEquipmentAffixRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bPrefix = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ExclusiveGroup;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Weight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> Profiles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FEquipmentAffixTier> Tiers;
};
