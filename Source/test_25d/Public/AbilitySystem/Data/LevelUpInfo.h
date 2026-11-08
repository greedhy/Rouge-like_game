// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LevelUpInfo.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct FMyLevelUpInfo
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
    int32 LevelUpRequirement = 0;

    UPROPERTY(EditDefaultsOnly)
    int32 AttributePointAward = 1;

    UPROPERTY(EditDefaultsOnly)
    int32 SpellPointAward = 1;
};

UCLASS()
class TEST_25D_API ULevelUpInfo : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly)
    TArray<FMyLevelUpInfo> LevelUpInformation;

    int32 FindLevelForXP(int32 XP) const;
};
