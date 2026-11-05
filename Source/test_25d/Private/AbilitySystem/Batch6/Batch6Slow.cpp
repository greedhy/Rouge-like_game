#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "PureMass/PureMassSubsystem.h"
UBatch6SlowEffect::UBatch6SlowEffect() { DurationPolicy=EGameplayEffectDurationType::Infinite; }
UBatch6SlowComponent::UBatch6SlowComponent() { PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=false; }
float UBatch6SlowComponent::GetMultiplier(const AActor* Actor)
{
 const auto* Enemy=Cast<AEnemyCharacter>(Actor);
 const auto* C=Actor?Actor->FindComponentByClass<UBatch6SlowComponent>():nullptr;
 if(!Enemy || !C || Enemy->GetDungeonPoolLease()!=C->Lease || Enemy->IsDungeonPoolSleeping() || ICombatInterface::Execute_IsDead(const_cast<AEnemyCharacter*>(Enemy))) return 1;
 float Result=1; for(const auto& E:C->Entries) if(E.Expires>C->GetWorld()->GetTimeSeconds()) Result=FMath::Min(Result,E.Multiplier);
 return Result;
}
bool UBatch6SlowComponent::Apply(UWorld* World,AActor* Caster,const FPureMassCombatTarget& Target,float Multiplier,float Duration)
{
 if(!World || !Caster || !Caster->HasAuthority() || !FMath::IsFinite(Multiplier) || !FMath::IsFinite(Duration) || Duration<=0 || Multiplier<=0 || Multiplier>1) return false;
 FVector P; if(!PureMassCombat::Resolve(World,Caster,Target,P)) return false;
 if(!Target.IsActor()) { auto* Mass=World->GetSubsystem<UPureMassSubsystem>(); return Mass && Mass->ApplySlow(Target.EntityId,Multiplier,Duration,Caster); }
 auto* Enemy=Cast<AEnemyCharacter>(Target.Actor.Get()); if(!Enemy) return false;
 auto* C=Enemy->FindComponentByClass<UBatch6SlowComponent>();
 if(!C) { C=NewObject<UBatch6SlowComponent>(Enemy); Enemy->AddInstanceComponent(C); C->RegisterComponent(); }
 if(C->Lease!=Enemy->GetDungeonPoolLease()) C->Clear();
 C->Lease=Enemy->GetDungeonPoolLease();
 C->Entries.RemoveAll([&](const FSlow& E){return E.Expires<=World->GetTimeSeconds();});
 // Bound repeated pulses without extending an old stronger slow with a weaker cast.
 const double End=World->GetTimeSeconds()+Duration;
 bool Merged=false; for(auto& E:C->Entries) if(FMath::IsNearlyEqual(E.Multiplier,Multiplier)) {E.Expires=FMath::Max(E.Expires,End); Merged=true; break;}
 if(!Merged && C->Entries.Num()<32) C->Entries.Add({Multiplier,End});
 if(!C->Marker.IsValid()) if(auto* ASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy)) C->Marker=ASC->ApplyGameplayEffectToSelf(GetDefault<UBatch6SlowEffect>(),1,ASC->MakeEffectContext());
 C->SetComponentTickEnabled(true);
 Enemy->RefreshMovementSpeed(); return true;
}
void UBatch6SlowComponent::Clear()
{ SetComponentTickEnabled(false); if(auto* ASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner())) ASC->RemoveActiveGameplayEffect(Marker); Marker.Invalidate(); Entries.Reset(); if(auto* Enemy=Cast<AEnemyCharacter>(GetOwner())) Enemy->RefreshMovementSpeed(); }
void UBatch6SlowComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Type,Function); auto* Enemy=Cast<AEnemyCharacter>(GetOwner());
 if(!Enemy || !Enemy->HasAuthority()) return;
 if(Enemy->GetDungeonPoolLease()!=Lease || Enemy->IsDungeonPoolSleeping() || ICombatInterface::Execute_IsDead(Enemy)) { Clear(); return; }
 const int32 Removed=Entries.RemoveAll([&](const FSlow& E){return E.Expires<=GetWorld()->GetTimeSeconds();});
 if(Entries.IsEmpty()) Clear(); else if(Removed) Enemy->RefreshMovementSpeed();
}
void UBatch6SlowComponent::EndPlay(const EEndPlayReason::Type Reason) { Clear(); Super::EndPlay(Reason); }
