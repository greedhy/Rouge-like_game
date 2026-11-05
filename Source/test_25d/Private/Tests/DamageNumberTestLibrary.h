#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyAbilityTypes.h"
#include "DamageNumberTestLibrary.generated.h"
UCLASS()
class UDamageNumberTestLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,meta=(DevelopmentOnly)) static bool ScheduleDamagePair(const FDamageEffectParams& Params);
};
