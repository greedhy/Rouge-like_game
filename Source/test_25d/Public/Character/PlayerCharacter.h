// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/BaseCharacter.h"
#include "Interface/PlayerInterface.h"
#include "PlayerCharacter.generated.h"

class USpringArmComponent;

class UCameraComponent;
class UNiagaraComponent;
class UBackpackComponent;
class ULoadScreenSaveGame;

UCLASS()
class TEST_25D_API APlayerCharacter : public ABaseCharacter,public IPlayerInterface
{
	GENERATED_BODY()
public:
	APlayerCharacter();
	virtual void PostInitializeComponents() override;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	//player interface
	virtual void AddToXP_Implementation(int32 InXP) override;
	virtual void LevelUp_Implementation() override;
	virtual int32 GetXP_Implementation() const override;
	virtual int32 FindLevelForXP_Implementation(int32 InXP) const override;
	virtual int32 GetAttributePointsReward_Implementation(int32 Level) const override;
	virtual int32 GetSpellPointsReward_Implementation(int32 Level) const override;
	virtual void AddToPlayerLevel_Implementation(int32 InPlayerLevel) override;
	virtual void AddToAttributePoints_Implementation(int32 InAttributePoints) override;
	virtual void AddToSpellPoints_Implementation(int32 InSpellPoints) override;
	virtual int32 GetAttributePoints_Implementation() const override;
	virtual int32 GetSpellPoints_Implementation() const override;
	virtual void ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial) override;
	virtual void HideMagicCircle_Implementation() override;
	virtual void SaveProgress_Implementation(const FName& CheckpointTag) override;
	void WriteProgressToSaveData(ULoadScreenSaveGame* SaveData, const FName& CheckpointTag);

	UFUNCTION(BlueprintPure, Category = "Backpack")
	UBackpackComponent* GetBackpackComponent() const;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UNiagaraComponent> LevelUpNiagaraComponent;

	// combat interface
	virtual int32 GetPlayerLevel_Implementation() ;
	virtual void Die(const FVector& DeathImpulse) override;

	UPROPERTY(EditDefaultsOnly)
	float DeathTime = 5.f;

	FTimerHandle DeathTimer;
	// Overrides of replicated notify functions from ABaseCharacter - do not redeclare UFUNCTION() here
	virtual void OnRep_Stunned() override;

	virtual void OnRep_Burned() override;
	void LoadProgress();

protected:
	virtual void InitAbilityActorInfo() override;
private:

	

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraBoom;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastLevelUpParticles() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> TopDownCameraComponent;
};
