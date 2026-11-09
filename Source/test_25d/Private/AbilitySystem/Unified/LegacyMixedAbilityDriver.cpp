/** Extracted authority behavior. Host BeginPlay or graph Start -> Start; graph/host Tick -> Advance. */
#include "AbilitySystem/Unified/LegacyMixedAbilityDriver.h"
#include "PureMass/PureMassRuntime.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain/TerrainSurface.h"
#include "Interface/CombatInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

FMixedAbilityDriver::FMixedAbilityDriver(APureMassRuntime* Host) : Presentation(Host)
{
}

bool FMixedAbilityDriver::IsFinished() const
{
    const auto* Host = Presentation.Get();
    return !IsValid(Host) || Host->bFinished;
}

bool FMixedAbilityDriver::Visible(FVector Origin, const FPureMassCombatTarget& Candidate) const
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    return PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), Origin, Candidate, Host);
}

bool FMixedAbilityDriver::Hit(FPureMassCombatTarget Candidate, float Scale)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    if (!Host->HasAuthority() || !Host->Context || !Host->Context->ValidateSourceLife() || Host->bFinished ||
        !Host->ExecutionPrototype)
        return false;
    AActor* Caster = Host->Context->Caster.Get();
    if (!IsValid(Caster) || (Caster->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Caster)))
        return false;
    FVector Position;
    if (!Host->Context->ResolveTargetLife(Candidate, Position))
        return false;
    if (Candidate.IsActor())
    {
        FDamageEffectParams Params = Host->Context->DamageParams;
        Params.WorldContextObject = Host;
        Params.TargetAbilitySystemComponent =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate.Actor.Get());
        if (!IsValid(Params.SourceAbilitySystemComponent))
            Params.SourceAbilitySystemComponent =
                UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Host->Context->Caster.Get());
        if (!IsValid(Params.SourceAbilitySystemComponent) || !IsValid(Params.TargetAbilitySystemComponent) ||
            !Params.DamageGameplayEffectClass)
            return false;
        if (Params.SourceAbilitySystemComponent->GetAvatarActor() != Host->Context->Caster.Get() ||
            Params.SourceAbilitySystemComponent->GetWorld() != Host->GetWorld())
            return false;
        Params.BaseDamage = Host->ExecutionPrototype->DamageAtLevel(Host->Context->Level) * Scale;
        Params.DamageType = Host->ExecutionPrototype->DamageType;
        Params.bIsRadialDamage = false;
        UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    }
    else
    {
        auto* Mass = Host->GetWorld()->GetSubsystem<UPureMassSubsystem>();
        if (!Mass)
            return false;
        FPureMassDamage Damage;
        Damage.Source = Host->Context->Caster;
        Damage.Amount = Host->ExecutionPrototype->DamageAtLevel(Host->Context->Level) * Scale;
        Damage.DamageType = Host->ExecutionPrototype->DamageType;
        const auto& Params = Host->Context->DamageParams;
        if (Params.DebuffChance > 0.f && FMath::FRandRange(0.f, 100.f) < Params.DebuffChance)
        {
            Damage.StatusDuration = Params.DebuffDuration;
            Damage.DamagePerSecond = Params.DebuffDamage / FMath::Max(.1f, Params.DebuffFrequency);
        }
        Mass->ApplyDamage(Candidate.EntityId, Damage);
    }
    // GE may synchronously cancel the cast or recycle its target. Secondary state must use the original lease.
    if (!IsValid(Host) || Host->bFinished || !Host->Context->ValidateSourceLife())
    {
        return true;
    }
    FVector CurrentLocation;
    if (const auto* Field = Cast<UBatch6FieldPrototype>(Host->ExecutionPrototype);
        Field && Field->SlowMultiplier < 1.f && Host->Context->ResolveTargetLife(Candidate, CurrentLocation))
    {
        UBatch6SlowComponent::Apply(Host->GetWorld(), Caster, Candidate, Field->SlowMultiplier, Field->SlowDuration);
    }
    if (!IsValid(Host) || Host->bFinished || !Host->Context->ValidateSourceLife())
    {
        return true;
    }
    Host->Impact(Position);
    Emit(TEXT("OnHit"), Host->ExecutionPrototype->OnHit, Position, Candidate);
    return true;
}

