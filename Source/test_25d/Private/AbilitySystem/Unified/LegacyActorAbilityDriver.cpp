/** Extracted authority behavior. Host BeginPlay or graph Start -> Start; graph/host Tick -> Advance. */
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeSurfaceService.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/AbilityPrototypeFlightQueries.h"
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
    return AbilityPrototypeTargetServices::ActorLineOfSight(Host->GetWorld(), Host->GetOwner(), Target,
                                                            Host->GetOwner()->GetActorLocation() + FVector(0, 0, 30),
                                                            true, Host);
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
    const bool bHoming = Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Homing;
    TOptional<FVector> Aim;
    if (bHoming)
    {
        if (!IsLiveTarget(AttackTarget.Get()))
        {
            Finish(false);
            return;
        }
        Aim = AttackTarget->GetActorLocation();
    }
    FAbilityProjectileMotionConfig Motion;
    Motion.Motion = bReturnKind ? EAbilityProjectileMotion::Returning
                    : bHoming   ? EAbilityProjectileMotion::LegacyTurnHoming
                                : EAbilityProjectileMotion::Linear;
    Motion.Speed = Host->ExecutionPrototype->Speed;
    Motion.Range = Host->ExecutionPrototype->Range;
    Motion.CollisionRadius = Host->ExecutionPrototype->CollisionRadius;
    Motion.HomingTurnRate = Host->ExecutionPrototype->HomingTurnRate;
    FAbilityProjectileMotionState State;
    State.Position = Host->GetActorLocation();
    State.Direction = Direction;
    State.OutboundDistance = Distance;
    State.Phase = Host->bReturning ? EAbilityProjectilePhase::Returning : EAbilityProjectilePhase::Outbound;
    FAbilityFlightSegment Segment;
    if (!AbilityProjectileMotion::ComputeFlightSegment(Motion, State, Delta, Host->GetOwner()->GetActorLocation(), Aim,
                                                       Segment))
    {
        Finish(false);
        return;
    }
    // 浮点出程余量可能产生小于Equals容差的有效返航微步；只有明确到达来源才可结束。
    if (bReturnKind && Host->bReturning && Segment.bReachesPhaseEnd && Segment.Start.Equals(Segment.End))
    {
        Finish(false);
        return;
    }
    Direction = Segment.Direction;
    if (bHoming || (bReturnKind && Host->bReturning))
    {
        Host->SetActorRotation(Direction.Rotation());
    }
    const FVector Start = Segment.Start;
    const FVector End = Segment.End;
    const float Step = FVector::Distance(Start, End);
    const auto Policy = bReturnKind ? EAbilityProjectileImpactPolicy::ReturnPerPhase
                        : Host->ExecutionPrototype->Kind == EAbilityPrototypeKind::Piercing
                            ? EAbilityProjectileImpactPolicy::Pierce
                            : EAbilityProjectileImpactPolicy::StopFirst;
    TSet<FPureMassCombatTarget> Ignored;
    for (const auto& Previous : HitTargets)
    {
        FPureMassCombatTarget Identity;
        Identity.Actor = Previous;
        Ignored.Add(Identity);
    }
    auto Resolve =
        [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location, FString& Error)
    {
        const bool bLive = Host->Context->ResolveTargetLife(Candidate, Location);
        if (const auto* Life = Host->Context->FindTargetLife(Candidate))
        {
            Token = *Life;
        }
        return bLive;
    };
    TArray<FAbilityPrototypeFlightQueryHit> Hits;
    FString Error;
    if (!AbilityPrototypeFlightQueries::GatherActorSegment(Host->Context->GetSourceLife(), Start, End,
                                                           Host->ExecutionPrototype->CollisionRadius, Ignored, Resolve,
                                                           Hits, Error, Host))
    {
        Finish(true);
        return;
    }
    for (const auto& Raw : Hits)
    {
        FAbilityPrototypeFlightQueryHit Contact;
        if (!AbilityPrototypeFlightQueries::ClassifyActorContact(Host->Context->GetSourceLife(), Raw.Hit, Resolve,
                                                                 Contact, Host))
        {
            continue;
        }
        const FHitResult& Result = Contact.Hit;
        AActor* Target = Result.GetActor();
        Host->SetActorLocation(Result.Location);
        if (Contact.Candidate.IsSet())
        {
            const auto Admission = AbilityProjectileMotion::AdmitImpact(
                Policy, State.Phase, HitTargets.Num(), HitTargets.Contains(Target), Host->ExecutionPrototype->Count,
                Host->ExecutionPrototype->DamageFalloff);
            if (!Admission.bAccepted)
            {
                continue;
            }
            Hit(Target, Admission.DamageScale);
            HitTargets.Add(Target);
            if (!Admission.bTerminateAfterImpact)
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
        if (Host->bReturning && Segment.bReachesPhaseEnd)
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
            const float ReturnDelta = Segment.RemainingSeconds;
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

    bool bReady = false;
    AbilityPrototypeTargetServices::AdvanceChainClock(PeriodClock, Delta, Host->ExecutionPrototype->Period,
                                                      HitTargets.IsEmpty(), EAbilityPrototypeTargetProfile::Actor,
                                                      [&]()
                                                      {
                                                          bReady = true;
                                                          return true;
                                                      });
    if (!bReady)
    {
        return;
    }
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
    TArray<FPureMassCombatTarget> Visited;
    for (const auto& Previous : HitTargets)
    {
        FPureMassCombatTarget Identity;
        Identity.Actor = Previous;
        Visited.Add(Identity);
    }
    auto Resolve =
        [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Life, FVector& Location, FString& Error)
    {
        if (!IsLiveTarget(Candidate.Actor.Get()))
        {
            return false;
        }
        const auto* Captured = Host->Context->FindTargetLife(Candidate);
        if (!Captured)
        {
            return false;
        }
        Life = *Captured;
        Location = Candidate.Actor->GetActorLocation();
        return true;
    };
    FAbilityTargetToken Next;
    FVector NextPoint;
    FString Error;
    AbilityPrototypeTargetServices::SelectChainNext(
        Host->Context->GetSourceLife(), ChainOrigin, Host->ExecutionPrototype->Radius,
        EAbilityPrototypeTargetProfile::Actor, Visited, Resolve, Next, NextPoint, Error);
    AttackTarget = Next.Target.Actor;
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
    FAbilityShapeFilter Shape;
    Shape.Kind = EAbilityBehaviorShape::Cone;
    Shape.Radius = Host->ExecutionPrototype->Radius;
    Shape.SpreadDegrees = Host->ExecutionPrototype->SpreadDegrees;
    Shape.AngleTolerance = KINDA_SMALL_NUMBER;
    for (auto* Target : Targets)
    {
        if (AbilityBehaviorPrimitives::ContainsPoint(Shape, Host->GetActorLocation(), Direction,
                                                     Target->GetActorLocation()) &&
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
    FAbilityShapeFilter Shape;
    Shape.Kind = bLine ? EAbilityBehaviorShape::Line : EAbilityBehaviorShape::Ring;
    Shape.Radius = Host->ExecutionPrototype->Radius;
    Shape.InnerRadius = Host->ExecutionPrototype->InnerRadius;
    Shape.Range = Host->ExecutionPrototype->Range;
    Shape.Width = Host->ExecutionPrototype->Width;
    Shape.HalfHeight = Host->ExecutionPrototype->ShapeHalfHeight;
    Shape.bUse3DDistance = false;
    Shape.bRejectOutsideHeight = true;
    for (auto* Target : Targets)
    {
        if (!IsLiveTarget(Target))
            continue;
        const bool bInside = AbilityBehaviorPrimitives::ContainsPoint(Shape, Host->GetActorLocation(), Direction,
                                                                      Target->GetActorLocation());
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
        if (!HitTargets.Contains(Target) &&
            AbilityBehaviorPrimitives::ContainsWavePoint(Host->GetActorLocation(), Target->GetActorLocation(), Previous,
                                                         Host->WaveRadius, EAbilityWaveCoverage::SweptShell3D) &&
            VisibleFrom(Host->GetActorLocation(), Target))
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
    if (Host->bFinished)
    {
        // 保留旧辅助入口的累计时钟合同；迟到调用不能在正常结束后消费周期或重新投递。
        PeriodClock += Delta;
        return;
    }

    AbilityBehaviorPrimitives::AdvanceCadence(
        PeriodClock, Delta, Host->ExecutionPrototype->Period, MAX_int32, EAbilityCadenceRemainder::Preserve,
        [&]()
        {
            TArray<AActor*> Targets;
            GatherTargets(Targets);
            TArray<FAbilityTargetCandidate> Eligible;
            for (auto* Target : Targets)
            {
                if (VisibleFrom(Host->GetActorLocation(), Target))
                {
                    if (const auto* Life = Host->Context->FindTargetLife(FPureMassCombatTarget::FromActor(Target)))
                    {
                        Eligible.Add({*Life, Target->GetActorLocation()});
                    }
                }
            }
            const auto Selected = AbilityBehaviorPrimitives::SelectNearest(Eligible, Host->GetActorLocation(),
                                                                           Host->ExecutionPrototype->Radius, {},
                                                                           EAbilityCandidateTieBreak::Last);
            AActor* Best = Selected.IsSet() ? Selected->Target.Target.Actor.Get() : nullptr;
            if (Best)
            {
                AttackTarget = Best;
                Direction = (Best->GetActorLocation() - Host->GetActorLocation())
                                .GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
                Host->BeamEnd = Best->GetActorLocation();
                if (Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, Best, Host->GetActorLocation()))
                {
                    if (++Emissions >= Host->ExecutionPrototype->Count)
                    {
                        Finish(false);
                    }
                    return !Host->bFinished;
                }
            }
            // Idle/rejected shots consume time, never successful-emission count or a future burst backlog.
            PeriodClock = FMath::Fmod(PeriodClock, Host->ExecutionPrototype->Period);
            return false;
        },
        SMALL_NUMBER, true);
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
    return AbilityPrototypeTargetServices::ActorLineOfSight(Host->GetWorld(), Host->GetOwner(), Target, Origin, false,
                                                            Host);
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
        Host->Surface =
            AbilityPrototypeSurfaceService::Spawn(*Host->GetOwner(), *Host->ExecutionPrototype,
                                                  Host->GetActorLocation(), EAbilityPrototypeSurfaceProfile::Actor);
        if (!Host->Surface)
        {
            Finish(true);
        }
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
    if (Host->ExecutionPrototype->Kind == K::Beam || Host->ExecutionPrototype->Kind == K::Orbit)
    {
        const bool bChargeThisFrame = ManaClock + Delta >= 1.f;
        const float Cost = AbilityPrototypeTargetServices::ManaDue(
            ManaClock, Delta, Host->ExecutionPrototype->ManaPerSecond, EAbilityPrototypeTargetProfile::Actor);
        if (bChargeThisFrame && !Host->SpendMana(Caster, Cost))
        {
            Finish(true);
            return;
        }
    }
    else
    {
        ManaClock += Delta;
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
        if (!Host->bFinished && !Host->Context->bCancelled)
        {
            AbilityBehaviorPrimitives::AdvanceCadence(
                PeriodClock, 0.f, Host->ExecutionPrototype->Period, MAX_int32, EAbilityCadenceRemainder::Preserve,
                [&]()
                {
                    Pulse();
                    Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod, AttackTarget.Get(),
                         Host->GetActorLocation());
                    return !Host->bFinished && !Host->Context->bCancelled;
                },
                KINDA_SMALL_NUMBER);
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
        AbilityPrototypeTargetServices::AdvanceBeamClock(PeriodClock, Delta, Host->ExecutionPrototype->Period,
                                                         EAbilityPrototypeTargetProfile::Actor,
                                                         [&]()
                                                         {
                                                             Hit(AttackTarget.Get());
                                                             Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod,
                                                                  AttackTarget.Get(), Host->GetActorLocation());
                                                             return !Host->bFinished;
                                                         });
        return;
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
    if (Host->ExecutionPrototype->Kind == K::Surface)
    {
        AbilityPrototypeSurfaceService::AdvanceClock(PeriodClock, Delta, Host->ExecutionPrototype->Period,
                                                     EAbilityPrototypeSurfaceProfile::Actor,
                                                     [&]()
                                                     {
                                                         Emit(TEXT("OnPeriod"), Host->ExecutionPrototype->OnPeriod,
                                                              AttackTarget.Get(), Host->GetActorLocation());
                                                         return !Host->bFinished;
                                                     });
        return;
    }
    PeriodClock += Delta;
    if (PeriodClock >= Host->ExecutionPrototype->Period)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Host->ExecutionPrototype->Period);
        if (Host->ExecutionPrototype->Kind == K::Field)
            Pulse();
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
