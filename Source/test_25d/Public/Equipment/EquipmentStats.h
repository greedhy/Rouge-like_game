#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
struct FEquipmentStatDefinition
{
    FGameplayTag Tag;
    FGameplayAttribute Attribute;
    const TCHAR* Label;
};
/** Single registry for supported persistent additive equipment attributes. */
namespace EquipmentStats
{
    TEST_25D_API const TArray<FEquipmentStatDefinition>& All();
    TEST_25D_API const FEquipmentStatDefinition* Find(FGameplayTag Tag);
} // namespace EquipmentStats
