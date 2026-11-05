#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "Backpack/ItemDefinition.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Character/StanderPlayer.h"
#include "Player/MyPlayerState.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbility.h"

UWeaponSetConfig* UEquipmentComponent::WeaponSetFor(const TArray<FEquipmentSlot>& Records) const
{
 for(const auto& Record:Records) if(const auto* Def=Definition(Record.ItemDefinitionId); Def && Def->WeaponSet) return Def->WeaponSet;
 return nullptr;
}
UWeaponSetConfig* UEquipmentComponent::GetWeaponSet() const { return WeaponSetFor(Slots); }
bool UEquipmentComponent::IsWeaponAbility(FGameplayAbilitySpecHandle Handle) const { return WeaponHandles.Contains(Handle); }
FGameplayTag UEquipmentComponent::GetSavedInputSlot(const FGameplayAbilitySpec& Spec) const
{
 if(!UMyAbilitySystemComponent::AbilityHasAnySlot(Spec)) if(const auto* Saved=DisplacedSlots.Find(Spec.Handle)) return *Saved;
 return UMyAbilitySystemComponent::GetInputTagFromSpec(Spec);
}
bool UEquipmentComponent::CanAssignWeaponSlot(FGameplayAbilitySpecHandle Handle,FGameplayTag Slot) const
{
 if(IsWeaponAbility(Handle) && !UWeaponSetConfig::IsWeaponInputSlot(Slot)) return false;
 // A weapon loadout owns its configured slots; its extra actions may replace those actions.
 if(const auto* Set=GetWeaponSet()) for(const auto& Entry:Set->Abilities)
  if(Entry.InputTag==Slot) return IsWeaponAbility(Handle);
 return true;
}
void UEquipmentComponent::DiscardWeaponSet(const TArray<FGameplayAbilitySpecHandle>& Prepared)
{ if(auto* A=ASC()) for(auto Handle:Prepared) A->ClearAbility(Handle); }
bool UEquipmentComponent::PrepareWeaponSet(const TArray<FEquipmentSlot>& Records,TArray<FGameplayAbilitySpecHandle>& Prepared)
{
 auto* A=Cast<UMyAbilitySystemComponent>(ASC()); if(!A) return WeaponSetFor(Records)==nullptr && WeaponHandles.IsEmpty();
 auto* Set=WeaponSetFor(Records);
 FGuid Instance; for(const auto& R:Records) if(const auto* D=Definition(R.ItemDefinitionId);D && D->WeaponSet) { Instance=R.ItemInstanceId; break; }
 int32 Count=0; for(const auto& R:Records) if(const auto* D=Definition(R.ItemDefinitionId);D && D->WeaponSet) ++Count;
 if(Count>1 || (Set && (!Set->Validate() || !Cast<AStanderPlayer>(A->GetAvatarActor())))) return false;
 if(Set==AppliedWeaponSet.Get() && Instance==AppliedWeaponInstance && WeaponASC.Get()==A)
 {
  bool Complete=true; for(auto H:WeaponHandles) Complete &= A->FindAbilitySpecFromHandle(H)!=nullptr;
  if(Complete && (!Set || WeaponHandles.Num()==Set->Abilities.Num())) return true;
 }
 // Cancellation must be possible before any old loadout state is changed.
 for(const auto& Spec:A->GetActivatableAbilities())
 {
  bool Displaced=WeaponHandles.Contains(Spec.Handle);
  if(Set) for(const auto& E:Set->Abilities) Displaced |= E.InputTag.IsValid() && UMyAbilitySystemComponent::AbilityHasSlot(Spec,E.InputTag);
  if(Displaced && Spec.IsActive())
  {
   const auto Instances=Spec.GetAbilityInstances();
   if(Instances.IsEmpty() && Spec.Ability && !Spec.Ability->CanBeCanceled()) return false;
   for(auto* AbilityInstance:Instances) if(AbilityInstance && AbilityInstance->IsActive() && !AbilityInstance->CanBeCanceled()) return false;
  }
 }
 if(!Set) return true;
 // Ability-tag lookup underlies the HUD/RPCs; never introduce ambiguous identities.
 for(const auto& Entry:Set->Abilities) for(const auto& Existing:A->GetActivatableAbilities())
  if(!WeaponHandles.Contains(Existing.Handle) && Entry.Ability.GetDefaultObject()->GetAssetTags().HasTagExact(A->GetAbilityTagFromSpec(Existing))) return false;
 for(const auto& Entry:Set->Abilities)
 {
  FGameplayAbilitySpec Spec(Entry.Ability,Entry.Level,INDEX_NONE,this);
  Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Unlocked);
  auto H=A->GiveAbility(Spec);
  if(!H.IsValid() || !A->FindAbilitySpecFromHandle(H)) { DiscardWeaponSet(Prepared); Prepared.Reset(); return false; }
  Prepared.Add(H);
 }
 return true;
}
void UEquipmentComponent::CommitWeaponSet(const TArray<FEquipmentSlot>& Records,const TArray<FGameplayAbilitySpecHandle>& Prepared)
{
 auto* A=Cast<UMyAbilitySystemComponent>(ASC()); if(!A) return;
 auto* Set=WeaponSetFor(Records);
 FGuid Instance; for(const auto& R:Records) if(const auto* D=Definition(R.ItemDefinitionId);D && D->WeaponSet) { Instance=R.ItemInstanceId; break; }
 if(Prepared.IsEmpty() && Set==AppliedWeaponSet.Get() && Instance==AppliedWeaponInstance && WeaponASC.Get()==A) return;
 ++WeaponGeneration;
 const auto& Tags=FMyGameplayTags::Get();
 if(auto* Old=Cast<UMyAbilitySystemComponent>(WeaponASC.Get()))
 {
  for(auto H:WeaponHandles) if(auto* S=Old->FindAbilitySpecFromHandle(H))
  {
   const auto Tag=Old->GetAbilityTagFromSpec(*S); const auto Slot=Old->GetInputTagFromSpec(*S);
   Old->CancelAbilityHandle(H); Old->ClearAbility(H);
   Old->ClientEquipAbility(Tag,Tags.Abilities_Status_Unlocked,FGameplayTag(),Slot);
  }
  for(const auto& Pair:DisplacedSlots) if(auto* S=Old->FindAbilitySpecFromHandle(Pair.Key);S && !Old->AbilityHasAnySlot(*S) && Old->SlotIsEmpty(Pair.Value))
  {
   Old->AssignSlotToAbility(*S,Pair.Value);S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Unlocked);S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped);
   Old->MarkAbilitySpecDirty(*S,true);Old->ClientEquipAbility(Old->GetAbilityTagFromSpec(*S),Tags.Abilities_Status_Equipped,Pair.Value,FGameplayTag());
  }
 }
 WeaponHandles=Prepared; DisplacedSlots.Reset(); WeaponASC=A; AppliedWeaponSet=Set; AppliedWeaponInstance=Instance;
 if(Set) for(int32 I=0;I<Prepared.Num();++I)
 {
  const auto Slot=Set->Abilities[I].InputTag;
  if(Slot.IsValid()) if(auto* Old=A->GetSpecWithSlot(Slot))
  {
   const auto H=Old->Handle; const auto Tag=A->GetAbilityTagFromSpec(*Old);
   DisplacedSlots.Add(H,Slot); A->CancelAbilityHandle(H);
   if(auto* S=A->FindAbilitySpecFromHandle(H))
   { A->ClearSlot(S);S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Equipped);S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Unlocked);A->MarkAbilitySpecDirty(*S,true); }
   A->ClientEquipAbility(Tag,Tags.Abilities_Status_Unlocked,FGameplayTag(),Slot);
  }
  if(auto* S=A->FindAbilitySpecFromHandle(Prepared[I]))
  {
   if(Slot.IsValid()) { A->AssignSlotToAbility(*S,Slot);S->DynamicAbilityTags.RemoveTag(Tags.Abilities_Status_Unlocked);S->DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped); }
   A->MarkAbilitySpecDirty(*S,true);A->ClientEquipAbility(A->GetAbilityTagFromSpec(*S),A->GetStatusFromSpec(*S),Slot,FGameplayTag());
  }
 }
 RefreshWeaponVisuals();
}
void UEquipmentComponent::RefreshWeaponVisuals()
{
 auto* State=Cast<AMyPlayerState>(GetOwner());
 auto* Player=State?Cast<AStanderPlayer>(State->GetPawn()):nullptr;
 if(!Player) if(auto* A=ASC()) Player=Cast<AStanderPlayer>(A->GetAvatarActor());
 if(Player) Player->ApplyWeaponSetVisuals(GetWeaponSet());
}

void UEquipmentComponent::RememberWeaponSlotDisplacement(FGameplayAbilitySpecHandle Incoming,const FGameplayAbilitySpec& Displaced)
{
 if(GetOwner()->HasAuthority() && IsWeaponAbility(Incoming) && !IsWeaponAbility(Displaced.Handle))
  DisplacedSlots.FindOrAdd(Displaced.Handle)=UMyAbilitySystemComponent::GetInputTagFromSpec(Displaced);
}
