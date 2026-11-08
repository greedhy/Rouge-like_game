#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "EquipmentSoulPickup.generated.h"

class USphereComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UWidgetComponent;
class UWorldPickupComponent;
class UTextBlock;
class AMyPlayerState;

UCLASS()
class TEST_25D_API UEquipmentSoulLabel : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetLabel(const FText& Name);

protected:
    virtual void NativeOnInitialized() override;
    UPROPERTY()
    TObjectPtr<UTextBlock> Text;
};

/** A shared world representation for all equipment. Contents remain in the existing pickup component. */
UCLASS()
class TEST_25D_API AEquipmentSoulPickup : public AActor
{
    GENERATED_BODY()
public:
    AEquipmentSoulPickup();
    virtual void Tick(float DeltaSeconds) override;
    virtual void PostInitializeComponents() override;
    FText GetEquipmentName() const;
    bool CanPlayerPickup(const AMyPlayerState* Player) const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USphereComponent> PickupRange;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UNiagaraComponent> Soul;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UWidgetComponent> NameWidget;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UWorldPickupComponent> Pickup;
    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UNiagaraSystem> PickupEffect;

protected:
    virtual void BeginPlay() override;

private:
    void OnCollected();
    UFUNCTION(NetMulticast, Reliable)
    void MulticastCollected();
};

/** Place in the run map; server creates examples once after the generated entrance is ready. */
UCLASS()
class TEST_25D_API AEquipmentEntranceSamples : public AActor
{
    GENERATED_BODY()
public:
    AEquipmentEntranceSamples();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere, Category = "Equipment")
    TArray<FName> EquipmentNames;
    /** Additional weapon samples for a Stander trial; ignored for other characters. */
    UPROPERTY(EditAnywhere, Category = "Equipment")
    TArray<FName> StanderWeaponNames;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SpawnedCount = 0;

private:
    bool bBaseSamplesSpawned = false;
};
