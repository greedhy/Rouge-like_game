#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LifecycleUIProbe.generated.h"
UCLASS()
class ULifecycleUIProbe : public UObject
{
 GENERATED_BODY()
public:
 int32 Calls=0;
 UFUNCTION() void Health(float Value){++Calls;}
};
