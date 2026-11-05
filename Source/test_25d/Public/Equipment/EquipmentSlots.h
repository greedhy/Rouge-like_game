#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
struct FEquipmentSlotDefinition { FGameplayTag Tag; const TCHAR* Label; bool bElement; };
namespace EquipmentSlots { TEST_25D_API const TArray<FEquipmentSlotDefinition>& All(); }
