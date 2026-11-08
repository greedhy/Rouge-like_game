// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "MyPassiveAbility.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyPassiveAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    void ReceiveDeactivate(const FGameplayTag& AbilityTag);
};
