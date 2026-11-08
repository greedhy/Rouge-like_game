// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Containers/Array.h"
#include "MyInputConfig.generated.h"

class UInputAction;
USTRUCT(BlueprintType)
struct FMyInputAction
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly)
    UInputAction* InputAction = nullptr;

    UPROPERTY(EditDefaultsOnly)
    FGameplayTag InputTag = FGameplayTag();
};

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyInputConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = false) const;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TArray<FMyInputAction> AbilityInputActions;
};
