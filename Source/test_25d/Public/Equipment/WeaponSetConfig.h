#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WeaponSetConfig.generated.h"
class UGameplayAbility;
class UAnimInstance;
class USkeletalMesh;
USTRUCT(BlueprintType)
struct FWeaponAbilityEntry
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UGameplayAbility> Ability;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayTag InputTag;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Level = 1;
};
UCLASS(BlueprintType)
class TEST_25D_API UWeaponSetConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Family;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FWeaponAbilityEntry> Abilities;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UAnimInstance> AnimClass;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> MainMesh;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> OffhandMesh;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName MainSocket = TEXT("ik_hand_r");
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OffhandSocket = TEXT("lowerarm_l");
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform MainTransform;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform OffhandTransform;
 static bool IsWeaponInputSlot(FGameplayTag Slot);
 bool Validate() const;
};
