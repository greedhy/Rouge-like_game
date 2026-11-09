/** Extracted authority behavior. Host BeginPlay or graph Start -> Start; graph/host Tick -> Advance. */
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interface/CombatInterface.h"
#include "Terrain/TerrainSurface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Character/BaseCharacter.h"

FActorAbilityDriver::FActorAbilityDriver(APrototypeRuntime* Host) : Presentation(Host)
{
}

bool FActorAbilityDriver::IsFinished() const
{
    const auto* Host = Presentation.Get();
    return !IsValid(Host) || Host->bFinished;
}

void FActorAbilityDriver::GatherTargets(TArray<AActor*>& Out) const
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    Out.Reset();
    if (!Host->Context || !Host->ExecutionPrototype || !Host->Context->Caster.IsValid())
        return;
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Host, Candidates, {Host->Context->Caster.Get()},
                                                        Host->ExecutionPrototype->Radius, Host->GetActorLocation());
    for (auto* Target : Candidates)
        if (IsLiveTarget(Target) && FVector::DistSquared(Target->GetActorLocation(), Host->GetActorLocation()) <=
                                        FMath::Square(Host->ExecutionPrototype->Radius))
            Out.AddUnique(Target);
}

bool FActorAbilityDriver::HasLineOfSight(AActor* Target) const
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    if (!IsValid(Target) || !IsValid(Host->GetOwner()))
        return false;
    FHitResult HitResult;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeLOS), false, Host->GetOwner());
    Query.AddIgnoredActor(Host);
    return !Host->GetWorld()->LineTraceSingleByChannel(HitResult,
                                                       Host->GetOwner()->GetActorLocation() + FVector(0, 0, 30),
                                                       Target->GetActorLocation(), ECC_Visibility, Query) ||
           HitResult.GetActor() == Target;
}

void FActorAbilityDriver::Hit(AActor* Target, float Scale)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (!Host->HasAuthority() || !Host->Context || !Host->Context->ValidateSourceLife() || Host->bFinished ||
        !IsLiveTarget(Target))
        return;
    FDamageEffectParams Params = Host->Context->DamageParams;
    Params.WorldContextObject = Host;
    Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    Params.BaseDamage = Host->ExecutionPrototype->DamageAtLevel(Host->Context->Level) * Scale;
    Params.DamageType = Host->ExecutionPrototype->DamageType;
    Params.bIsRadialDamage = false;
    UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    // Damage can synchronously destroy the target; observer cancellation must close all later business callbacks.
    if (!IsValid(Target) || !Host->Context->ValidateSourceLife() || Host->bFinished)
    {
        return;
    }
    Emit(TEXT("OnHit"), Host->ExecutionPrototype->OnHit, Target, Target->GetActorLocation());
    if (IsValid(Target) && Host->Context->ValidateSourceLife() && !Host->bFinished &&
        ICombatInterface::Execute_IsDead(Target))
    {
        Host->BehaviorEvent.Broadcast(TEXT("OnKill"), Target, Target->GetActorLocation());
    }
}

void FActorAbilityDriver::Pulse()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    TArray<AActor*> Targets;
    GatherTargets(Targets);
    for (auto* Target : Targets)
        Hit(Target);
    if (Host->ExecutionPrototype->Kind != EAbilityPrototypeKind::Burst)
        Host->MulticastImpact(Host->GetActorLocation());
}

