#include "PureMass/PureMassLegacyProjectile.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "Actor/MyProjectile.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/UnrealType.h"

namespace
{
    // The original projectile keeps these asset references private and exposes no impact API.
    // Read only its reflected properties here, so the legacy class and Blueprint stay unchanged.
    template <class T> T* ImpactAsset(AMyProjectile* Projectile, FName PropertyName)
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(Projectile->GetClass(), PropertyName);
        return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Projectile)) : nullptr;
    }
} // namespace

void UPureMassLegacyProjectile::OnRegister()
{
    Super::OnRegister();
    Projectile = Cast<AMyProjectile>(GetOwner());
    if (auto* P = Projectile.Get(); P && P->Sphere)
    {
        PreviousLocation = P->Sphere->GetComponentLocation();
        P->Sphere->TransformUpdated.AddUObject(this, &ThisClass::Moved);
    }
}

void UPureMassLegacyProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (auto* P = Projectile.Get(); P && P->Sphere)
    {
        PreviousLocation = P->Sphere->GetComponentLocation();
        // BP_FireBolt overrides the native overlap responses with Block. Restore the
        // native contract on this instance; leave the Blueprint's muzzle grace timer intact.
        P->Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
        P->Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
        P->Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
    }
}

void UPureMassLegacyProjectile::OnUnregister()
{
    if (auto* P = Projectile.Get(); P && P->Sphere)
        P->Sphere->TransformUpdated.RemoveAll(this);
    Super::OnUnregister();
}

void UPureMassLegacyProjectile::Moved(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport)
{
    const FVector Start = PreviousLocation, End = Component->GetComponentLocation();
    PreviousLocation = End;
    auto* P = Projectile.Get();
    if (bConsumed || !IsValid(P) || !P->HasAuthority() || !P->HasActorBegunPlay() || Teleport != ETeleportType::None ||
        !P->Sphere->IsQueryCollisionEnabled())
        return;
    auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>();
    const FDamageEffectParams Params = P->DamageEffectParams;
    auto* ASC = Params.SourceAbilitySystemComponent.Get();
    AActor* Source = IsValid(ASC) ? ASC->GetAvatarActor() : nullptr;
    if (!Mass || !Mass->IsEnabled() || !IsValid(Source) || Source->GetWorld() != GetWorld())
        return;

    // TransformUpdated fires before the movement's pending overlap notifications.
    // Resolve the same segment against world geometry, Actor capsules and Mass capsules
    // together, so a long frame cannot damage a Mass enemy behind a wall or Actor enemy.
    float Time = 1.f;
    FHitResult Wall;
    const auto Target = PureMassCombat::Sweep(GetWorld(), Source, Start, End, P->Sphere->GetScaledSphereRadius(), Time,
                                              Wall, nullptr, P);
    if (!Target.IsSet() || Target.IsActor())
        return; // Actor overlaps retain the original GAS pipeline.

    bConsumed = true; // Claim before any movement, damage/XP events or destruction can re-enter.
    P->Sphere->OnComponentBeginOverlap.RemoveDynamic(P, &AMyProjectile::OnSphereOverlap);
    P->Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    P->ProjectileMovement->StopMovementImmediately();
    const FVector Position = FMath::Lerp(Start, End, Time);
    P->SetActorLocation(Position, false);

    FPureMassDamage Damage;
    Damage.Source = Source;
    Damage.Amount = Params.BaseDamage;
    Damage.DamageType = Params.DamageType;
    if (Params.DebuffChance > 0.f && FMath::FRandRange(0.f, 100.f) < Params.DebuffChance)
    {
        Damage.StatusDuration = Params.DebuffDuration;
        Damage.DamagePerSecond = Params.DebuffDamage / FMath::Max(.1f, Params.DebuffFrequency);
    }
    Mass->ApplyDamage(Target.EntityId, Damage);
    // Authority's original Destroyed() doesn't play impact; remote clients already do.
    if (GetNetMode() != NM_DedicatedServer)
    {
        UGameplayStatics::PlaySoundAtLocation(P, ImpactAsset<USoundBase>(P, TEXT("ImpactSound")), Position);
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(P, ImpactAsset<UNiagaraSystem>(P, TEXT("ImpactEffect")),
                                                       Position);
    }
    P->Destroy();
}
