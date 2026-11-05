#pragma once
#include "Character/SwordShieldPlayer.h"
#include "StanderPlayer.generated.h"

class UAnimInstance;
class USkeletalMesh;

UCLASS()
class TEST_25D_API AStanderPlayer : public ASwordShieldPlayer
{
 GENERATED_BODY()
public:
 AStanderPlayer();
 void ApplyWeaponSetVisuals(class UWeaponSetConfig* Set);
 virtual void BeginPlay() override;
private:
 UPROPERTY(Transient) TObjectPtr<USkeletalMesh> DefaultMainMesh;
 UPROPERTY(Transient) TObjectPtr<USkeletalMesh> DefaultOffhandMesh;
 UPROPERTY(Transient) TSubclassOf<UAnimInstance> DefaultAnimClass;
 FTransform DefaultMainTransform;
 FTransform DefaultOffhandTransform;
 FName DefaultMainSocket;
 FName DefaultOffhandSocket;
 bool bWeaponVisualDefaultsCaptured=false;
public:
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> StanderSword;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> StanderShield;
};
