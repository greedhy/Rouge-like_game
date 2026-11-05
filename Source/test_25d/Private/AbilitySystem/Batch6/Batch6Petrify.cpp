#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "PureMass/PureMassAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Interface/CombatInterface.h"
#include "MyGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
UBatch6PetrifyEffect::UBatch6PetrifyEffect()
{
 DurationPolicy = EGameplayEffectDurationType::Infinite;
}
UBatch6PetrifyComponent::UBatch6PetrifyComponent()
{
 SetIsReplicatedByDefault(true);
 PrimaryComponentTick.bCanEverTick = true;
 PrimaryComponentTick.bStartWithTickEnabled = false;
}
bool UBatch6PetrifyComponent::Apply(float Duration, UMaterialInterface* Material)
{
 AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
 UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy);
 if (!Enemy || !Enemy->HasAuthority() || !ASC || Duration <= 0 || !FMath::IsFinite(Duration) || Enemy->IsDungeonPoolSleeping() || ICombatInterface::Execute_IsDead(Enemy)) return false;
 if (bPetrified && !IsLeaseValid(Lease, Enemy->GetDungeonPoolLease())) Clear();
 if (!bPetrified)
 {
  FGameplayEffectSpec Spec(GetDefault<UBatch6PetrifyEffect>(), ASC->MakeEffectContext(), 1.f);
  Spec.DynamicGrantedTags.AddTag(FMyGameplayTags::Get().Debuff_Stun);
  ControlHandle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
  if (!ControlHandle.IsValid()) return false;
  ASC->OnAnyGameplayEffectRemovedDelegate().RemoveAll(this);
  ASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this, &UBatch6PetrifyComponent::OnControlRemoved);
  ASC->CancelAllAbilities();
  Enemy->GetCharacterMovement()->StopMovementImmediately();
 }
 Lease = Enemy->GetDungeonPoolLease();
 Expires = FMath::Max(Expires, GetWorld()->GetTimeSeconds() + Duration);
 StoneMaterial = Material;
 bPetrified = true;
 OnRep_State();
 Enemy->ForceNetUpdate();
 return true;
}
void UBatch6PetrifyComponent::OnRep_State()
{
 AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
 if (!Enemy || !Enemy->GetMesh()) return;
 auto* Mesh = Enemy->GetMesh();
 SetComponentTickEnabled(bPetrified);
 const bool bValidLife = IsLeaseValid(Lease, Enemy->GetDungeonPoolLease()) && !ICombatInterface::Execute_IsDead(Enemy);
 if (bPetrified && bValidLife && !bVisualApplied)
 {
  bWasPaused = Mesh->bPauseAnims;
  OriginalMaterials.Reset();
  for (int32 I=0; I<Mesh->GetNumMaterials(); ++I) OriginalMaterials.Add(Mesh->GetMaterial(I));
  Mesh->bPauseAnims = true;
  bVisualApplied = true;
 }
 if (bPetrified && bValidLife && StoneMaterial)
 {
  for (int32 I=0; I<Mesh->GetNumMaterials(); ++I) Mesh->SetMaterial(I, StoneMaterial);
  AppliedMaterial = StoneMaterial;
 }
 if ((!bPetrified || !bValidLife) && bVisualApplied)
 {
  for (int32 I=0; I<OriginalMaterials.Num(); ++I)
   if (AppliedMaterial && Mesh->GetMaterial(I) == AppliedMaterial) Mesh->SetMaterial(I, OriginalMaterials[I]);
  if (Enemy->GetDungeonPoolLease() == Lease || Enemy->GetDungeonPoolLease() < 0) Mesh->bPauseAnims = bWasPaused;
  OriginalMaterials.Reset();
  AppliedMaterial = nullptr;
  bVisualApplied = false;
 }
}
void UBatch6PetrifyComponent::OnControlRemoved(const FActiveGameplayEffect& Effect)
{
 if (Effect.Handle != ControlHandle) return;
 ControlHandle.Invalidate();
 Clear();
}
void UBatch6PetrifyComponent::Clear()
{
 const auto RemovingHandle = ControlHandle;
 ControlHandle.Invalidate();
 if (GetOwner() && GetOwner()->HasAuthority())
  if (auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner())) { ASC->OnAnyGameplayEffectRemovedDelegate().RemoveAll(this); if (RemovingHandle.IsValid()) ASC->RemoveActiveGameplayEffect(RemovingHandle); }
 ControlHandle.Invalidate();
 Expires = 0;
 bPetrified = false;
 OnRep_State();
 if (GetOwner()) GetOwner()->ForceNetUpdate();
}
void UBatch6PetrifyComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick)
{
 Super::TickComponent(Delta, Type, Tick);
 auto* Enemy = Cast<AEnemyCharacter>(GetOwner());
 if (bPetrified && Enemy && (ICombatInterface::Execute_IsDead(Enemy) || !IsLeaseValid(Lease, Enemy->GetDungeonPoolLease()) || (Enemy->HasAuthority() && GetWorld()->GetTimeSeconds() >= Expires)))
 {
  if (Enemy->HasAuthority()) Clear();
  else OnRep_State();
 }
}
void UBatch6PetrifyComponent::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }
void UBatch6PetrifyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME(UBatch6PetrifyComponent, StoneMaterial);
 DOREPLIFETIME(UBatch6PetrifyComponent, Lease);
 DOREPLIFETIME(UBatch6PetrifyComponent, bPetrified);
}
UBatch6PetrifyAbility::UBatch6PetrifyAbility() { NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly; InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; }
void UBatch6PetrifyAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const
{
 if (Info && Info->IsNetAuthority() && Prototype) UPureMassAbility::SpendMana(Info->AvatarActor.Get(),Prototype->ManaCost);
}
void UBatch6PetrifyAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Data)
{
 AActor* Avatar = Info ? Info->AvatarActor.Get() : nullptr;
 if (!Avatar || !Avatar->HasAuthority() || !CommitAbility(Handle, Info, Activation)) { EndAbility(Handle, Info, Activation, true, true); return; }
 for (const auto& Target : PureMassCombat::Gather(GetWorld(), Avatar, Avatar->GetActorLocation(), Radius))
 {
  if (!Target.IsActor()) { if (PureMassCombat::Visible(GetWorld(), Avatar, Avatar->GetActorLocation(), Target)) if (auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>()) Mass->ApplyPetrify(Target.EntityId, Duration, Avatar); continue; }
  auto* Enemy = Cast<AEnemyCharacter>(Target.Actor.Get());
  if (!Enemy || !PureMassCombat::Visible(GetWorld(), Avatar, Avatar->GetActorLocation(), Target)) continue;
  auto* Component = Enemy->FindComponentByClass<UBatch6PetrifyComponent>();
  if (!Component) { Component = NewObject<UBatch6PetrifyComponent>(Enemy); Enemy->AddInstanceComponent(Component); Component->RegisterComponent(); }
  Component->Apply(Duration, StoneMaterial);
 }
 EndAbility(Handle, Info, Activation, true, false);
}









FString UBatch6PetrifyAbility::GetDescription(int32 Level)
{
 return FString::Printf(TEXT("<Title>%s</> <Level>%d</>\n<Default>Petrifies visible enemies in radius %.0f for %.1fs. Stops movement and attacks.</>\n<Default>Mana %.0f | Cooldown %.1fs</>"), Prototype ? *Prototype->DisplayName.ToString() : TEXT("Petrify"), Level, Radius, Duration, Prototype ? Prototype->ManaCost : 0.f, Prototype ? Prototype->Cooldown : 0.f);
}