bool FMixedAbilityDriver::Emit(FName Event, UAbilityPrototype* Child, FVector Position, FPureMassCombatTarget Candidate)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    return Host->HasAuthority() && Host->Context && !Host->Context->bCancelled &&
           RequestChild(Event, Child, Position, Direction, Candidate, Depth + 1, false);
}

FPureMassCombatTarget FMixedAbilityDriver::Nearest(FVector Origin, float Radius) const
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return {};
    }

    float Best = FMath::Square(Radius);
    FPureMassCombatTarget Result;
    for (const auto& Candidate : PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), Origin, Radius))
    {
        FVector Position;
        if (HitTargets.Contains(Candidate) || !Host->Context->ResolveTargetLife(Candidate, Position))
            continue;
        const float Dist = FVector::DistSquared(Origin, Position);
        if (Dist <= Best && Visible(Origin, Candidate))
        {
            Result = Candidate;
            Best = Dist;
        }
    }
    return Result;
}

void FMixedAbilityDriver::Pulse()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    using K = EAbilityPrototypeKind;
    const auto Kind = Host->ExecutionPrototype->Kind;
    const FVector Facing = Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
    const float Bounds = Kind == K::Line   ? FMath::Sqrt(FMath::Square(Host->ExecutionPrototype->Range) +
                                                         FMath::Square(Host->ExecutionPrototype->Width * .5f) +
                                                         FMath::Square(Host->ExecutionPrototype->ShapeHalfHeight))
                         : Kind == K::Ring ? FMath::Sqrt(FMath::Square(Host->ExecutionPrototype->Radius) +
                                                         FMath::Square(Host->ExecutionPrototype->ShapeHalfHeight))
                                           : Host->ExecutionPrototype->Radius;
    for (const auto& Candidate :
         PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), Host->GetActorLocation(), Bounds))
    {
        FVector Position;
        if (!Host->Context->ResolveTargetLife(Candidate, Position))
            continue;
        const FVector Offset = Position - Host->GetActorLocation();
        if (Kind == K::Cone && !Offset.IsNearlyZero() &&
            FVector::DotProduct(Facing, Offset.GetSafeNormal2D()) <
                FMath::Cos(FMath::DegreesToRadians(Host->ExecutionPrototype->SpreadDegrees * .5f)))
            continue;
        if ((Kind == K::Line || Kind == K::Ring) && FMath::Abs(Offset.Z) > Host->ExecutionPrototype->ShapeHalfHeight)
            continue;
        if (Kind == K::Ring && (Offset.SizeSquared2D() < FMath::Square(Host->ExecutionPrototype->InnerRadius) ||
                                Offset.SizeSquared2D() > FMath::Square(Host->ExecutionPrototype->Radius)))
            continue;
        if (Kind == K::Line)
        {
            const float Along = FVector::DotProduct(Facing, Offset);
            if (Along < 0.f || Along > Host->ExecutionPrototype->Range ||
                FMath::Abs(FVector::DotProduct(Right, Offset)) > Host->ExecutionPrototype->Width * .5f)
                continue;
        }
        if (Kind == K::Wave &&
            (HitTargets.Contains(Candidate) || Offset.SizeSquared() > FMath::Square(Host->WaveRadius)))
            continue;
        if (const auto* Config = Cast<UBatch6FieldPrototype>(Host->ExecutionPrototype);
            Config && Config->bWaveFrontOnly && Kind == K::Wave &&
            !UBatch6FieldPrototype::IsInWaveFront(Offset.Size(), PreviousWaveRadius, Host->WaveRadius))
            continue;
        if (!Visible(Host->GetActorLocation(), Candidate))
            continue;
        Hit(Candidate);
        if (Kind == K::Wave)
            HitTargets.Add(Candidate);
    }
}

