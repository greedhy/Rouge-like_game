#include "Actor/MyOrbitingFireBolt.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Interface/CombatInterface.h"

AMyOrbitingFireBolt::AMyOrbitingFireBolt()
{
    PrimaryActorTick.bCanEverTick = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    SetMinNetUpdateFrequency(15.f);
    Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProjectileMovement->bAutoActivate = false;
}

void AMyOrbitingFireBolt::InitializeOrbit(AActor* Avatar, UOrbitingFireBoltConfig* Config, float PhaseDegrees)
{
    OrbitAvatar = Avatar;
    Settings = Config;
    OrbitPhase = PhaseDegrees;
    ReturnToOrbit();
}

void AMyOrbitingFireBolt::BeginPlay()
{
    Super::BeginPlay();
    SetLifeSpan(0.f);
    // Only the authority simulates; clients display replicated actor movement.
    ProjectileMovement->Deactivate();
    ProjectileMovement->SetComponentTickEnabled(false);
    Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetActorTickEnabled(HasAuthority());
}

void AMyOrbitingFireBolt::ReturnToOrbit()
{
    if (!HasAuthority() || bImpactStarted)
        return;
    bLaunched = false;
    AttackTarget.Reset();
    FlightSeconds = 0.f;
    ProjectileMovement->StopMovementImmediately();
    ProjectileMovement->HomingTargetComponent = nullptr;
    ProjectileMovement->bIsHomingProjectile = false;
    ProjectileMovement->Deactivate();
    ProjectileMovement->SetComponentTickEnabled(false);
    Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ForceNetUpdate();
}

void AMyOrbitingFireBolt::LaunchAt(AActor* Target)
{
    if (!HasAuthority() || bImpactStarted || !IsValid(Target) || !Settings)
        return;
    AttackTarget = Target;
    bLaunched = true;
    FlightSeconds = 0.f;
    ProjectileMovement->SetUpdatedComponent(Sphere);
    ProjectileMovement->InitialSpeed = Settings->ProjectileSpeed;
    ProjectileMovement->MaxSpeed = Settings->ProjectileSpeed;
    ProjectileMovement->HomingTargetComponent = Target->GetRootComponent();
    ProjectileMovement->bIsHomingProjectile = true;
    ProjectileMovement->HomingAccelerationMagnitude = Settings->ProjectileSpeed * 10.f;
    ProjectileMovement->Velocity =
        (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal() * Settings->ProjectileSpeed;
    ProjectileMovement->Activate(true);
    ProjectileMovement->SetComponentTickEnabled(true);
    Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ForceNetUpdate();
}

void AMyOrbitingFireBolt::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || bImpactStarted)
        return;
    AActor* Avatar = OrbitAvatar.Get();
    if (!IsValid(Avatar) || !Settings ||
        (Avatar->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Avatar)))
    {
        Destroy();
        return;
    }
    OrbitClock = GetWorld()->GetTimeSeconds();
    if (bLaunched)
    {
        AActor* Target = AttackTarget.Get();
        FlightSeconds += DeltaSeconds;
        if (!IsValid(Target) || !Target->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Target) ||
            FlightSeconds > Settings->MaxFlightSeconds ||
            FVector::DistSquared(Avatar->GetActorLocation(), Target->GetActorLocation()) >
                FMath::Square(Settings->MaxTargetDistance))
        {
            ReturnToOrbit();
        }
        else
        {
            // Keep a homing bolt from endlessly circling a very close/moving target.
            ProjectileMovement->Velocity =
                (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal() * Settings->ProjectileSpeed;
            SetActorRotation(ProjectileMovement->Velocity.Rotation());
            return;
        }
    }
    const float Angle = FMath::DegreesToRadians(OrbitPhase + OrbitClock * Settings->OrbitDegreesPerSecond);
    const FVector Desired =
        Avatar->GetActorLocation() + FVector(FMath::Cos(Angle) * Settings->OrbitRadius,
                                             FMath::Sin(Angle) * Settings->OrbitRadius, Settings->OrbitHeight);
    SetActorLocation(FMath::VInterpConstantTo(GetActorLocation(), Desired, DeltaSeconds, Settings->ProjectileSpeed),
                     false);
    SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
}

bool AMyOrbitingFireBolt::IsValidOverlap(AActor* OtherActor)
{
    return HasAuthority() && bLaunched && !bImpactStarted && OtherActor == AttackTarget.Get() && IsValid(OtherActor) &&
           OtherActor->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(OtherActor) &&
           Super::IsValidOverlap(OtherActor);
}

void AMyOrbitingFireBolt::OnHit()
{
    if (!HasAuthority() || bImpactStarted)
        return;
    bImpactStarted = true;
    MulticastImpact();
}

void AMyOrbitingFireBolt::MulticastImpact_Implementation()
{
    Super::OnHit();
}
