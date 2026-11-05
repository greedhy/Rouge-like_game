#include "Actor/EnemySlingProjectile.h"
#include "Character/EnemyCharacter.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Engine/World.h"
AEnemySlingProjectile::AEnemySlingProjectile(){PrimaryActorTick.bCanEverTick=true;}
void AEnemySlingProjectile::BeginPlay()
{
 Super::BeginPlay();
 // BeginPlay and collision updates can synchronously invoke gameplay callbacks.
 // A muzzle overlap may already have hit and destroyed this projectile.
 if(!IsValid(this) || IsActorBeingDestroyed()) return;
 // Production walls need not generate overlaps. Let swept movement block them.
 Sphere->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
 if(!IsValid(this) || IsActorBeingDestroyed()) return;
 ProjectileMovement->OnProjectileStop.AddDynamic(this,&ThisClass::WorldStopped);
 ProjectileMovement->bTickBeforeOwner=false;
 RemoveTickPrerequisiteComponent(ProjectileMovement);
 ProjectileMovement->AddTickPrerequisiteActor(this);
}
void AEnemySlingProjectile::InitializeSource(AEnemyCharacter* Source)
{
 if(FiringEnemy.IsValid()) FiringEnemy->GetOnDeathDelegate().RemoveDynamic(this,&ThisClass::SourceDied);
 FiringEnemy=Source;FiringLease=Source?Source->GetDungeonPoolLease():0;
 if(Source) Source->GetOnDeathDelegate().AddUniqueDynamic(this,&ThisClass::SourceDied);
}
bool AEnemySlingProjectile::IsSourceCurrent() const
{
 return FiringEnemy.IsValid() && !FiringEnemy->IsActorBeingDestroyed() && !FiringEnemy->IsDead_Implementation() &&
  !FiringEnemy->IsDungeonPoolSleeping() && FiringEnemy->GetDungeonPoolLease()==FiringLease;
}
bool AEnemySlingProjectile::IsValidOverlap(AActor* OtherActor)
{
 if(!HasAuthority() || !IsSourceCurrent() || !IsValid(OtherActor) || !Super::IsValidOverlap(OtherActor)) return false;
 if(OtherActor->Implements<UCombatInterface>()) return !ICombatInterface::Execute_IsDead(OtherActor) && ADungeonRunDirector::AllowsCombat(FiringEnemy.Get(),OtherActor);
 return true; // Geometry terminates the rock through the existing projectile collision path.
}
void AEnemySlingProjectile::Tick(float Dt)
{
 Super::Tick(Dt);if(!HasAuthority()) return;
 if(!IsSourceCurrent()){Destroy();return;}
 // Default level geometry ignores the custom Projectile channel. Object sweeps
 // enforce the promised wall boundary without changing project collision profiles.
 FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySlingWall),false,this);Params.AddIgnoredActor(FiringEnemy.Get());
 FHitResult Hit;
 if(GetWorld()->SweepSingleByObjectType(Hit,GetActorLocation(),GetActorLocation()+ProjectileMovement->Velocity*Dt,
     FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(Sphere->GetScaledSphereRadius()),Params))
 {
  // Sweep normally up to the wall first, so a closer player receives its overlap
  // before the terminal wall impact, even on a long frame.
  FHitResult MovementHit;
  Sphere->MoveComponent(Hit.Location-GetActorLocation(),GetActorQuat(),true,&MovementHit);
  if(!IsActorBeingDestroyed()) WorldStopped(Hit);
 }
}
void AEnemySlingProjectile::SourceDied(AActor*){if(HasAuthority()) Destroy();}
void AEnemySlingProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
 if(FiringEnemy.IsValid()) FiringEnemy->GetOnDeathDelegate().RemoveDynamic(this,&ThisClass::SourceDied);
 Super::EndPlay(Reason);
}
void AEnemySlingProjectile::OnHit(){if(HasAuthority() && !bImpactSent){bImpactSent=true;PlayTerminalImpact();}}
void AEnemySlingProjectile::WorldStopped(const FHitResult&){if(HasAuthority()){if(IsSourceCurrent()) OnHit();Destroy();}}
void AEnemySlingProjectile::PlayTerminalImpact_Implementation(){Super::OnHit();}
