#pragma once
#include "CoreMinimal.h"
#include "Character/PlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "LifecyclePlayerProbe.generated.h"
// Meshless fixture exercises the inherited production Die/EndPlay without querying an absent socket.
UCLASS()
class ALifecyclePlayerProbe : public APlayerCharacter
{
 GENERATED_BODY()
public:
 ALifecyclePlayerProbe(){GetWeapon_Implementation()->SetupAttachment(GetMesh());}
};
