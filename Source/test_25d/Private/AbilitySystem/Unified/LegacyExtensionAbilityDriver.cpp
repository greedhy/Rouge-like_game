/** Extracted authority behavior. Host BeginPlay or graph Start -> Start; graph/host Tick -> Advance. */
#include "AbilitySystem/Unified/LegacyExtensionAbilityDriver.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Terrain/TerrainSurface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

FExtensionAbilityDriver::FExtensionAbilityDriver(AExtensionRuntime* Host) : Presentation(Host)
{
}

bool FExtensionAbilityDriver::IsFinished() const
{
    const auto* Host = Presentation.Get();
    return !IsValid(Host) || Host->bFinished;
}

bool FExtensionAbilityDriver::Emit(FName Event, UExtensionNode* Child, FVector P, FPureMassCombatTarget T,
                                   FVector EventCenter, bool ConfirmedHit)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    return RequestChild(Event, Child, P, Direction, T, Depth + 1, EventCenter, ConfirmedHit);
}

bool FExtensionAbilityDriver::Hit(FPureMassCombatTarget T, float Scale)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return false;
    }

    FVector P;
    if (Host->bFinished || !Host->HasAuthority() ||
        (!UExtensionWorldSubsystem::CasterAlive(Host->Context) || !Host->Context->ValidateSourceLife()) ||
        !Host->Context->ResolveTargetLife(T, P))
        return false;
    const float Damage = Host->ExecutionNode->DamageAtLevel(Host->Context->Level) * Scale;
    if (T.IsActor())
    {
        auto Params = Host->Context->DamageParams;
        Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(T.Actor.Get());
        if (!Params.SourceAbilitySystemComponent || !Params.TargetAbilitySystemComponent ||
            !Params.DamageGameplayEffectClass)
            return false;
        Params.WorldContextObject = Host;
        Params.BaseDamage = Damage;
        Params.DamageType = Host->ExecutionNode->DamageType;
        Params.bIsRadialDamage = false;
        Params.DebuffChance = 0;
        if (Damage > 0)
            UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    }
    else
    {
        FPureMassDamage D;
        D.Source = Host->Context->Caster;
        D.Amount = Damage;
        D.DamageType = Host->ExecutionNode->DamageType;
        Host->GetWorld()->GetSubsystem<UPureMassSubsystem>()->ApplyDamage(T.EntityId, D);
    }
    Host->Impact(P);
    // Snapshot before damage: a lethal hit may already have removed the entity.
    Emit(TEXT("OnHit"), Host->ExecutionNode->HitChild, P, T, Host->GetActorLocation(), true);
    return true;
}

void FExtensionAbilityDriver::ChainStep()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    FVector P;
    if (!Host->Context->ResolveTargetLife(Target, P) ||
        !PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), LastPosition, Target, Host))
    {
        Finish();
        return;
    }
    Host->BeamStart = LastPosition;
    Host->BeamEnd = P;
    HitTargets.Add(Target);
    Hit(Target, FMath::Pow(Host->ExecutionNode->DamageFalloff, Hits++));
    LastPosition = P;
    Host->ForceNetUpdate();
    if (Hits >= Host->ExecutionNode->Count)
    {
        Finish();
        return;
    }
    float Best = Host->ExecutionNode->Radius * Host->ExecutionNode->Radius;
    FPureMassCombatTarget Next;
    for (auto T : PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), P, Host->ExecutionNode->Radius))
    {
        FVector Q;
        if (HitTargets.Contains(T) || !Host->Context->ResolveTargetLife(T, Q))
            continue;
        float Dist = FVector::DistSquared(P, Q);
        if (Dist < Best && PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), P, T, Host))
        {
            Best = Dist;
            Next = T;
        }
    }
    Target = Next;
    if (!Target.IsSet())
        Finish();
}

void FExtensionAbilityDriver::Finish(bool Cancelled)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }

    if (Host->bFinished)
        return;
    Host->bFinished = true;
    if (!Cancelled && UExtensionWorldSubsystem::CasterAlive(Host->Context))
        Emit(TEXT("OnExpire"), Host->ExecutionNode->CompleteChild,
             Host->ExecutionNode->Behavior == EExtensionKind::Chain ? LastPosition : Host->GetActorLocation(), Target,
             Host->GetActorLocation());
    if (IsValid(Host->Surface))
        Host->Surface->Destroy();
    Host->Surface = nullptr;
    Host->RefreshVisual();
    Host->ForceNetUpdate();
    Host->SetLifeSpan(.35f);
}

