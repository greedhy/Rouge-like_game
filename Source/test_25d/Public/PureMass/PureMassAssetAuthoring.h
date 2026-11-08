#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PureMassAssetAuthoring.generated.h"

class USkeletalMesh;

/** Offline mesh authoring only. No per-entity runtime merge or attachment. */
UCLASS()
class TEST_25D_API UPureMassAssetAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Destination must be a new copy in /Game/PureMass/Rendering/Merged/. Does not save packages. */
    UFUNCTION(BlueprintCallable, Category = "PureMass|Authoring", meta = (DevelopmentOnly))
    static FString BakeRigidWeapon(USkeletalMesh* Destination, USkeletalMesh* Body, USkeletalMesh* Weapon,
                                   FName SocketName, FTransform WeaponOffset);
};
