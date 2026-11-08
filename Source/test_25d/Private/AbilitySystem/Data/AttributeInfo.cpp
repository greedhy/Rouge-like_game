// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Data/AttributeInfo.h"

FMyAttributeInfo UAttributeInfo::FindAttributeInfoForTag(const FGameplayTag& AttributeTag, bool bLogNotFound) const
{

    for (const FMyAttributeInfo& Info : AttributeInfomation)
    {
        if (Info.AttributeTag.MatchesTagExact(AttributeTag))
        {
            return Info;
        }
    }
    if (bLogNotFound)
    {
        UE_LOG(LogTemp, Error, TEXT("Attribute with tag %s not found in AttributeInfo %s"), *AttributeTag.ToString(),
               *GetNameSafe(this));
    }

    return FMyAttributeInfo();
}