void FActorAbilityDriver::TickProjectile(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    const bool bReturnKind = Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Returning;
    float Remaining = FMath::Max(0.f, Host->ExecutionPrototype->Range - Distance);
    if (bReturnKind && Host->bReturning)
    {
        const FVector ToCaster = Host->GetOwner()->GetActorLocation() - Host->GetActorLocation();
        Remaining = ToCaster.Size();
        if (Remaining <= Host->ExecutionPrototype->CollisionRadius)
        {
            Finish(false);
            return;
        }
        Direction = ToCaster.GetSafeNormal();
        Host->SetActorRotation(Direction.Rotation());
    }
    if (Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Homing)
    {
        if (!IsLiveTarget(AttackTarget.Get()))
        {
            Finish(false);
            return;
        }
        Direction = FMath::VInterpNormalRotationTo(
            Direction, (AttackTarget->GetActorLocation() - Host->GetActorLocation()).GetSafeNormal(), Delta,
            Host->ExecutionPrototype->HomingTurnRate);
        Host->SetActorRotation(Direction.Rotation());
    }
    const float Step = FMath::Min(Host->ExecutionPrototype->Speed * Delta, Remaining);
    const FVector Start = Host->GetActorLocation(), End = Start + Direction * Step;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeProjectile), false, Host->GetOwner());
    Query.AddIgnoredActor(Host);
    for (const auto& Previous : HitTargets)
        if (Previous.IsValid())
            Query.AddIgnoredActor(Previous.Get());
    // A multi-channel sweep stops at the first blocking hit. Ignore friends before
    // querying so their blocking capsules cannot hide enemies farther along the segment.
    for (TActorIterator<ABaseCharacter> It(Host->GetWorld()); It; ++It)
        if (!IsLiveTarget(*It))
            Query.AddIgnoredActor(*It);
    TArray<FHitResult> Hits;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    // Object queries include the character capsule even when its Visibility response
    // ignores traces (the project's small goblin meshes do not cover the capsule centre).
    Host->GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
                                             FCollisionShape::MakeSphere(Host->ExecutionPrototype->CollisionRadius),
                                             Query);
    Hits.Sort(
        [](const FHitResult& A, const FHitResult& B)
        {
            return A.Time < B.Time;
        });
    for (const FHitResult& Result : Hits)
    {
        AActor* Target = Result.GetActor();
        if (Target == Host->GetOwner() || Target == Host || Cast<APrototypeRuntime>(Target))
            continue;
        if (IsValid(Target) && Target->Implements<UCombatInterface>() && !IsLiveTarget(Target))
            continue;
        // Overlap-only trigger volumes (portals, pickups, encounter areas) are not walls.
        if (!IsLiveTarget(Target) && (!Result.GetComponent() || Result.GetComponent()->GetCollisionResponseToChannel(
                                                                    ECC_Visibility) != ECR_Block))
            continue;
        Host->SetActorLocation(Result.Location);
        if (IsLiveTarget(Target))
        {
            if (HitTargets.Contains(Target))
                continue;
            if (bReturnKind && HitTargets.Num() >= Host->ExecutionPrototype->Count)
                continue;
            Hit(Target, bReturnKind ? (Host->bReturning ? Host->ExecutionPrototype->DamageFalloff : 1.f)
                                    : FMath::Pow(Host->ExecutionPrototype->DamageFalloff, HitTargets.Num()));
            HitTargets.Add(Target);
            if (bReturnKind)
            {
                Host->MulticastImpact(Result.Location);
                continue;
            }
            if (Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Piercing &&
                HitTargets.Num() < Host->ExecutionPrototype->Count)
            {
                Host->MulticastImpact(Result.Location);
                continue;
            }
        }
        Host->bImpactOnFinish = true;
        Finish(false);
        return;
    }
    Host->SetActorLocation(End);
    Distance += Step;
    if (bReturnKind)
    {
        if (Host->bReturning && Step >= Remaining)
        {
            Finish(false);
            return;
        }
        if (!Host->bReturning && Distance >= Host->ExecutionPrototype->Range)
        {
            Host->bReturning = true;
            HitTargets.Reset();
            Host->ForceNetUpdate();
            // The outbound endpoint can occur partway through this frame.
            const float ReturnDelta = FMath::Max(0.f, Delta - Step / Host->ExecutionPrototype->Speed);
            if (ReturnDelta > SMALL_NUMBER)
                TickProjectile(ReturnDelta);
        }
    }
    else if (Distance >= Host->ExecutionPrototype->Range)
        Finish(false);
}