void FExtensionAbilityDriver::Start()
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || !Host->Context || !Host->ExecutionNode)
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
    using K = EExtensionKind;
    if (Host->ExecutionNode->Behavior == K::Burst)
    {
        for (auto T : PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), Host->GetActorLocation(),
                                             Host->ExecutionNode->Radius))
            if (PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), Host->GetActorLocation() + FVector(0, 0, 2),
                                        T, Host))
                Hit(T);
        Finish();
    }
    else if (Host->ExecutionNode->Behavior == K::Stack)
    {
        auto* Subsystem = Host->GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
        if (Host->bExternallyDriven)
        {
            // The subsystem owns persistent stack state; the graph keeps its continuation alive until release.
            bAwaitingStack = true;
            TWeakPtr<FExtensionAbilityDriver> WeakDriver = AsShared();
            const bool bAccepted =
                Subsystem->AddStack(Host->Context, Host->ExecutionNode, Target, Host->GetActorLocation(), Direction,
                                    Depth, bConfirmedHit, ChildAdmission,
                                    [WeakDriver]()
                                    {
                                        if (const auto Driver = WeakDriver.Pin())
                                        {
                                            Driver->ReleaseStack();
                                        }
                                    });
            if (!bAccepted)
            {
                bAwaitingStack = false;
                Finish(true);
            }
        }
        else
        {
            Subsystem->AddStack(Host->Context, Host->ExecutionNode, Target, Host->GetActorLocation(), Direction, Depth,
                                bConfirmedHit);
            // Standalone compatibility callers keep the original immediate application lifetime.
            ReleaseStack();
        }
    }
    else if (Host->ExecutionNode->Behavior == K::Chain)
        ChainStep();
    else if (Host->ExecutionNode->Behavior == K::Attach)
    {
        if (!Host->Context->ResolveTargetLife(Target, LastPosition))
        {
            Finish(true);
            return;
        }
        Host->SetActorLocation(LastPosition);
        HitTargets.Add(Target);
    }
    else if (Host->ExecutionNode->Behavior == K::Displace)
    {
        auto* S = Host->GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
        auto Targets = Host->ExecutionNode->bSingleTarget
                           ? TArray<FPureMassCombatTarget>{Target}
                           : PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), Host->GetActorLocation(),
                                                    Host->ExecutionNode->Radius);
        for (auto T : Targets)
        {
            // Cursor ground hits lie on the collision surface; lift only the sight ray.
            if (!PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(),
                                         Host->GetActorLocation() + FVector(0, 0, 2), T, Host))
                continue;
            FGuid Token = S->RequestDisplacement(Host->Context, Host->ExecutionNode, T,
                                                 Host->ExecutionNode->bSingleTarget ? Center : Host->GetActorLocation(),
                                                 Direction);
            if (Token.IsValid())
                Moves.Add(Token);
        }
        if (Host->ExecutionNode->bSingleTarget && Moves.IsEmpty())
            Finish(true);
    }
    else if (Host->ExecutionNode->Behavior == K::Surface)
    {
        FTransform Transform(FRotator::ZeroRotator, Host->GetActorLocation());
        Host->Surface = Host->GetWorld()->SpawnActorDeferred<ATerrainSurface>(
            Host->ExecutionNode->SurfaceClass, Transform, Host->GetOwner(), Cast<APawn>(Host->GetOwner()),
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Host->Surface)
        {
            Finish(true);
            return;
        }
        Host->Surface->bOnlyAffectCasterEnemies = true;
        Host->Surface->bAffectPlayers = false;
        Host->Surface->bAffectEnemies = true;
        Host->Surface->SurfaceSize = FVector2D(Host->ExecutionNode->Radius * 2);
        Host->Surface->FinishSpawning(Transform);
        if (Host->Surface->Ground)
            Host->Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void FExtensionAbilityDriver::Advance(float Delta)
{
    auto* Host = Presentation.Get();
    if (!IsValid(Host))
    {
        return;
    }
    if (!Host->HasAuthority() || Host->bFinished || !Host->ExecutionNode)
        return;
    if ((!UExtensionWorldSubsystem::CasterAlive(Host->Context) || !Host->Context->ValidateSourceLife()))
    {
        Finish(true);
        return;
    }
    if (bAwaitingStack)
    {
        return;
    }
    using K = EExtensionKind;
    Delta = FMath::Max(0.f, Delta);
    const float ActiveDelta =
        FMath::Min(Delta, FMath::Max(0.f, Host->ExecutionNode->Duration - Age - 2 * KINDA_SMALL_NUMBER));
    Age += Delta;
    const bool bExpired = Age + KINDA_SMALL_NUMBER >= Host->ExecutionNode->Duration;
    if (Host->ExecutionNode->Behavior == K::Attach)
    {
        FVector P;
        if (!Host->Context->ResolveTargetLife(Target, P))
        {
            FPureMassCombatTarget Next;
            float Best = FMath::Square(Host->ExecutionNode->TransferRadius);
            if (Transfers < Host->ExecutionNode->MaxTransfers)
                for (auto T : PureMassCombat::Gather(Host->GetWorld(), Host->GetOwner(), LastPosition,
                                                     Host->ExecutionNode->TransferRadius))
                {
                    FVector Q;
                    if (HitTargets.Contains(T) || !Host->Context->ResolveTargetLife(T, Q))
                        continue;
                    float D = FVector::DistSquared(LastPosition, Q);
                    if (D < Best && PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), LastPosition, T, Host))
                    {
                        Best = D;
                        Next = T;
                    }
                }
            if (!Next.IsSet())
            {
                Finish(true);
                return;
            }
            Target = Next;
            HitTargets.Add(Next);
            ++Transfers;
            Host->Context->ResolveTargetLife(Target, P);
        }
        LastPosition = P;
        Host->SetActorLocation(P);
    }
    if (Host->ExecutionNode->Behavior == K::Displace && Host->ExecutionNode->bSingleTarget)
    {
        FVector P;
        auto* S = Host->GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
        if (!Host->Context->ResolveTargetLife(Target, P) || (!Moves.IsEmpty() && S->WasMoveReplaced(Moves[0])))
        {
            Finish(true);
            return;
        }
        Host->SetActorLocation(P);
        LastPosition = P;
    }
    if (Host->ExecutionNode->Behavior == K::Delay)
    {
        if (Age >= Host->ExecutionNode->ArmDelay)
            Finish();
        return;
    }
    if (Host->ExecutionNode->Behavior == K::Projectile)
    {
        float Step = FMath::Min(Host->ExecutionNode->Speed * ActiveDelta, Host->ExecutionNode->Range - Travel);
        FVector Start = Host->GetActorLocation(), End = Start + Direction * Step;
        FHitResult Wall;
        float Time = 1;
        auto T = PureMassCombat::Sweep(Host->GetWorld(), Host->GetOwner(), Start, End,
                                       Host->ExecutionNode->CollisionRadius, Time, Wall, nullptr, Host);
        if (T.IsSet())
        {
            Host->SetActorLocation(FMath::Lerp(Start, End, Time));
            Hit(T);
            Finish();
            return;
        }
        if (Wall.bBlockingHit)
        {
            Host->SetActorLocation(Wall.ImpactPoint);
            Finish();
            return;
        }
        Host->SetActorLocation(End);
        Travel += Step;
        if (Travel >= Host->ExecutionNode->Range)
            Finish();
    }
    if (Host->ExecutionNode->Behavior != K::Attach && Host->ExecutionNode->Behavior != K::Chain)
    {
        if (bExpired)
            Finish();
        return;
    }
    Clock += ActiveDelta;
    for (int32 I = 0; Clock + KINDA_SMALL_NUMBER >= Host->ExecutionNode->Period && I < 16 && !Host->bFinished; ++I)
    {
        Clock -= Host->ExecutionNode->Period;
        if (Host->ExecutionNode->Behavior == K::Chain)
            ChainStep();
        else if (PureMassCombat::Visible(Host->GetWorld(), Host->GetOwner(), Host->GetOwner()->GetActorLocation(),
                                         Target, Host))
            Emit(TEXT("OnPeriod"), Host->ExecutionNode->PeriodChild, LastPosition, Target, LastPosition);
    }
    Clock = FMath::Min(Clock, Host->ExecutionNode->Period);
    if (bExpired)
        Finish();
}

