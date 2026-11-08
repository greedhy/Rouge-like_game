// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyPassiveAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
void UMyPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetAvatarActorFromActorInfo())))
    {
        MyASC->DeactivatePassiveAbility.AddUObject(this, &UMyPassiveAbility::ReceiveDeactivate);
    }
}

void UMyPassiveAbility::ReceiveDeactivate(const FGameplayTag& AbilityTag)
{
    if (AbilityTags.HasTagExact(AbilityTag))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}