void FActorAbilityDriver::TickChain(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    PeriodClock += Delta;
    if (!HitTargets.IsEmpty() && PeriodClock < Host->ExecutionPrototype->Period)
        return;
    PeriodClock = 0.f;
    AActor* Target = AttackTarget.Get();
    const float Reach = HitTargets.IsEmpty() ? Host->ExecutionPrototype->Range : Host->ExecutionPrototype->Radius;
    if (HitTargets.Num() >= Host->ExecutionPrototype->Count || !IsLiveTarget(Target) || HitTargets.Contains(Target) ||
        FVector::DistSquared(ChainOrigin, Target->GetActorLocation()) > FMath::Square(Reach) ||
        !VisibleFrom(ChainOrigin, Target))
    {
        Finish(false);
        return;
    }
    Host->SetActorLocation(ChainOrigin);
    Host->BeamEnd = Target->GetActorLocation();
    Hit(Target, FMath::Pow(Host->ExecutionPrototype->DamageFalloff, HitTargets.Num()));
    HitTargets.Add(Target);
    LastChainTarget = Target;
    ChainOrigin = Host->BeamEnd;
    AttackTarget = nullptr;
    float BestDistance = FMath::Square(Host->ExecutionPrototype->Radius);
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Host, Candidates, {Host->GetOwner()},
                                                        Host->ExecutionPrototype->Radius, ChainOrigin);
    for (auto* Candidate : Candidates)
    {
        const float DistanceSquared = FVector::DistSquared(ChainOrigin, Candidate->GetActorLocation());
        if (IsLiveTarget(Candidate) && !HitTargets.Contains(Candidate) && DistanceSquared <= BestDistance &&
            VisibleFrom(ChainOrigin, Candidate))
        {
            BestDistance = DistanceSquared;
            AttackTarget = Candidate;
        }
    }
    Host->ForceNetUpdate();
}

void FActorAbilityDriver::ConePulse()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    TArray<AActor*> Targets;
    GatherTargets(Targets);
    const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Host->ExecutionPrototype->SpreadDegrees * .5f));
    const FVector Facing = Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector);
    for (auto* Target : Targets)
    {
        const FVector Offset = Target->GetActorLocation() - Host->GetActorLocation();
        if ((Offset.IsNearlyZero() ||
             FVector::DotProduct(Facing, Offset.GetSafeNormal2D()) + KINDA_SMALL_NUMBER >= CosHalf) &&
            VisibleFrom(Host->GetActorLocation(), Target))
            Hit(Target);
    }
}

void FActorAbilityDriver::ShapePulse()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    const bool bLine = Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Line;
    const float Bounds =
        FMath::Sqrt(FMath::Square(bLine ? Host->ExecutionPrototype->Range : Host->ExecutionPrototype->Radius) +
                    (bLine ? FMath::Square(Host->ExecutionPrototype->Width * .5f) : 0.f) +
                    FMath::Square(Host->ExecutionPrototype->ShapeHalfHeight));
    TArray<AActor*> Targets;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Host, Targets, {Host->Context->Caster.Get()}, Bounds,
                                                        Host->GetActorLocation());
    const FVector Forward = Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    for (auto* Target : Targets)
    {
        if (!IsLiveTarget(Target))
            continue;
        const FVector Offset = Target->GetActorLocation() - Host->GetActorLocation();
        if (FMath::Abs(Offset.Z) > Host->ExecutionPrototype->ShapeHalfHeight)
            continue;
        const float Along = FVector::DotProduct(Offset, Forward);
        const bool bInside =
            bLine ? Along >= 0.f && Along <= Host->ExecutionPrototype->Range &&
                        FMath::Abs(FVector::DotProduct(Offset, Right)) <= Host->ExecutionPrototype->Width * .5f
                  : Offset.SizeSquared2D() >= FMath::Square(Host->ExecutionPrototype->InnerRadius) &&
                        Offset.SizeSquared2D() <= FMath::Square(Host->ExecutionPrototype->Radius);
        if (bInside && VisibleFrom(Host->GetActorLocation(), Target))
            Hit(Target);
    }
}

