#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PureMassLegacyProjectile.generated.h"

class AMyProjectile;
class USceneComponent;

/** Opt-in adapter attached only to the original FireBolt in enabled Mass worlds.
 * Uses actual movement segments (including homing/substeps), never a monster Actor proxy.
 */
UCLASS()
class TEST_25D_API UPureMassLegacyProjectile : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void OnRegister() override;
    virtual void BeginPlay() override;
    virtual void OnUnregister() override;

private:
    void Moved(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);
    TWeakObjectPtr<AMyProjectile> Projectile;
    FVector PreviousLocation = FVector::ZeroVector;
    bool bConsumed = false;
};