void FMixedAbilityDriver::Projectile(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    using K = EAbilityPrototypeKind;
    const bool bReturn = Host->ExecutionPrototype->Kind == K::Returning;
    float Remaining = FMath::Max(0.f, Host->ExecutionPrototype->Range - Travel);
    if (bReturn && bReturning)
    {
        Remaining = FVector::Distance(Host->GetActorLocation(), Host->GetOwner()->GetActorLocation());
        if (Remaining <= Host->ExecutionPrototype->CollisionRadius)
        {
            Finish(false);
            return;
        }
        Direction = (Host->GetOwner()->GetActorLocation() - Host->GetActorLocation()).GetSafeNormal();
    }
    if (Host->ExecutionPrototype->Kind == K::Homing)
    {
        FVector Position;
        if (!Host->Context->ResolveTargetLife(Target, Position))
        {
            Finish(false);
            return;
        }
        Direction = FMath::VInterpNormalRotationTo(Direction, (Position - Host->GetActorLocation()).GetSafeNormal(),
                                                   Delta, Host->ExecutionPrototype->HomingTurnRate);
    }
    const float Step = FMath::Min(Host->ExecutionPrototype->Speed * Delta, Remaining);
    const FVector Start = Host->GetActorLocation(), End = Start + Direction * Step;
    FHitResult Wall;
    // Re-query both representations with the growing exclusion set; walls still win ties.
    for (int32 I = 0; I < 64; ++I)
    {
        float Time = 1.f;
        const auto Candidate =
            PureMassCombat::Sweep(Host->GetWorld(), Host->GetOwner(), Start, End,
                                  Host->ExecutionPrototype->CollisionRadius, Time, Wall, &HitTargets, Host);
        if (!Candidate.IsSet())
            break;
        if (bReturn && HitTargets.Num() >= Host->ExecutionPrototype->Count)
            break;
        Host->SetActorLocation(FMath::Lerp(Start, End, Time));
        Hit(Candidate, bReturn ? (bReturning ? Host->ExecutionPrototype->DamageFalloff : 1.f)
                               : FMath::Pow(Host->ExecutionPrototype->DamageFalloff, HitTargets.Num()));
        HitTargets.Add(Candidate);
        if (!bReturn &&
            (Host->ExecutionPrototype->Kind != K::Piercing || HitTargets.Num() >= Host->ExecutionPrototype->Count))
        {
            Finish(false);
            return;
        }
    }
    if (Wall.bBlockingHit)
    {
        Host->SetActorLocation(Wall.Location);
        Host->Impact(Wall.ImpactPoint);
        Finish(false);
        return;
    }
    Host->SetActorLocation(End);
    Host->SetActorRotation(Direction.Rotation());
    Travel += Step;
    if (bReturn)
    {
        if (bReturning && Step >= Remaining)
        {
            Finish(false);
            return;
        }
        if (!bReturning && Travel >= Host->ExecutionPrototype->Range)
        {
            bReturning = true;
            HitTargets.Reset();
            const float Remainder = Delta - Step / Host->ExecutionPrototype->Speed;
            if (Remainder > SMALL_NUMBER)
                Projectile(Remainder);
        }
    }
    else if (Travel >= Host->ExecutionPrototype->Range)
        Finish(false);
}

void FMixedAbilityDriver::Chain()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    FVector Position;
    const float Reach = HitTargets.IsEmpty() ? Host->ExecutionPrototype->Range : Host->ExecutionPrototype->Radius;
    if (HitTargets.Num() >= Host->ExecutionPrototype->Count || HitTargets.Contains(Target) ||
        !Host->Context->ResolveTargetLife(Target, Position) ||
        FVector::DistSquared(ChainOrigin, Position) > FMath::Square(Reach) || !Visible(ChainOrigin, Target))
    {
        Finish(false);
        return;
    }
    Host->SetActorLocation(ChainOrigin);
    Host->BeamEnd = Position;
    Hit(Target, FMath::Pow(Host->ExecutionPrototype->DamageFalloff, HitTargets.Num()));
    HitTargets.Add(Target);
    ChainOrigin = Position;
    Target = Nearest(Position, Host->ExecutionPrototype->Radius);
    Host->ForceNetUpdate();
    if (!Target.IsSet() || HitTargets.Num() >= Host->ExecutionPrototype->Count)
        Finish(false);
}