void FActorAbilityDriver::TickWave(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    const float Previous = Host->WaveRadius;
    Host->WaveRadius =
        FMath::Min(Host->ExecutionPrototype->Radius, Host->WaveRadius + Host->ExecutionPrototype->Speed * Delta);
    TArray<AActor*> Targets;
    GatherTargets(Targets);
    for (auto* Target : Targets)
    {
        const float DistanceToTarget = FVector::Distance(Host->GetActorLocation(), Target->GetActorLocation());
        if (!HitTargets.Contains(Target) && DistanceToTarget <= Host->WaveRadius &&
            (Previous == 0.f || DistanceToTarget > Previous) && VisibleFrom(Host->GetActorLocation(), Target))
        {
            HitTargets.Add(Target);
            Hit(Target);
            Host->MulticastImpact(Target->GetActorLocation());
        }
    }
    if (Host->WaveRadius >= Host->ExecutionPrototype->Radius)
        Finish(false);
}

void FActorAbilityDriver::TickEmitter(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    PeriodClock += Delta;
    while (!Host->bFinished && PeriodClock + SMALL_NUMBER >= Host->ExecutionPrototype->Period)
    {
        PeriodClock = FMath::Max(0.f, PeriodClock - Host->ExecutionPrototype->Period);
        AActor* Best = nullptr;
        float BestDistance = FMath::Square(Host->ExecutionPrototype->Radius);
        TArray<AActor*> Targets;
        GatherTargets(Targets);
        for (auto* Target : Targets)
        {
            const float CandidateDistance = FVector::DistSquared(Host->GetActorLocation(), Target->GetActorLocation());
            if (CandidateDistance <= BestDistance && VisibleFrom(Host->GetActorLocation(), Target))
            {
                Best = Target;
                BestDistance = CandidateDistance;
            }
        }
        if (Best)
        {
            AttackTarget = Best;
            Direction = (Best->GetActorLocation() - Host->GetActorLocation())
                            .GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
            Host->BeamEnd = Best->GetActorLocation();
            if (Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Best, Host->GetActorLocation()))
            {
                if (++Emissions >= Host->ExecutionPrototype->Count)
                    Finish(false);
                continue;
            }
        }
        // Idle or rejected shots consume time, not the successful-emission budget.
        // Drop missed idle periods so acquiring a target does not release a backlog.
        PeriodClock = FMath::Fmod(PeriodClock, Host->ExecutionPrototype->Period);
        return;
    }
}

bool FActorAbilityDriver::VisibleFrom(const FVector& Origin, AActor* Target) const
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    if (!IsValid(Target))
        return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeChainLOS), false, Host->GetOwner());
    Query.AddIgnoredActor(Host);
    for (TActorIterator<ABaseCharacter> It(Host->GetWorld()); It; ++It)
        if (*It != Target)
            Query.AddIgnoredActor(*It);
    FHitResult Obstruction;
    return !Host->GetWorld()->LineTraceSingleByChannel(Obstruction, Origin, Target->GetActorLocation(), ECC_Visibility,
                                                       Query) ||
           Obstruction.GetActor() == Target;
}

bool FActorAbilityDriver::Emit(FName Event, UAbilityPrototype* Child, AActor* Target, FVector Position)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    if (!Host->HasAuthority() || !Host->Context || !Host->Context->ValidateSourceLife())
        return false;
    Host->BehaviorEvent.Broadcast(Event, Target, Position);
    if (!Host->Context->ValidateSourceLife())
        return false;
    return RequestChild(Event, Child, Position, Direction, FPureMassCombatTarget::FromActor(Target), Depth + 1, false);
}

void FActorAbilityDriver::Finish(bool bCancelled)
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
        Host->BehaviorEvent.Broadcast(TEXT("OnCancel"), nullptr, Host->GetActorLocation());
    }
    else if (Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Chain && !HitTargets.IsEmpty())
        Emit(TEXT("OnExpire"), Host->ExecutionPrototype->OnExpire, LastChainTarget.Get(), ChainOrigin);
    else
        Emit(TEXT("OnExpire"), Host->ExecutionPrototype->OnExpire, AttackTarget.Get(), Host->GetActorLocation());
    CleanupOwnedActors();
    Host->OnRep_Finished();
    Host->ForceNetUpdate();
    // Keep the replicated impact state alive long enough to open an actor channel,
    // including projectiles that hit during their very first server tick.
    Host->SetLifeSpan(.35f);
}