bool FExtensionAbilityDriver::RequestChild(FName Event, UExtensionNode* Child, FVector Position, FVector ChildDirection,
                                           FPureMassCombatTarget Candidate, int32 ChildDepth, FVector EventCenter,
                                           bool bConfirmed)
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
        return AExtensionRuntime::Spawn(Host->Context, Child, Position, ChildDirection, Candidate, ChildDepth,
                                        EventCenter, bConfirmed) != nullptr;
    }
    FLegacyAbilityChildRequest Request;
    Request.Event = Event;
    Request.Config = Child;
    Request.Context = Host->Context;
    Request.Position = Position;
    Request.Direction = ChildDirection;
    Request.Target = Candidate;
    Request.Depth = ChildDepth;
    Request.EventCenter = EventCenter;
    Request.bConfirmedHit = bConfirmed;
    return ChildAdmission(Request);
}

/** Completion was already routed by the stack owner; close only presentation and invocation lifetime. */
void FExtensionAbilityDriver::ReleaseStack()
{
    bAwaitingStack = false;
    auto* Host = Presentation.Get();
    if (!IsValid(Host) || Host->bFinished)
    {
        return;
    }
    Host->bFinished = true;
    Host->RefreshVisual();
    Host->ForceNetUpdate();
    Host->SetLifeSpan(.35f);
}
