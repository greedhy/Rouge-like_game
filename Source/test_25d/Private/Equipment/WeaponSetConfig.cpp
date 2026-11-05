#include "Equipment/WeaponSetConfig.h"
#include "Animation/AnimInstance.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "Abilities/GameplayAbility.h"
bool UWeaponSetConfig::IsWeaponInputSlot(FGameplayTag Slot)
{
 static const TCHAR* Names[]={TEXT("InputTag.LMB"),TEXT("InputTag.RMB"),TEXT("InputTag.1"),TEXT("InputTag.2"),TEXT("InputTag.3"),TEXT("InputTag.4")};
 for(const auto* Name:Names) if(Slot==FGameplayTag::RequestGameplayTag(Name)) return true;
 return false;
}
bool UWeaponSetConfig::Validate() const
{
 if(Family.IsNone() || Abilities.IsEmpty() || !AnimClass || (MainMesh && MainSocket.IsNone()) || (OffhandMesh && OffhandSocket.IsNone())
  || MainTransform.ContainsNaN() || OffhandTransform.ContainsNaN()) return false;
 TSet<UClass*> Classes; TSet<FGameplayTag> Slots; TSet<FGameplayTag> Identities;
 for(const auto& Entry:Abilities)
 {
  if(!Entry.Ability || !Entry.Ability->IsChildOf(UMyGameplayAbility::StaticClass()) || Entry.Level<1 || Classes.Contains(Entry.Ability.Get())) return false;
  Classes.Add(Entry.Ability.Get());
  const auto* CDO=Entry.Ability.GetDefaultObject();
  bool HasIdentity=false;
  for(const auto& Tag:CDO->GetAssetTags()) if(Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities"))))
  { if(Identities.Contains(Tag)) return false; Identities.Add(Tag); HasIdentity=true; }
  if(!HasIdentity) return false;
  if(Entry.InputTag.IsValid())
  { if(!IsWeaponInputSlot(Entry.InputTag) || Slots.Contains(Entry.InputTag)) return false; Slots.Add(Entry.InputTag); }
 }
 return true;
}