void FMixedAbilityDriver::Orbit(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    const auto* Settings = Host->ExecutionPrototype->OrbitSettings.Get();
    if (!Settings)
    {
        Finish(true);
        return;
    }
    Host->SetActorLocation(Host->GetOwner()->GetActorLocation());
    PeriodClock += Delta;
    LaunchClock += Delta;
    if (PeriodClock >= Settings->SpawnInterval)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Settings->SpawnInterval);
        if (Host->OrbitBolts.Num() < Settings->MaxFireBolts)
        {
            const int32 Index = Host->OrbitBolts.AddDefaulted();
            const float Angle = Age * Settings->OrbitDegreesPerSecond + 360.f * Index / Host->OrbitBolts.Num();
            // Launch can happen this same tick. Never expose the default world-origin position to its sweep.
            Host->OrbitBolts[Index].Position =
                Host->GetActorLocation() +
                FVector(Settings->OrbitRadius, 0, Settings->OrbitHeight).RotateAngleAxis(Angle, FVector::UpVector);
        }
        Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Host->GetActorLocation(), Target);
    }
    FVector TargetPosition;
    const bool bTarget =
        Host->Context->ResolveTargetLife(Target, TargetPosition) &&
        FVector::DistSquared(Host->GetActorLocation(), TargetPosition) <= FMath::Square(Settings->MaxTargetDistance);
    if (!bTarget)
        Target = {};
    if (bTarget && LaunchClock >= Settings->LaunchInterval && Visible(Host->GetActorLocation(), Target))
    {
        for (auto& Bolt : Host->OrbitBolts)
            if (!Bolt.bLaunched)
            {
                Bolt.bLaunched = true;
                Bolt.Age = 0.f;
                LaunchClock = 0.f;
                break;
            }
    }
    for (int32 I = Host->OrbitBolts.Num() - 1; I >= 0; --I)
    {
        auto& Bolt = Host->OrbitBolts[I];
        if (!bTarget)
        {
            Bolt.bLaunched = false;
            Bolt.Age = 0.f;
        }
        if (!Bolt.bLaunched)
        {
            const float Angle =
                Age * Settings->OrbitDegreesPerSecond + 360.f * I / FMath::Max(1, Host->OrbitBolts.Num());
            Bolt.Position =
                Host->GetActorLocation() +
                FVector(Settings->OrbitRadius, 0, Settings->OrbitHeight).RotateAngleAxis(Angle, FVector::UpVector);
            continue;
        }
        Bolt.Age += Delta;
        const FVector Start = Bolt.Position;
        const FVector End = FMath::VInterpConstantTo(Start, TargetPosition, Delta, Settings->ProjectileSpeed);
        float Time = 1.f;
        FHitResult Wall;
        const auto Candidate =
            PureMassCombat::Sweep(Host->GetWorld(), Host->GetOwner(), Start, End,
                                  Host->ExecutionPrototype->CollisionRadius, Time, Wall, nullptr, Host);
        if (Candidate.IsSet())
        {
            Hit(Candidate);
            Host->OrbitBolts.RemoveAt(I);
            continue;
        }
        if (Wall.bBlockingHit || Bolt.Age >= Settings->MaxFlightSeconds)
        {
            Host->OrbitBolts.RemoveAt(I);
            continue;
        }
        Bolt.Position = End;
    }
}

void FMixedAbilityDriver::Finish(bool bCancelled)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (Host->bFinished)
        return;
    Host->bFinished = true;
    if (bCancelled)
    {
        if (Host->Context)
            Host->Context->bCancelled = true;
    }
    else
        Emit(TEXT("OnExpire"), Host->ExecutionPrototype->OnExpire,
             Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Chain ? ChainOrigin : Host->GetActorLocation(),
             Target);
    if (IsValid(Host->Surface))
        Host->Surface->Destroy();
    Host->Surface = nullptr;
    Host->OrbitBolts.Reset();
    Host->RefreshVisual();
    Host->ForceNetUpdate();
    Host->SetLifeSpan(.35f);
}

