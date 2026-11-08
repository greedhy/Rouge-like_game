// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Containers/Array.h"
#include "AttributeInfo.generated.h"

/**
 * 
 */

USTRUCT(BlueprintType)
struct FMyAttributeInfo
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTag AttributeTag = FGameplayTag();

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText AttributeName = FText();

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText AttributeDescription = FText();

    UPROPERTY(BlueprintReadOnly)
    float AttributeValue = 0.f;
};

UCLASS()
class TEST_25D_API UAttributeInfo : public UDataAsset
{
    GENERATED_BODY()
public:
    FMyAttributeInfo FindAttributeInfoForTag(const FGameplayTag& AttributeTag, bool bLogNotFound = false) const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TArray<FMyAttributeInfo> AttributeInfomation;
};