void FActorAbilityDriver::OffensiveAttack(UGameplayAbility* Ability, AActor* Target)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (!Ability || !IsLiveTarget(Target))
        return;
    if (FVector::DistSquared(Host->GetOwner()->GetActorLocation(), Target->GetActorLocation()) >
        FMath::Square(Host->ExecutionPrototype->OrbitSettings->MaxTargetDistance))
        return;
    AttackTarget = Target;
}

void FActorAbilityDriver::TickOrbit(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    auto* Settings = Host->ExecutionPrototype->OrbitSettings.Get();
    Host->SetActorLocation(Host->GetOwner()->GetActorLocation());
    Host->Bolts.RemoveAll(
        [](const auto& Bolt)
        {
            return !IsValid(Bolt);
        });
    PeriodClock += Delta;
    LaunchClock += Delta;
    if (PeriodClock >= Settings->SpawnInterval)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Settings->SpawnInterval);
        if (Host->Bolts.Num() < Settings->MaxFireBolts)
        {
            FTransform Transform(FRotator::ZeroRotator,
                                 Host->GetActorLocation() + FVector(Settings->OrbitRadius, 0, Settings->OrbitHeight));
            auto* Bolt = Host->GetWorld()->SpawnActorDeferred<AMyOrbitingFireBolt>(
                Settings->FireBoltClass, Transform, Host->GetOwner(), Cast<APawn>(Host->GetOwner()),
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (Bolt)
            {
                Bolt->DamageEffectParams = Host->Context->DamageParams;
                Bolt->DamageEffectParams.BaseDamage = Host->ExecutionPrototype->DamageAtLevel(Host->Context->Level);
                Bolt->DamageEffectParams.DamageType = Host->ExecutionPrototype->DamageType;
                Bolt->SetCastContext(Host->Context);
                Bolt->InitializeOrbit(Host->GetOwner(), Settings, 0.f);
                Bolt->DamageApplied.AddUObject(Host, &APrototypeRuntime::OrbitHit);
                Bolt->FinishSpawning(Transform);
                Host->Bolts.Add(Bolt);
            }
        }
        Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, AttackTarget.Get(), Host->GetActorLocation());
    }
    if (!IsLiveTarget(AttackTarget.Get()) ||
        FVector::DistSquared(Host->GetActorLocation(), AttackTarget->GetActorLocation()) >
            FMath::Square(Settings->MaxTargetDistance))
    {
        AttackTarget.Reset();
        for (AMyOrbitingFireBolt* Bolt : Host->Bolts)
            if (Bolt->IsLaunched())
                Bolt->ReturnToOrbit();
    }
    else if (LaunchClock >= Settings->LaunchInterval)
    {
        LaunchClock = 0.f;
        for (AMyOrbitingFireBolt* Bolt : Host->Bolts)
            if (!Bolt->IsLaunched())
            {
                Bolt->LaunchAt(AttackTarget.Get());
                break;
            }
    }
    int32 OrbitCount = 0, Index = 0;
    for (AMyOrbitingFireBolt* Bolt : Host->Bolts)
        if (!Bolt->IsLaunched())
            ++OrbitCount;
    for (AMyOrbitingFireBolt* Bolt : Host->Bolts)
        if (!Bolt->IsLaunched())
            Bolt->SetOrbitPhase(360.f * Index++ / FMath::Max(1, OrbitCount));
}

void FActorAbilityDriver::OrbitHit(AActor* Target)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (!IsValid(Target) || !Host->Context || !Host->Context->ValidateSourceLife() || Host->bFinished)
        return;
    Emit(TEXT("OnHit"), Host->ExecutionPrototype->OnHit, Target, Target->GetActorLocation());
    if (IsValid(Target) && Host->Context->ValidateSourceLife() && !Host->bFinished &&
        Target->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Target))
    {
        Host->BehaviorEvent.Broadcast(TEXT("OnKill"), Target, Target->GetActorLocation());
    }
}

