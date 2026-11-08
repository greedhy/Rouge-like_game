#include "Backpack/WorldPickupComponent.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/BackpackComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Player/MyPlayerController.h"
#include "Player/MyPlayerState.h"

UWorldPickupComponent::UWorldPickupComponent()
{
    SetIsReplicatedByDefault(true);
}

void UWorldPickupComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner()->HasAuthority())
    {
        ConfigureOwnerForNetworkedPickup(GetOwner());
        ResolveDefinition();
        TArray<UPrimitiveComponent*> Components;
        GetOwner()->GetComponents(Components);
        for (UPrimitiveComponent* Component : Components)
        {
            Component->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOwnerOverlap);
            Component->OnComponentEndOverlap.AddDynamic(this, &ThisClass::HandleOwnerEndOverlap);
        }
    }
}

void UWorldPickupComponent::ConfigureOwnerForNetworkedPickup(AActor* PickupActor)
{
    if (IsValid(PickupActor))
    {
        PickupActor->SetReplicates(true);
        PickupActor->SetReplicateMovement(true);
        PickupActor->ForceNetUpdate();
    }
}

void UWorldPickupComponent::ConfigurePickup(const FPrimaryAssetId& InItemDefinitionId, int32 InQuantity,
                                            bool bInForceInteract, const FEquipmentRollData* ExistingRoll)
{
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        ItemInstanceId.Invalidate();
        RollData = ExistingRoll ? *ExistingRoll : FEquipmentRollData();
        ItemDefinitionId = InItemDefinitionId;
        Quantity = FMath::Max(1, InQuantity);
        bForceInteract = bInForceInteract;
        ResolveDefinition();
    }
}

void UWorldPickupComponent::ResolveDefinition()
{
    if (IsValid(ItemDefinitions))
    {
        const FBackpackItemDefEntry* Definition =
            ItemDefinitionId.IsValid() ? ItemDefinitions->FindDefinition(ItemDefinitionId)
                                       : ItemDefinitions->FindDefinitionForPickupActor(GetOwner()->GetClass());
        if (Definition)
        {
            ItemDefinitionId = Definition->ItemDefinitionId;
            PickupMode = Definition->PickupMode;
            if (Definition->bRandomizedEquipment && RollData.GenerationVersion == 0)
            {
                FString Error;
                if (!FEquipmentGenerator::Generate(*ItemDefinitions, *Definition, 0, FMath::Rand(), RollData, Error))
                    UE_LOG(LogTemp, Warning, TEXT("Cannot generate pickup %s: %s"), *ItemDefinitionId.ToString(),
                           *Error);
            }
            if (Definition->bEquippable && !ItemInstanceId.IsValid())
                ItemInstanceId = FGuid::NewGuid();
        }
    }
}

bool UWorldPickupComponent::TryPickup(AMyPlayerState* PlayerState)
{
    if (bPickupInProgress || !GetOwner()->HasAuthority() || !IsValid(PlayerState) || !ItemDefinitionId.IsValid() ||
        Quantity <= 0)
    {
        return false;
    }
    if (const auto* Soul = Cast<AEquipmentSoulPickup>(GetOwner()); Soul && !Soul->CanPlayerPickup(PlayerState))
        return false;
    TGuardValue<bool> PickupGuard(bPickupInProgress, true);
    const auto* Definition =
        PlayerState->GetBackpackComponent()->ItemDefinitions
            ? PlayerState->GetBackpackComponent()->ItemDefinitions->FindDefinition(ItemDefinitionId)
            : nullptr;
    if (Definition && Definition->bEquippable)
    {
        if (Quantity != 1)
            return false;
        if (!ItemInstanceId.IsValid())
            ItemInstanceId = FGuid::NewGuid();
        if (!PlayerState->GetBackpackComponent()->TryAddExistingItem(ItemDefinitionId, ItemInstanceId, RollData))
            return false;
        Quantity = 0;
        OnPickedUp.Broadcast();
        GetOwner()->Destroy();
        return true;
    }
    const FInventoryAddResult Result = PlayerState->GetBackpackComponent()->TryAddItem(ItemDefinitionId, Quantity);
    if (Result.AcceptedQuantity <= 0)
    {
        return false;
    }
    Quantity -= Result.AcceptedQuantity;
    if (Quantity <= 0)
        GetOwner()->Destroy();
    else
        GetOwner()->ForceNetUpdate();
    return true;
}

void UWorldPickupComponent::HandleOwnerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                               UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
                                               bool bFromSweep, const FHitResult& SweepResult)
{
    APawn* Pawn = Cast<APawn>(OtherActor);
    if (!Pawn)
    {
        return;
    }

    if (GetPickupMode() == EPickupMode::Auto)
    {
        TryPickup(Pawn->GetPlayerState<AMyPlayerState>());
        return;
    }

    if (AMyPlayerController* Controller = Cast<AMyPlayerController>(Pawn->GetController()))
    {
        Controller->RegisterOverlappingInteractPickup(GetOwner());
    }
}

void UWorldPickupComponent::HandleOwnerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                                  UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
    if (APawn* Pawn = Cast<APawn>(OtherActor))
    {
        if (AMyPlayerController* Controller = Cast<AMyPlayerController>(Pawn->GetController()))
        {
            Controller->UnregisterOverlappingInteractPickup(GetOwner());
        }
    }
}

void UWorldPickupComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UWorldPickupComponent, ItemDefinitionId);
    DOREPLIFETIME(UWorldPickupComponent, Quantity);
    DOREPLIFETIME(UWorldPickupComponent, ItemInstanceId);
    DOREPLIFETIME(UWorldPickupComponent, RollData);
    DOREPLIFETIME(UWorldPickupComponent, PickupMode);
    DOREPLIFETIME(UWorldPickupComponent, bForceInteract);
}
