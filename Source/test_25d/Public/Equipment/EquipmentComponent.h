#pragma once
#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpec.h"
#include "EquipmentComponent.generated.h"

class UAbilitySystemComponent;
class UBackpackComponent;
struct FReplicatedInventorySlot;
struct FActiveGameplayEffect;
struct FBackpackItemDefEntry;

USTRUCT(BlueprintType)
struct FEquipmentSlot
{
 GENERATED_BODY()
 UPROPERTY(SaveGame, BlueprintReadOnly) FGameplayTag SlotTag;
 UPROPERTY(SaveGame, BlueprintReadOnly) FPrimaryAssetId ItemDefinitionId;
 UPROPERTY(SaveGame, BlueprintReadOnly) FGuid ItemInstanceId;
 UPROPERTY(SaveGame, BlueprintReadOnly) FEquipmentRollData RollData;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEquipmentChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquipmentRequestResult, bool, bSucceeded);

UCLASS(ClassGroup=(Equipment), meta=(BlueprintSpawnableComponent))
class TEST_25D_API UEquipmentComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEquipmentComponent();
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FGameplayTagContainer AvailableSlots;
 UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly) TArray<FEquipmentSlot> Slots;
 UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly) int32 Revision = 0;
 UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly) bool bNeedsReconcile = false;
 UPROPERTY(BlueprintAssignable) FEquipmentChanged OnEquipmentChanged;
 UPROPERTY(BlueprintAssignable) FEquipmentRequestResult OnRequestResult;
 UFUNCTION(BlueprintCallable, Category="Equipment") void RequestEquip(int32 BackpackSlot, FGameplayTag SlotTag);
 UFUNCTION(BlueprintCallable, Category="Equipment") void RequestUnequip(FGameplayTag SlotTag);
 UFUNCTION(BlueprintCallable, Server, Reliable) void ServerEquip(int32 BackpackSlot, FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
 UFUNCTION(BlueprintCallable, Server, Reliable) void ServerUnequip(FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
 UFUNCTION(Client, Reliable) void ClientResult(bool bSucceeded);
 bool TryEquip(int32 BackpackSlot, FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
 bool TryUnequip(FGameplayTag SlotTag, FGuid ExpectedInstance, int32 BackpackRevision, int32 EquipmentRevision);
 /** Called only after actor info and base attribute initialization. Safe to repeat. */
 bool ReconcileEffects();
 bool IsReadyForCurrentAvatar() const;
 bool ValidateRecords(const TArray<FEquipmentSlot>& Records) const;
 bool RestoreCombined(const TArray<FReplicatedInventorySlot>& Items, int32 Capacity, int32 BagRevision, const TArray<FEquipmentSlot>& Records, int32 SavedRevision);
 UFUNCTION(BlueprintPure) class UWeaponSetConfig* GetWeaponSet() const;
 uint32 GetWeaponGeneration() const { return WeaponGeneration; }
 bool IsWeaponAbility(FGameplayAbilitySpecHandle Handle) const;
 FGameplayTag GetSavedInputSlot(const FGameplayAbilitySpec& Spec) const;
 bool CanAssignWeaponSlot(FGameplayAbilitySpecHandle Handle, FGameplayTag Slot) const;
 void RefreshWeaponVisuals();
 void RememberWeaponSlotDisplacement(FGameplayAbilitySpecHandle Incoming,const FGameplayAbilitySpec& Displaced);
 bool ContainsInstance(const FGuid& Id) const;
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
 virtual void OnRegister() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UFUNCTION() void OnRep_State();
 bool CanMutate(int32 BackpackRevision, int32 EquipmentRevision) const;
 UBackpackComponent* Backpack() const;
 UAbilitySystemComponent* ASC() const;
 const FBackpackItemDefEntry* Definition(const FPrimaryAssetId& Id) const;
 FActiveGameplayEffectHandle Apply(const FBackpackItemDefEntry& Def, const FEquipmentRollData& Roll);
 bool HasEffect(const FGuid& Id) const;
 void Commit();
 void ClampResources();
 void ClearEffects();
 TWeakObjectPtr<UAbilitySystemComponent> EffectASC;
 TMap<FGuid, FActiveGameplayEffectHandle> Handles;
 bool bMutating = false;
 TWeakObjectPtr<UAbilitySystemComponent> WeaponASC;
 TArray<FGameplayAbilitySpecHandle> WeaponHandles;
 TMap<FGameplayAbilitySpecHandle, FGameplayTag> DisplacedSlots;
 TWeakObjectPtr<UWeaponSetConfig> AppliedWeaponSet;
 FGuid AppliedWeaponInstance;
 uint32 WeaponGeneration = 0;
 bool PrepareWeaponSet(const TArray<FEquipmentSlot>& Records, TArray<FGameplayAbilitySpecHandle>& Prepared);
 void CommitWeaponSet(const TArray<FEquipmentSlot>& Records, const TArray<FGameplayAbilitySpecHandle>& Prepared);
 void DiscardWeaponSet(const TArray<FGameplayAbilitySpecHandle>& Prepared);
 UWeaponSetConfig* WeaponSetFor(const TArray<FEquipmentSlot>& Records) const;

 FDelegateHandle RemovedDelegate;
 void BindEffects();
 void OnEffectRemoved(const FActiveGameplayEffect& Effect);
 TWeakObjectPtr<AActor> ReadyAvatar;
};