void FActorAbilityDriver::CleanupOwnedActors()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (Host->HasAuthority())
    {
        if (Host->Context)
            if (auto* ASC = Cast<UMyAbilitySystemComponent>(Host->Context->DamageParams.SourceAbilitySystemComponent))
                ASC->OffensiveAttackTargeted.Remove(AttackDelegate);
        if (IsValid(Host->Surface))
            Host->Surface->Destroy();
        for (AMyOrbitingFireBolt* Bolt : Host->Bolts)
            if (IsValid(Bolt))
                Bolt->Destroy();
    }
    Host->Bolts.Reset();
    Host->Surface = nullptr;
}

void FActorAbilityDriver::Start()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || !Host->ExecutionPrototype || !Host->Context)
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
        if (!Host->bSingleProjectile)
        {
            for (int32 Index = 0; Index < Host->ExecutionPrototype->Count; ++Index)
            {
                const float Angle =
                    Host->ExecutionPrototype->Count == 1
                        ? 0.f
                        : -Host->ExecutionPrototype->SpreadDegrees * .5f +
                              Host->ExecutionPrototype->SpreadDegrees * Index / (Host->ExecutionPrototype->Count - 1);
                RequestChild(TEXT("Multishot"), Host->ExecutionPrototype, Host->GetActorLocation(),
                             Direction.RotateAngleAxis(Angle, FVector::UpVector), {}, Depth, true);
            }
            Host->bFinished = true;
            Host->SetLifeSpan(.15f);
            Host->Visual->DeactivateImmediate();
        }
        break;
    case K::Burst:
        Pulse();
        break;
    case K::Cone:
        ConePulse();
        break;
    case K::Ring:
    case K::Line:
        ShapePulse();
        break;
    case K::Surface:
    {
        FTransform Transform(FRotator::ZeroRotator, Host->GetActorLocation());
        Host->Surface = Host->GetWorld()->SpawnActorDeferred<ATerrainSurface>(
            Host->ExecutionPrototype->SurfaceClass, Transform, Host->GetOwner(), Cast<APawn>(Host->GetOwner()),
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Host->Surface)
        {
            Host->Surface->bOnlyAffectCasterEnemies = true;
            Host->Surface->SurfaceSize = FVector2D(Host->ExecutionPrototype->Radius * 2.f);
            Host->Surface->FinishSpawning(Transform);
            Host->Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        else
            Finish(true);
        break;
    }
    case K::Orbit:
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(Host->Context->DamageParams.SourceAbilitySystemComponent))
            AttackDelegate = ASC->OffensiveAttackTargeted.AddUObject(Host, &APrototypeRuntime::OffensiveAttack);
        break;
    default:
        break;
    }
}

