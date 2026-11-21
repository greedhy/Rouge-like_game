// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Game/PlayerProfession.h"
#include "AbilityInfo.generated.h"

/**
 * 
 */

class UGameplayAbility;

USTRUCT(BlueprintType)
struct FMyAbilityInfo
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag AbilityTag = FGameplayTag();

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag InputTag = FGameplayTag();

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag StatusTag = FGameplayTag();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag CooldownTag = FGameplayTag();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag AbilityType = FGameplayTag();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<const UTexture2D> Icon = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<const UMaterialInterface> BackgroundMaterial = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    int32 LevelRequirement = 1;

    // 旧目录默认归法师；近战可学习技能在资产中显式设为 Stander。
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EPlayerProfession RequiredProfession = EPlayerProfession::Mage;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TSubclassOf<UGameplayAbility> Ability;
};

UCLASS()
class TEST_25D_API UAbilityInfo : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityInformation")
    TArray<FMyAbilityInfo> AbilityInformation;

    FMyAbilityInfo FindAbilityInfoForTag(const FGameplayTag& AbilityTag, bool bLogNotFound = false) const;
    static EPlayerProfession ResolveProfession(const AActor* Actor);
    bool CanLearn(const FGameplayTag& AbilityTag, const AActor* Actor) const;
};
