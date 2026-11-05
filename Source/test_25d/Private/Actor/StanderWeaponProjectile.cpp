#include "Actor/StanderWeaponProjectile.h"
#include "Character/StanderPlayer.h"
#include "Player/SwordShieldConfig.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/PlayerState.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonGenerationRuntime.h"
AStanderWeaponProjectile::AStanderWeaponProjectile()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=true;SetReplicateMovement(true);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 FlightVisual=CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlightVisual"));
 FlightVisual->SetupAttachment(RootComponent);FlightVisual->SetAutoActivate(false);
}
void AStanderWeaponProjectile::OnRep_FlightEffect()
{
 if(GetNetMode()==NM_DedicatedServer) return;
 if(FlightVisual->GetAsset()!=FlightEffect) { FlightVisual->SetAsset(FlightEffect);if(FlightEffect) FlightVisual->Activate(true);else FlightVisual->Deactivate(); }
}
void AStanderWeaponProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AStanderWeaponProjectile,FlightEffect); }
void AStanderWeaponProjectile::Initialize(AStanderPlayer* Source,const FStanderWeaponActionEvent& Event,const FDamageEffectParams& Params,FVector Direction)
{
 if(!HasAuthority() || !IsValid(Source) || Direction.ContainsNaN() || Direction.IsNearlyZero()) { Destroy();return; }
 auto* PS=Source->GetPlayerState();auto* Equipment=PS?PS->FindComponentByClass<UEquipmentComponent>():nullptr;
 if(!Equipment || !FMath::IsFinite(Event.Radius) || Event.Radius<0 || !FMath::IsFinite(Event.ProjectileSpeed) || Event.ProjectileSpeed<=0 || !FMath::IsFinite(Event.ProjectileLifetime) || Event.ProjectileLifetime<=0) { Destroy();return; }
 Caster=Source;Action=Event;DamageParams=Params;Forward=Direction.GetSafeNormal();SetActorRotation(Forward.Rotation());SetLifeSpan(Event.ProjectileLifetime);
 WeaponGeneration=Equipment->GetWeaponGeneration();
 OriginDirector=ADungeonRunDirector::Find(GetWorld());bHasDungeon=OriginDirector.IsValid();
 if(auto* Director=OriginDirector.Get())
 {
  OriginRoom=Director->GetRoomIndex(Source->GetActorLocation());RunId=Director->RunId;
  if(auto* Generation=Director->GetGeneration()) DungeonGeneration=Generation->Manifest.GenerationId;
  if(OriginRoom==INDEX_NONE || !AllowsLocation(GetActorLocation())) { Destroy();return; }
 }
 FlightEffect=Event.Effect;
 // A looping flight system belongs to the projectile component. Never spawn it
 // as an unowned impact effect that could survive the projectile indefinitely.
 Action.Effect=Source->SwordConfig?Source->SwordConfig->HitEffect.Get():nullptr;
 OnRep_FlightEffect();ForceNetUpdate();
}
bool AStanderWeaponProjectile::AllowsLocation(const FVector& Location) const
{
 if(Location.ContainsNaN()) return false;
 if(!bHasDungeon) return ADungeonRunDirector::Find(GetWorld())==nullptr;
 const auto* Director=OriginDirector.Get();
 const auto* Generation=Director?Director->GetGeneration():nullptr;
 return Director && Director==ADungeonRunDirector::Find(GetWorld()) && Director->RunId==RunId && Generation
  && Generation->Manifest.GenerationId==DungeonGeneration && Director->GetRoomIndex(Location)==OriginRoom;
}
void AStanderWeaponProjectile::Tick(float Delta)
{
 Super::Tick(Delta);if(!HasAuthority()) return;
 if(!Caster.IsValid() || Caster->IsDungeonDowned() || Caster->IsDead_Implementation() || !AllowsLocation(Caster->GetActorLocation())) { Destroy();return; }
 auto* PS=Caster->GetPlayerState();auto* Equipment=PS?PS->FindComponentByClass<UEquipmentComponent>():nullptr;
 if(!Equipment || Equipment->GetWeaponGeneration()!=WeaponGeneration || !FMath::IsFinite(Delta) || Delta<0) { Destroy();return; }
 const FVector Start=GetActorLocation();FVector End=Start+Forward*Action.ProjectileSpeed*Delta;
 if(!AllowsLocation(Start) || End.ContainsNaN()) { Destroy();return; }
 const bool ReachesBoundary=!AllowsLocation(End);
 if(ReachesBoundary)
 {
  // Keep valid hits before the boundary even on a long frame, without querying the next room.
  FVector Inside=Start,Outside=End;
  for(int32 I=0;I<16;++I) { const FVector Mid=(Inside+Outside)*.5; if(AllowsLocation(Mid)) Inside=Mid;else Outside=Mid; }
  End=Inside;
 }
 float Time=1;FHitResult Wall;
 const auto Target=PureMassCombat::Sweep(GetWorld(),Caster.Get(),Start,End,Action.Radius,Time,Wall,nullptr,this);
 if(Target.IsSet())
 {
  FVector TargetLocation;
  if(PureMassCombat::Resolve(GetWorld(),Caster.Get(),Target,TargetLocation) && AllowsLocation(TargetLocation))
  {
   if(Target.IsActor())
   {
    DamageParams.TargetAbilitySystemComponent=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Actor.Get());
    if(DamageParams.TargetAbilitySystemComponent) UMyAbilitySystemLibrary::ApplyDamageEffect(DamageParams);
   }
   else if(auto* Mass=GetWorld()->GetSubsystem<UPureMassSubsystem>())
   { FPureMassDamage Damage;Damage.Source=Caster.Get();Damage.Amount=DamageParams.BaseDamage;Damage.DamageType=DamageParams.DamageType;Damage.DeathImpulse=DamageParams.DeathImpulse;Mass->ApplyDamage(Target.EntityId,Damage); }
   if(Caster.IsValid()) Caster->ShowSwordImpact(Action.Effect,FMath::Lerp(Start,End,Time));
  }
  Destroy();return;
 }
 if(Wall.bBlockingHit) { if(Caster.IsValid()) Caster->ShowSwordImpact(Action.Effect,Wall.ImpactPoint);Destroy();return; }
 if(ReachesBoundary) { Destroy();return; }
 SetActorLocation(End);
}