void FMixedAbilityDriver::Start()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || !Host->Context || !Host->ExecutionPrototype)
        return;
    if (bStarted || Host->bFinished)
    {
        return;
    }
    bStarted = true;
    if (!Host->Context->ValidateSourceLife())
    {
        Finish(true);
        return;
    }
    using K = EAbilityPrototypeKind;
    switch (Host->ExecutionPrototype->Kind)
    {
    case K::Multishot:
        if (!Host->bSingle)
        {
            for (int32 I = 0; I < Host->ExecutionPrototype->Count; ++I)
            {
                const float Angle =
                    Host->ExecutionPrototype->Count == 1
                        ? 0.f
                        : -Host->ExecutionPrototype->SpreadDegrees * .5f +
                              Host->ExecutionPrototype->SpreadDegrees * I / (Host->ExecutionPrototype->Count - 1);
                RequestChild(TEXT("Multishot"), Host->ExecutionPrototype, Host->GetActorLocation(),
                             Direction.RotateAngleAxis(Angle, FVector::UpVector), {}, Depth, true);
            }
            // Each pellet owns its hit/expiry hooks. The fan container has no hooks.
            Host->bFinished = true;
            Host->RefreshVisual();
            Host->SetLifeSpan(.35f);
        }
        break;
    case K::Burst:
    case K::Cone:
    case K::Ring:
    case K::Line:
        Pulse();
        break;
    case K::Surface:
    {
        const FTransform Transform(FRotator::ZeroRotator, Host->GetActorLocation());
        Host->Surface = Host->GetWorld()->SpawnActorDeferred<ATerrainSurface>(
            Host->ExecutionPrototype->SurfaceClass, Transform, Host->GetOwner(), Cast<APawn>(Host->GetOwner()),
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Host->Surface)
        {
            Finish(true);
            break;
        }
        Host->Surface->bOnlyAffectCasterEnemies = true;
        Host->Surface->bAffectPlayers = false;
        Host->Surface->bAffectEnemies = true;
        Host->Surface->SurfaceSize = FVector2D(Host->ExecutionPrototype->Radius * 2.f);
        Host->Surface->FinishSpawning(Transform);
        if (Host->Surface->Ground)
            Host->Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        break;
    }
    default:
        break;
    }
}

