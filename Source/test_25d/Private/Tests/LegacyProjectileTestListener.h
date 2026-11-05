#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "LegacyProjectileTestListener.generated.h"

UCLASS()
class ULegacyProjectileTestListener : public UObject
{
 GENERATED_BODY()
public:
 bool bCalled=false;
 UFUNCTION() void DestroyOnCollisionChange(UPrimitiveComponent* Component)
 {
  bCalled=true;
  Component->GetOwner()->Destroy();
 }
};
