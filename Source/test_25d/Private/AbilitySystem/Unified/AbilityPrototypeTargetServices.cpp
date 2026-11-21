// 调用链：原型目标节点/旧 Driver -> 目标、费用及节拍服务；本文件不结算效果或控制图执行。
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "Character/BaseCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace AbilityPrototypeTargetServices
{
    bool ActorLineOfSight(UWorld* World, AActor* Caster, AActor* Target, const FVector& Origin, bool bBeam,
                          AActor* IgnoredEffect)
    {
        if (!World || !IsValid(Target) || Origin.ContainsNaN())
        {
            return false;
        }
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeTargetLOS), false, Caster);
        if (IgnoredEffect)
        {
            Query.AddIgnoredActor(IgnoredEffect);
        }
        if (!bBeam)
        {
            for (TActorIterator<ABaseCharacter> It(World); It; ++It)
            {
                if (*It != Target)
                {
                    Query.AddIgnoredActor(*It);
                }
            }
        }
        FHitResult Obstruction;
        return !World->LineTraceSingleByChannel(Obstruction, Origin, Target->GetActorLocation(), ECC_Visibility,
                                                Query) ||
               Obstruction.GetActor() == Target;
    }

    bool HasVisibility(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target, const FVector& Origin,
                       EAbilityPrototypeTargetProfile Profile, bool bBeam)
    {
        AActor* Avatar = nullptr;
        FVector Position;
        if (Origin.ContainsNaN() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
            !AbilityExecutionIdentity::ResolveTarget(Source, Target, Position))
        {
            return false;
        }
        if (Profile == EAbilityPrototypeTargetProfile::Mixed)
        {
            return PureMassCombat::Visible(Avatar->GetWorld(), Avatar, Origin, Target.Target);
        }
        AActor* Victim = Target.Target.Actor.Get();
        if (Profile != EAbilityPrototypeTargetProfile::Actor || !IsValid(Victim))
        {
            return false;
        }
        return ActorLineOfSight(Avatar->GetWorld(), Avatar, Victim, Origin, bBeam);
    }

    bool ResolveBeam(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target, float Range,
                     EAbilityPrototypeTargetProfile Profile, FVector& SourcePoint, FVector& TargetPoint)
    {
        AActor* Avatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
            !AbilityExecutionIdentity::ResolveTarget(Source, Target, TargetPoint))
        {
            return false;
        }
        SourcePoint = Avatar->GetActorLocation() + FVector(0, 0, 30);
        return FVector::DistSquared(Avatar->GetActorLocation(), TargetPoint) <= FMath::Square(Range) &&
               HasVisibility(Source, Target, SourcePoint, Profile, true);
    }

    bool ResolveChainContact(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                             const FVector& Origin, float Reach, EAbilityPrototypeTargetProfile Profile,
                             FVector& TargetPoint)
    {
        return AbilityExecutionIdentity::ResolveTarget(Source, Target, TargetPoint) &&
               FVector::DistSquared(Origin, TargetPoint) <= FMath::Square(Reach) &&
               HasVisibility(Source, Target, Origin, Profile, false);
    }

    bool SelectChainNext(const FAbilitySourceToken& Source, const FVector& Origin, float Radius,
                         EAbilityPrototypeTargetProfile Profile, const TArray<FPureMassCombatTarget>& Visited,
                         AbilityTargetQueryService::FAbilityQueryLifeResolver Resolver, FAbilityTargetToken& Target,
                         FVector& TargetPoint, FString& Error)
    {
        Error.Reset();
        Target = {};
        AActor* Avatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
        {
            Error = TEXT("Chain selection lost its original source life");
            return false;
        }
        TArray<FPureMassCombatTarget> Candidates;
        if (Profile == EAbilityPrototypeTargetProfile::Actor)
        {
            TArray<AActor*> Actors;
            UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Actors, {Avatar}, Radius, Origin);
            for (auto* Actor : Actors)
            {
                Candidates.Add(FPureMassCombatTarget::FromActor(Actor));
            }
        }
        else if (Profile == EAbilityPrototypeTargetProfile::Mixed)
        {
            Candidates = PureMassCombat::Gather(Avatar->GetWorld(), Avatar, Origin, Radius);
        }
        else
        {
            Error = TEXT("Unknown chain target profile");
            return false;
        }
        TArray<FAbilityTargetCandidate> Eligible;
        for (const auto& Candidate : Candidates)
        {
            if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
            {
                Error = TEXT("Chain selection source changed during query");
                return false;
            }
            if (Profile == EAbilityPrototypeTargetProfile::Mixed && Visited.Contains(Candidate))
            {
                continue;
            }
            FAbilityTargetToken Life;
            FVector Location;
            FString CaptureError;
            if (!Resolver(Candidate, Life, Location, CaptureError) || Visited.Contains(Candidate))
            {
                continue;
            }
            // Resolver has just validated the original life. Preserve the old LOS call count/order after it.
            const bool bVisible =
                Profile == EAbilityPrototypeTargetProfile::Actor
                    ? ActorLineOfSight(Avatar->GetWorld(), Avatar, Candidate.Actor.Get(), Origin, false)
                    : PureMassCombat::Visible(Avatar->GetWorld(), Avatar, Origin, Candidate);
            if (!bVisible)
            {
                continue;
            }
            Eligible.Add({Life, Location});
        }
        // Keep the old float-distance threshold and candidate order. A generic double sphere prefilter changes ties.
        const auto Selected =
            AbilityBehaviorPrimitives::SelectNearest(Eligible, Origin, Radius, {}, EAbilityCandidateTieBreak::Last);
        if (Selected.IsSet())
        {
            Target = Selected->Target;
            TargetPoint = Selected->Location;
        }
        return true;
    }

    float ManaDue(float& WholeSecondClock, float Delta, float ManaPerSecond, EAbilityPrototypeTargetProfile Profile)
    {
        if (Profile == EAbilityPrototypeTargetProfile::Mixed)
        {
            return ManaPerSecond * Delta;
        }
        WholeSecondClock += Delta;
        if (WholeSecondClock < 1.f)
        {
            return 0.f;
        }
        const int32 Seconds = FMath::FloorToInt(WholeSecondClock);
        WholeSecondClock -= Seconds;
        return ManaPerSecond * Seconds;
    }

    int32 AdvanceBeamClock(float& Clock, float Delta, float Period, EAbilityPrototypeTargetProfile Profile,
                           TFunctionRef<bool()> Pulse)
    {
        if (Profile == EAbilityPrototypeTargetProfile::Mixed)
        {
            return AbilityBehaviorPrimitives::AdvanceCadence(Clock, Delta, Period, 8,
                                                             EAbilityCadenceRemainder::Preserve, Pulse);
        }
        Clock += Delta;
        if (Clock < Period)
        {
            return 0;
        }
        Clock = FMath::Fmod(Clock, Period);
        Pulse();
        return 1;
    }

    int32 AdvanceChainClock(float& Clock, float Delta, float Period, bool bFirstContact,
                            EAbilityPrototypeTargetProfile Profile, TFunctionRef<bool()> Contact)
    {
        if (bFirstContact)
        {
            if (Profile == EAbilityPrototypeTargetProfile::Mixed)
            {
                // The old mixed first-step path cleared the clock after its synchronous contact callbacks.
                Contact();
                Clock = 0.f;
            }
            else
            {
                Clock = 0.f;
                Contact();
            }
            return 1;
        }
        if (Profile == EAbilityPrototypeTargetProfile::Mixed)
        {
            return AbilityBehaviorPrimitives::AdvanceCadence(Clock, Delta, Period, 8,
                                                             EAbilityCadenceRemainder::Preserve, Contact);
        }
        Clock += Delta;
        if (Clock < Period)
        {
            return 0;
        }
        Clock = 0.f;
        Contact();
        return 1;
    }
} // namespace AbilityPrototypeTargetServices
