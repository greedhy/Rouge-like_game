#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentSlots.h"
#include "Equipment/EquipmentStats.h"
#include "Equipment/EquipmentGameplayEffect.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ScopeExit.h"
#include "Equipment/WeaponSetConfig.h"

UEquipmentComponent::UEquipmentComponent() { SetIsReplicatedByDefault(true); }
void UEquipmentComponent::OnRegister()
{
 Super::OnRegister();
 // Extend legacy blueprint defaults without changing saved slot identities.
 for (const auto& Slot : EquipmentSlots::All()) AvailableSlots.AddTag(Slot.Tag);
}
UBackpackComponent* UEquipmentComponent::Backpack() const
{
 const auto* State = Cast<AMyPlayerState>(GetOwner());
 return State ? State->GetBackpackComponent() : nullptr;
}
UAbilitySystemComponent* UEquipmentComponent::ASC() const
{
 const auto* State = Cast<AMyPlayerState>(GetOwner());
 return State ? State->GetAbilitySystemComponent() : nullptr;
}
const FBackpackItemDefEntry* UEquipmentComponent::Definition(const FPrimaryAssetId& Id) const
{
 const auto* Bag = Backpack();
 return Bag && Bag->ItemDefinitions ? Bag->ItemDefinitions->FindDefinition(Id) : nullptr;
}
bool UEquipmentComponent::ContainsInstance(const FGuid& Id) const
{
 return Id.IsValid() && Slots.ContainsByPredicate([&](const FEquipmentSlot& S){ return S.ItemInstanceId == Id; });
}
bool UEquipmentComponent::CanMutate(int32 BagRevision, int32 EquipmentRevision) const
{
 const auto* Bag = Backpack();
 const auto* AbilitySystem = ASC();
 const auto* State = Cast<AMyPlayerState>(GetOwner());
 if (!State || !State->HasAuthority() || !Bag || bMutating || Bag->bEquipmentMutation ||
     BagRevision != Bag->GetRevision() || EquipmentRevision != Revision || !AbilitySystem ||
     AbilitySystem->GetOwnerActor() != State || !AbilitySystem->GetAvatarActor() ||
     AbilitySystem->GetAvatarActor() != State->GetPawn()) return false;
 AActor* Avatar = AbilitySystem->GetAvatarActor();
 if (Avatar->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Avatar)) return false;
 const auto* Dungeon = Cast<ADungeonPlayerCharacter>(Avatar);
 return !Dungeon || !Dungeon->IsDungeonDowned();
}
bool UEquipmentComponent::ValidateRecords(const TArray<FEquipmentSlot>& Records) const
{
 int32 WeaponSets=0;
 TSet<FGuid> Ids;
 TSet<FGameplayTag> UsedSlots;
 for (const auto& Slot : Records)
 {
  const auto* Def = Definition(Slot.ItemDefinitionId);
  if (!Slot.ItemInstanceId.IsValid() || Ids.Contains(Slot.ItemInstanceId) || UsedSlots.Contains(Slot.SlotTag) ||
      !AvailableSlots.HasTagExact(Slot.SlotTag) || !Def || !Def->bEquippable || Def->bStackable || Def->MaxStack != 1 ||
      !Def->AllowedEquipmentSlots.HasTagExact(Slot.SlotTag) || !Def->EquipEffect ||
      !Def->EquipEffect.GetDefaultObject()->IsSafeEquipmentEffect(Def->EquipEffectLevel, Def->bRandomizedEquipment) ||
      !FEquipmentGenerator::ValidateRoll(Slot.RollData) || Def->bRandomizedEquipment != (Slot.RollData.GenerationVersion > 0) ||
      (Def->bRandomizedEquipment && Slot.RollData.ProfileId != Def->GenerationProfile)) return false;
  if(Def->WeaponSet && (++WeaponSets>1 || !Def->WeaponSet->Validate())) return false;
  Ids.Add(Slot.ItemInstanceId); UsedSlots.Add(Slot.SlotTag);
 }
 return true;
}
FActiveGameplayEffectHandle UEquipmentComponent::Apply(const FBackpackItemDefEntry& Def, const FEquipmentRollData& Roll)
{
 auto* AbilitySystem = ASC();
 auto Context = AbilitySystem->MakeEffectContext();
 Context.AddSourceObject(this);
 auto Spec = AbilitySystem->MakeOutgoingSpec(Def.EquipEffect, Def.EquipEffectLevel, Context);
 if (!Spec.IsValid()) return {};
 if (Def.bRandomizedEquipment)
 {
  if (!FEquipmentGenerator::ValidateRoll(Roll) || Roll.ProfileId != Def.GenerationProfile) return {};
  const auto Totals=FEquipmentGenerator::GetTotals(Roll);
  const auto* Effect = Def.EquipEffect.GetDefaultObject();
  TSet<FGameplayTag> Supported;
  for (const auto& Modifier : Effect->Modifiers)
  {
   const auto Tag = Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag;
   Supported.Add(Tag);
   Spec.Data->SetSetByCallerMagnitude(Tag, Totals.FindRef(Tag));
  }
  // Subset templates are allowed, but must never silently discard rolled stats.
  for (const auto& Total : Totals) if (!Supported.Contains(Total.Key)) return {};

 }
 auto Handle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 const auto* Active = AbilitySystem->GetActiveGameplayEffect(Handle);
 if (!Handle.WasSuccessfullyApplied() || !Active || Active->bIsInhibited)
 {
  if (Active) AbilitySystem->RemoveActiveGameplayEffect(Handle);
  return {};
 }
 return Handle;
}
bool UEquipmentComponent::HasEffect(const FGuid& Id) const
{
 const auto* Handle = Handles.Find(Id);
 const auto* Active = Handle && EffectASC.Get() == ASC() && EffectASC.IsValid() ? EffectASC->GetActiveGameplayEffect(*Handle) : nullptr;
 return Active && !Active->bIsInhibited;
}
void UEquipmentComponent::ClampResources()
{
 auto* AbilitySystem = ASC();
 if (!AbilitySystem) return;
 const float Health = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
 const float Mana = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
 const float MaxHealth = FMath::Max(0.f, AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()));
 const float MaxMana = FMath::Max(0.f, AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()));
 if (Health > MaxHealth) AbilitySystem->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), MaxHealth);
 if (Mana > MaxMana) AbilitySystem->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), MaxMana);
}
void UEquipmentComponent::Commit()
{
 auto* Bag = Backpack();
 ++Revision;
 Bag->SynchronizeReplicatedSlotsFromModel();
 ClampResources();
 GetOwner()->ForceNetUpdate();
 Bag->InventoryModel->OnInventoryUpdated.Broadcast(Bag->InventoryModel->Slots);
 OnEquipmentChanged.Broadcast();
 // The existing GameMode persistence listener now exports BOTH collections.
 Bag->OnBackpackStateChanged.Broadcast();
}
bool UEquipmentComponent::TryEquip(int32 Index, FGameplayTag Tag, FGuid Expected, int32 BagRevision, int32 EquipmentRevision)
{
 if (!CanMutate(BagRevision, EquipmentRevision)) return false;
 if (bNeedsReconcile && !ReconcileEffects()) return false;
 auto* Bag = Backpack(); Bag->Initialize();
 auto& Items = Bag->InventoryModel->Slots;
 if (!Items.IsValidIndex(Index) || Items[Index].IsEmpty() || Items[Index].Quantity != 1 ||
     Items[Index].ItemInstanceId != Expected || ContainsInstance(Expected)) return false;
 FEquipmentSlot NewSlot; NewSlot.SlotTag = Tag; NewSlot.ItemDefinitionId = Items[Index].ItemDefinitionId; NewSlot.ItemInstanceId = Expected; NewSlot.RollData = Items[Index].RollData;
 if (!ValidateRecords({NewSlot})) return false;
 const int32 OldIndex = Slots.IndexOfByPredicate([&](const FEquipmentSlot& S){ return S.SlotTag == Tag; });
 if (OldIndex != INDEX_NONE && !HasEffect(Slots[OldIndex].ItemInstanceId)) { bNeedsReconcile = true; OnRep_State(); return false; }
 TGuardValue<bool> Guard(bMutating, true);
 TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
 auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
 if (!Attributes) return false;
 TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
 TArray<FEquipmentSlot> Proposed=Slots;
 if(OldIndex!=INDEX_NONE) Proposed[OldIndex]=NewSlot; else Proposed.Add(NewSlot);
 TArray<FGameplayAbilitySpecHandle> Prepared;
 if(!PrepareWeaponSet(Proposed,Prepared)) return false;
 bool WeaponCommitted=false;
 ON_SCOPE_EXIT { if(!WeaponCommitted) DiscardWeaponSet(Prepared); };
 const auto Handle = Apply(*Definition(NewSlot.ItemDefinitionId), NewSlot.RollData);
 if (!Handle.IsValid()) return false;
 EffectASC = ASC();
 BindEffects();
 FInventorySlot Returned;
 if (OldIndex != INDEX_NONE)
 {
  const auto OldSlot = Slots[OldIndex];
  if (!ASC()->RemoveActiveGameplayEffect(Handles.FindChecked(OldSlot.ItemInstanceId)))
  { ASC()->RemoveActiveGameplayEffect(Handle); return false; }
  Handles.Remove(OldSlot.ItemInstanceId);
  Returned.ItemDefinitionId = OldSlot.ItemDefinitionId; Returned.ItemInstanceId = OldSlot.ItemInstanceId; Returned.Quantity = 1; Returned.RollData = OldSlot.RollData;
  Slots[OldIndex] = NewSlot;
 }
 else Slots.Add(NewSlot);
 Items[Index] = Returned;
 Handles.Add(Expected, Handle);
 CommitWeaponSet(Slots,Prepared); WeaponCommitted=true;
 Commit(); return true;
}
bool UEquipmentComponent::TryUnequip(FGameplayTag Tag, FGuid Expected, int32 BagRevision, int32 EquipmentRevision)
{
 if (!CanMutate(BagRevision, EquipmentRevision)) return false;
 if (bNeedsReconcile && !ReconcileEffects()) return false;
 auto* Bag = Backpack(); Bag->Initialize();
 const int32 Index = Slots.IndexOfByPredicate([&](const FEquipmentSlot& S){ return S.SlotTag == Tag && S.ItemInstanceId == Expected; });
 const int32 Empty = Bag->InventoryModel->Slots.IndexOfByPredicate([](const FInventorySlot& S){ return S.IsEmpty(); });
 if (Index == INDEX_NONE || Empty == INDEX_NONE) return false;
 if (!HasEffect(Expected)) { bNeedsReconcile = true; OnRep_State(); return false; }
 TGuardValue<bool> Guard(bMutating, true); TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
 auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
 if (!Attributes) return false;
 TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
 TArray<FEquipmentSlot> Proposed=Slots; Proposed.RemoveAt(Index);
 TArray<FGameplayAbilitySpecHandle> Prepared;
 if(!PrepareWeaponSet(Proposed,Prepared)) return false;
 bool WeaponCommitted=false;
 ON_SCOPE_EXIT { if(!WeaponCommitted) DiscardWeaponSet(Prepared); };
 if (!ASC()->RemoveActiveGameplayEffect(Handles.FindChecked(Expected))) return false;
 auto& Item = Bag->InventoryModel->Slots[Empty];
 Item.ItemDefinitionId = Slots[Index].ItemDefinitionId; Item.ItemInstanceId = Expected; Item.Quantity = 1; Item.RollData = Slots[Index].RollData;
 Slots.RemoveAt(Index); Handles.Remove(Expected); CommitWeaponSet(Slots,Prepared); WeaponCommitted=true; Commit(); return true;
}
void UEquipmentComponent::BindEffects()
{
 if (EffectASC.IsValid() && !RemovedDelegate.IsValid())
  RemovedDelegate = EffectASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this, &ThisClass::OnEffectRemoved);
}
void UEquipmentComponent::OnEffectRemoved(const FActiveGameplayEffect& Effect)
{
 if (bMutating) return;
 for (const auto& Pair : Handles) if (Pair.Value == Effect.Handle)
 {
  bNeedsReconcile = true; GetOwner()->ForceNetUpdate(); OnRep_State(); return;
 }
}
void UEquipmentComponent::ClearEffects()
{
 if (EffectASC.IsValid()) EffectASC->OnAnyGameplayEffectRemovedDelegate().Remove(RemovedDelegate);
 RemovedDelegate.Reset();
 if (EffectASC.IsValid()) for (const auto& Pair : Handles) EffectASC->RemoveActiveGameplayEffect(Pair.Value);
 Handles.Reset(); EffectASC.Reset();
}
bool UEquipmentComponent::RestoreCombined(const TArray<FReplicatedInventorySlot>& Items, int32 Capacity, int32 BagRevision, const TArray<FEquipmentSlot>& Records, int32 SavedRevision)
{
 auto* Bag = Backpack();
 if (!GetOwner()->HasAuthority() || bMutating || !Bag || Bag->bEquipmentMutation || Capacity < 0 || Items.Num() != Capacity || !ValidateRecords(Records)) return false;
 TSet<FGuid> SeenInstances; for (const auto& Record:Records) SeenInstances.Add(Record.ItemInstanceId);
 for (const auto& Item:Items)
 {
  if (!FEquipmentGenerator::ValidateRoll(Item.RollData)) return false;
  if (Item.IsEmpty()) { if (Item.RollData.GenerationVersion != 0) return false; continue; }
  const auto* Def=Definition(Item.ItemDefinitionId);
  if (!FEquipmentGenerator::ValidateForDefinition(Def,Item.RollData)) return false;
  if (Def && Def->bEquippable && (Item.Quantity!=1 || !Item.ItemInstanceId.IsValid())) return false;
  if (Item.ItemInstanceId.IsValid() && SeenInstances.Contains(Item.ItemInstanceId)) return false;
  if (Item.ItemInstanceId.IsValid()) SeenInstances.Add(Item.ItemInstanceId);
 }

 {
  TGuardValue<bool> Guard(bMutating, true); TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
  auto* Attributes = ASC() ? const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>()) : nullptr;
  bool Unused = false;
  TGuardValue<bool> AttributeGuard(Attributes ? Attributes->bEquipmentChanging : Unused, true);
  // Live restores must validate and stage the new ability group before publishing either snapshot.
  // Initial loads without actor info remain pending until ReconcileEffects after initialization.
  const bool bLiveRestore=IsReadyForCurrentAvatar();
  TArray<FGameplayAbilitySpecHandle> Prepared;
  if(bLiveRestore && !PrepareWeaponSet(Records,Prepared)) return false;
  bool WeaponCommitted=false;
  ON_SCOPE_EXIT { if(!WeaponCommitted) DiscardWeaponSet(Prepared); };
  Bag->Initialize();
  // Publish both record collections before any GAS removal callback can inspect them.
  Slots = Records; Revision = FMath::Max(0, SavedRevision);
  Bag->ReplicatedSlots.Items = Items; Bag->Capacity = Capacity; Bag->Revision = FMath::Max(0, BagRevision);
  Bag->ReplicatedSlots.MarkArrayDirty();
  Bag->InventoryModel->ApplyReplicatedSlots(Items, Capacity, false);
  ClearEffects(); bNeedsReconcile = !Slots.IsEmpty();
  if(bLiveRestore) CommitWeaponSet(Slots,Prepared);
  WeaponCommitted=true;
 }
 if (IsReadyForCurrentAvatar()) ReconcileEffects();
 {
  TGuardValue<bool> Guard(bMutating, true); TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
  Bag->InventoryModel->OnInventoryMaxSlotsChanged.Broadcast(Capacity);
  Bag->InventoryModel->OnInventoryUpdated.Broadcast(Bag->InventoryModel->Slots);
  OnRep_State(); GetOwner()->ForceNetUpdate();
 }
 return true;
}
bool UEquipmentComponent::IsReadyForCurrentAvatar() const { return ASC() && ReadyAvatar.IsValid() && ReadyAvatar.Get() == ASC()->GetAvatarActor(); }
bool UEquipmentComponent::ReconcileEffects()
{
 if (!GetOwner()->HasAuthority() || bMutating || !ASC() || ASC()->GetOwnerActor() != GetOwner() || !ASC()->GetAvatarActor()) return false;
 auto* Bag = Backpack();
 if (!Bag || Bag->bEquipmentMutation) return false;
 TGuardValue<bool> Guard(bMutating, true); TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
 auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
 if (!Attributes) return false;
 TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
 if (EffectASC.Get() != ASC()) ClearEffects();
 EffectASC = ASC();
 BindEffects();
 if(ReadyAvatar.Get()!=ASC()->GetAvatarActor()) ++WeaponGeneration;
 ReadyAvatar = ASC()->GetAvatarActor();
 bNeedsReconcile = !ValidateRecords(Slots);
 if (!bNeedsReconcile) for (const auto& Slot : Slots)
 {
  if (HasEffect(Slot.ItemInstanceId)) continue;
  if (auto* Old = Handles.Find(Slot.ItemInstanceId)) ASC()->RemoveActiveGameplayEffect(*Old);
  auto Handle = Apply(*Definition(Slot.ItemDefinitionId), Slot.RollData);
  if (Handle.IsValid()) Handles.Add(Slot.ItemInstanceId, Handle);
  else { Handles.Remove(Slot.ItemInstanceId); bNeedsReconcile = true; }
 }
 if(!bNeedsReconcile)
 {
  TArray<FGameplayAbilitySpecHandle> Prepared;
  if(PrepareWeaponSet(Slots,Prepared)) CommitWeaponSet(Slots,Prepared);
  else bNeedsReconcile=true;
 }
 ClampResources(); OnRep_State(); return !bNeedsReconcile;
}
void UEquipmentComponent::RequestEquip(int32 Index, FGameplayTag Tag)
{
 auto* Bag = Backpack();
 if (!Bag) return;
 Bag->Initialize();
 const auto& Items = Bag->GetInventoryModel()->GetSlots();
 if (!Items.IsValidIndex(Index) || Items[Index].IsEmpty()) return;
 ServerEquip(Index, Tag, Items[Index].ItemInstanceId, Bag->GetRevision(), Revision);
}
void UEquipmentComponent::RequestUnequip(FGameplayTag Tag)
{
 const auto* Bag = Backpack();
 const auto* Slot = Slots.FindByPredicate([&](const FEquipmentSlot& S){ return S.SlotTag == Tag; });
 if (Bag && Slot) ServerUnequip(Tag, Slot->ItemInstanceId, Bag->GetRevision(), Revision);
}
void UEquipmentComponent::ServerEquip_Implementation(int32 Index, FGameplayTag Tag, FGuid Id, int32 BagRevision, int32 EquipmentRevision)
{ ClientResult(TryEquip(Index, Tag, Id, BagRevision, EquipmentRevision)); GetOwner()->ForceNetUpdate(); }
void UEquipmentComponent::ServerUnequip_Implementation(FGameplayTag Tag, FGuid Id, int32 BagRevision, int32 EquipmentRevision)
{ ClientResult(TryUnequip(Tag, Id, BagRevision, EquipmentRevision)); GetOwner()->ForceNetUpdate(); }
void UEquipmentComponent::ClientResult_Implementation(bool bSucceeded) { OnRequestResult.Broadcast(bSucceeded); }
void UEquipmentComponent::OnRep_State() { RefreshWeaponVisuals(); OnEquipmentChanged.Broadcast(); }
void UEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{ if (GetOwner()->HasAuthority()) { ++WeaponGeneration; CommitWeaponSet({},{}); ClearEffects(); } Super::EndPlay(Reason); }
void UEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME(UEquipmentComponent, Slots); DOREPLIFETIME(UEquipmentComponent, Revision); DOREPLIFETIME(UEquipmentComponent, bNeedsReconcile);
}
