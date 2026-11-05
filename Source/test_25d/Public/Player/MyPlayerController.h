// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include"Input/MyInputConfig.h"
#include "UI/Widget/DamageTextComponent.h"

#include "MyPlayerController.generated.h"

class IHighLightInterface;
class UNiagaraSystem;
class USplineComponent;
class UInputMappingContext;
class UInputAction;
class UMyAbilitySystemComponent;
struct FInputActionValue;
class AMagicCircle;
class UUserWidget;

/**
 * 
 */
UCLASS()
class TEST_25D_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AMyPlayerController();
 AActor* GetFocusedEquipmentPickup() const { return FocusedEquipmentPickup.Get(); }
	virtual void PlayerTick(float DeltaTime) override;
	virtual void ReceivedPlayer() override;
	virtual void AcknowledgePossession(APawn* P) override;

	UFUNCTION(Client,Reliable)
	void ShowDamageNumber(float DamageAccount,ACharacter* TargetCharacter, bool bBlockHit, bool bCriticalHit);

	UFUNCTION(Server, Reliable)
	void ServerRequestPickup();

	// Called by authoritative WorldPickupComponent overlap callbacks. These are
	// deliberately actor-based so one pickup with several primitive components
	// remains a single candidate.
	void RegisterOverlappingInteractPickup(AActor* PickupActor);
	void UnregisterOverlappingInteractPickup(AActor* PickupActor);
	AActor* GetRandomOverlappingInteractPickup();
	int32 GetOverlappingInteractPickupCount() const;

	UFUNCTION(Server, Reliable)
	void ServerRequestSwap(int32 SourceSlotIndex, int32 TargetSlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerRequestUse(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerRequestDropWholeStack(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
	void ToggleBackpackUI();
	UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
	void OpenBackpackUI();
	UFUNCTION(BlueprintCallable, Category = "Backpack|UI")
	void CloseBackpackUI();
	UFUNCTION(BlueprintPure, Category = "Backpack|UI")
	bool IsBackpackUIOpen() const { return bBackpackUIOpen; }

	UFUNCTION(BlueprintCallable)
	void ShowMagicCircle(UMaterialInterface* DecalMaterial = nullptr);

	UFUNCTION(BlueprintCallable)
	void HideMagicCircle();


protected:
	virtual void BeginPlay() override;	
	virtual void SetupInputComponent() override;

private:
	friend class FPlayerBasicInputTest;
	void InitializeLocalInput();
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputMappingContext> PlayerContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ShiftAction;

	UPROPERTY(EditDefaultsOnly, Category = "Backpack|UI")
	TSubclassOf<UUserWidget> BackpackWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> BackpackWidget;
	UPROPERTY(Transient)
	TObjectPtr<class UEquipmentPanel> EquipmentWidget;

	bool bBackpackUIOpen = false;
 TWeakObjectPtr<AActor> FocusedEquipmentPickup;
 float EquipmentFocusElapsed = 0.f;

	void Move(const FInputActionValue& InputActionValue);
	void CursorTrace();

	TObjectPtr<AActor> LastActor;
	TObjectPtr<AActor> ThisActor;
	static void HighlightActor(AActor* InActor);
	static void UnHighlightActor(AActor* InActor);
	FHitResult CursorHit;
	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	void AbilityInputTagHeld(FGameplayTag InputTag);
	void RequestPickupFocusedActor();


	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UMyInputConfig> InputConfig;
	UPROPERTY()
	TObjectPtr<UMyAbilitySystemComponent> MyAbilitySystemComponent;

	UMyAbilitySystemComponent* GetASC();




	UPROPERTY(EditDefaultsOnly, Category = "Backpack", meta = (ClampMin = "0"))
	float PickupRequestRange = 250.f;
	TMap<TWeakObjectPtr<AActor>, int32> OverlappingInteractPickups;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UNiagaraSystem> ClickNiagaraSystem;




	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UDamageTextComponent> DamageTextComponentClass;

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AMagicCircle> MagicCircleClass;

	UPROPERTY()
	TObjectPtr<AMagicCircle> MagicCircle;

	void UpdateMagicCircleLocation();
};