void FActorAbilityDriver::Advance(float Delta)
{
    using K = EAbilityPrototypeKind;
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || Host->bFinished || !Host->Context)
        return;
    AActor* Caster = Host->Context->Caster.Get();
    if (!Host->Context->ValidateSourceLife() || !IsValid(Caster) ||
        (Caster->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Caster)))
    {
        Finish(true);
        return;
    }
    Age += Delta;
    if (Host->ExecutionPrototype->Kind == K::Delay)
    {
        if (Age >= Host->ExecutionPrototype->ArmDelay)
            Finish(false);
        return;
    }
    ManaClock += Delta;
    if (ManaClock >= 1.f && (Host->ExecutionPrototype->Kind == K::Beam || Host->ExecutionPrototype->Kind == K::Orbit))
    {
        const int32 Seconds = FMath::FloorToInt(ManaClock);
        ManaClock -= Seconds;
        if (!Host->SpendMana(Caster, Host->ExecutionPrototype->ManaPerSecond * Seconds))
        {
            Finish(true);
            return;
        }
    }
    if (Host->ExecutionPrototype->Kind == K::Wave || Host->ExecutionPrototype->Kind == K::Returning ||
        Host->ExecutionPrototype->Kind == K::Emitter)
    {
        // Consume the final partial frame before the hard lifetime expires.
        const float ActiveDelta =
            FMath::Min(Delta, FMath::Max(0.f, Host->ExecutionPrototype->Duration - (Age - Delta)));
        if (Host->ExecutionPrototype->Kind == K::Wave)
            TickWave(ActiveDelta);
        else if (Host->ExecutionPrototype->Kind == K::Returning)
            TickProjectile(ActiveDelta);
        else
            TickEmitter(ActiveDelta);
        if (Age >= Host->ExecutionPrototype->Duration)
            Finish(false);
        return;
    }
    // Field pulses are scheduled over (0, Duration], including the last boundary.
    // Consume only the active portion of a hitch before emitting the expiry child.
    if (Host->ExecutionPrototype->Kind == K::Field)
    {
        PeriodClock += FMath::Min(Delta, FMath::Max(0.f, Host->ExecutionPrototype->Duration - (Age - Delta)));
        while (PeriodClock + KINDA_SMALL_NUMBER >= Host->ExecutionPrototype->Period && !Host->bFinished &&
               !Host->Context->bCancelled)
        {
            PeriodClock -= Host->ExecutionPrototype->Period;
            Pulse();
            Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, AttackTarget.Get(), Host->GetActorLocation());
        }
        if (Age >= Host->ExecutionPrototype->Duration)
            Finish(false);
        return;
    }
    // Orbit is explicitly toggled; all other modes have a finite hard lifetime.
    if ((Host->ExecutionPrototype->Kind != K::Orbit || Depth > 0) && Age >= Host->ExecutionPrototype->Duration)
    {
        Finish(false);
        return;
    }
    if (Host->ExecutionPrototype->Kind == K::Chain)
    {
        TickChain(Delta);
        return;
    }
    if (Host->ExecutionPrototype->Kind == K::Projectile || Host->ExecutionPrototype->Kind == K::Multishot ||
        Host->ExecutionPrototype->Kind == K::Homing || Host->ExecutionPrototype->Kind == K::Piercing)
    {
        TickProjectile(Delta);
        return;
    }
    if (Host->ExecutionPrototype->Kind == K::Cone || Host->ExecutionPrototype->Kind == K::Ring ||
        Host->ExecutionPrototype->Kind == K::Line)
        return;
    if (Host->ExecutionPrototype->Kind == K::Orbit)
    {
        TickOrbit(Delta);
        return;
    }
    if (Host->ExecutionPrototype->Kind == K::Beam)
    {
        AActor* Target = AttackTarget.Get();
        if (!IsLiveTarget(Target) ||
            FVector::DistSquared(Caster->GetActorLocation(), Target->GetActorLocation()) >
                FMath::Square(Host->ExecutionPrototype->Range) ||
            !HasLineOfSight(Target))
        {
            Finish(true);
            return;
        }
        Host->SetActorLocation(Caster->GetActorLocation() + FVector(0, 0, 30));
        Host->BeamEnd = Target->GetActorLocation();
    }
    if (Host->ExecutionPrototype->Kind == K::Trap)
    {
        if (Age < Host->ExecutionPrototype->ArmDelay)
            return;
        TArray<AActor*> Targets;
        GatherTargets(Targets);
        if (!Targets.IsEmpty())
        {
            Pulse();
            Finish(false);
        }
        return;
    }
    PeriodClock += Delta;
    if (PeriodClock >= Host->ExecutionPrototype->Period)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Host->ExecutionPrototype->Period);
        if (Host->ExecutionPrototype->Kind == K::Field)
            Pulse();
        if (Host->ExecutionPrototype->Kind == K::Beam)
            Hit(AttackTarget.Get());
        if (Host->ExecutionPrototype->Kind != K::Burst)
            Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, AttackTarget.Get(), Host->GetActorLocation());
    }
}

bool FActorAbilityDriver::RequestChild(FName Event, UAbilityPrototype* Child, FVector Position, FVector ChildDirection,
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
        return APrototypeRuntime::Spawn(Host->Context, Child, Position, ChildDirection, Candidate.Actor.Get(),
                                        ChildDepth, bSingle) != nullptr;
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

/** Capture selection identity once and reject a later pool lease before aiming or applying damage. */
bool FActorAbilityDriver::IsLiveTarget(AActor* Target) const
{
    const auto* Host = Presentation.Get();
    FVector Position;
    return IsValid(Host) && Host->Context && Host->IsEnemy(Host->GetOwner(), Target) &&
           Host->Context->ResolveTargetLife(FPureMassCombatTarget::FromActor(Target), Position);
}