void FMixedAbilityDriver::Advance(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || Host->bFinished)
        return;
    if (!Host->Context || !Host->Context->ValidateSourceLife() || !Host->ExecutionPrototype ||
        !IsValid(Host->GetOwner()) ||
        (Host->GetOwner()->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Host->GetOwner())))
    {
        Finish(true);
        return;
    }
    using K = EAbilityPrototypeKind;
    const auto Kind = Host->ExecutionPrototype->Kind;
    const auto* FieldConfig = Cast<UBatch6FieldPrototype>(Host->ExecutionPrototype);
    const bool bFollowToggle = FieldConfig && FieldConfig->bFollowCasterToggle && Kind == K::Field && Depth == 0;
    const bool bFinalBoundary = FieldConfig && !bFollowToggle && (Kind == K::Wave || Kind == K::Field);
    if (bFinalBoundary)
        Delta = FMath::Min(Delta, FMath::Max(0.f, Host->ExecutionPrototype->Duration - Age));
    Age += Delta;
    if (bFollowToggle)
        Host->SetActorLocation(Host->GetOwner()->GetActorLocation());
    if ((Kind == K::Beam || Kind == K::Orbit || bFollowToggle) &&
        !UPureMassAbility::SpendMana(Host->GetOwner(), Host->ExecutionPrototype->ManaPerSecond * Delta))
    {
        Finish(true);
        return;
    }
    // Orbit is a toggle, finite kinds honor their configured duration.
    if (!bFinalBoundary && !bFollowToggle && (Kind != K::Orbit || Depth > 0) &&
        Age >= Host->ExecutionPrototype->Duration)
    {
        Finish(false);
        return;
    }
    if (Kind != K::Orbit && Kind != K::Beam && Kind != K::Field && Kind != K::Chain && Kind != K::Emitter)
    {
        PeriodClock += Delta;
        for (int32 I = 0; I < 8 && PeriodClock >= Host->ExecutionPrototype->Period; ++I)
        {
            PeriodClock -= Host->ExecutionPrototype->Period;
            if (Kind != K::Burst)
                Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Host->GetActorLocation(), Target);
        }
    }
    if (Kind == K::Projectile || Kind == K::Multishot || Kind == K::Homing || Kind == K::Piercing ||
        Kind == K::Returning)
    {
        Projectile(Delta);
        return;
    }
    if (Kind == K::Orbit)
    {
        Orbit(Delta);
        return;
    }
    if (Kind == K::Wave)
    {
        PreviousWaveRadius = Host->WaveRadius;
        Host->WaveRadius =
            FMath::Min(Host->ExecutionPrototype->Radius, Host->WaveRadius + Host->ExecutionPrototype->Speed * Delta);
        Pulse();
        if (Host->WaveRadius >= Host->ExecutionPrototype->Radius ||
            (bFinalBoundary && Age >= Host->ExecutionPrototype->Duration))
            Finish(false);
        return;
    }
    if (Kind == K::Delay)
    {
        if (Age >= Host->ExecutionPrototype->ArmDelay)
            Finish(false);
        return;
    }
    if (Kind == K::Trap)
    {
        if (Age >= Host->ExecutionPrototype->ArmDelay &&
            Nearest(Host->GetActorLocation(), Host->ExecutionPrototype->Radius).IsSet())
        {
            Pulse();
            Finish(false);
        }
        return;
    }
    if (Kind == K::Beam)
    {
        FVector Position;
        if (!Host->Context->ResolveTargetLife(Target, Position) ||
            FVector::DistSquared(Host->GetOwner()->GetActorLocation(), Position) >
                FMath::Square(Host->ExecutionPrototype->Range) ||
            !Visible(Host->GetOwner()->GetActorLocation() + FVector(0, 0, 30), Target))
        {
            Finish(false);
            return;
        }
        Host->SetActorLocation(Host->GetOwner()->GetActorLocation() + FVector(0, 0, 30));
        Host->BeamEnd = Position;
    }
    if (Kind == K::Beam || Kind == K::Field || Kind == K::Chain || Kind == K::Emitter)
        PeriodClock += Delta;
    if (Kind == K::Chain && HitTargets.IsEmpty())
    {
        Chain();
        PeriodClock = 0.f;
        return;
    }
    // At most eight catch-up pulses per frame; no unbounded bursts after a hitch.
    for (int32 I = 0; I < 8 && !Host->bFinished && PeriodClock >= Host->ExecutionPrototype->Period; ++I)
    {
        PeriodClock -= Host->ExecutionPrototype->Period;
        if (Kind == K::Chain)
        {
            Chain();
            continue;
        }
        if (Kind == K::Field)
            Pulse();
        if (Kind == K::Beam)
            Hit(Target);
        if (Kind == K::Emitter)
        {
            Target = Nearest(Host->GetActorLocation(), Host->ExecutionPrototype->Radius);
            FVector Position;
            if (!Host->Context->ResolveTargetLife(Target, Position))
            {
                PeriodClock = 0.f;
                break;
            }
            Direction = (Position - Host->GetActorLocation()).GetSafeNormal(SMALL_NUMBER, Direction);
            if (Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Host->GetActorLocation(), Target) &&
                ++Emissions >= Host->ExecutionPrototype->Count)
                Finish(false);
        }
        else
            Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Host->GetActorLocation(), Target);
    }
    if (bFinalBoundary && Age >= Host->ExecutionPrototype->Duration && !Host->bFinished)
        Finish(false);
}

bool FMixedAbilityDriver::RequestChild(FName Event, UAbilityPrototype* Child, FVector Position, FVector ChildDirection,
                                       FPureMassCombatTarget Candidate, int32 ChildDepth, bool bSingle)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host) || (!Child && !ChildAdmission) || !Host->Context || !Host->Context->ValidateSourceLife())
    {
        return false;
    }
    // Graph edges own composition, including ports whose legacy DA child reference was empty.
    // Standalone callers still require a child config and retain the original spawn behavior.
    if (!ChildAdmission)
    {
        return APureMassRuntime::Spawn(Host->Context, Child, Position, ChildDirection, Candidate, ChildDepth,
                                       bSingle) != nullptr;
    }
    FLegacyAbilityChildRequest Request;
    Request.Event = Event;
    Request.Config = Child;
    Request.Context = Host->Context;
    Request.Position = Position;
    Request.Direction = ChildDirection;
    Request.Target = Candidate;
    Request.Depth = ChildDepth;
    Request.bSingle = bSingle;
    return ChildAdmission(Request);
}
