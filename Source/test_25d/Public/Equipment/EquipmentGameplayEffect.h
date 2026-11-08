#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EquipmentGameplayEffect.generated.h"

/** Reversible equipment modifiers. Arbitrary GE components/executions are deliberately disallowed. */
UCLASS(Blueprintable)
class TEST_25D_API UEquipmentGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UEquipmentGameplayEffect();
    bool IsSafeEquipmentEffect(float Level, bool bAllowRolled = false) const;
};

/** Fixed whitelist of attributes; per-instance values are injected into the Spec, never the CDO. */
UCLASS(Blueprintable)
class TEST_25D_API URandomEquipmentGameplayEffect : public UEquipmentGameplayEffect
{
    GENERATED_BODY()
public:
    URandomEquipmentGameplayEffect();
};
