/** Runtime compatibility facade / graph strategy -> child admission -> graph scheduler.
 * Request acknowledgement means a child was admitted; Emitter counts only successful admissions. */
#pragma once
#include "CoreMinimal.h"
#include "PureMass/PureMassCombatTarget.h"

struct TEST_25D_API FLegacyAbilityChildRequest
{
    FName Event;
    UObject* Config = nullptr;
    UObject* Context = nullptr;
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    FVector EventCenter = FVector::ZeroVector;
    FPureMassCombatTarget Target;
    int32 Depth = 0;
    bool bSingle = false;
    bool bConfirmedHit = false;
};
using FLegacyAbilityChildAdmission = TFunction<bool(const FLegacyAbilityChildRequest&)>;
