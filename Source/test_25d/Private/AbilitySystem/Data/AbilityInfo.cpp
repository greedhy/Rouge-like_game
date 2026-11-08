// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Data/AbilityInfo.h"

#include "test_25d/MyLogChannels.h"

FMyAbilityInfo UAbilityInfo::FindAbilityInfoForTag(const FGameplayTag& AbilityTag, bool bLogNotFound) const
{
    for (const FMyAbilityInfo& Info : AbilityInformation)
    {
        if (Info.AbilityTag == AbilityTag)
        {
            return Info;
        }
    }

    if (bLogNotFound)
    {
        UE_LOG(LogMyChannel, Error, TEXT("Can't find info for AbilityTag [%s] on AbilityInfo [%s]"),
               *AbilityTag.ToString(), *GetNameSafe(this));
    }

    return FMyAbilityInfo();
}
