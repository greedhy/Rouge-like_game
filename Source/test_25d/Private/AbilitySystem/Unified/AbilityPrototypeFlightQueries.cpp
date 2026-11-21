// 调用链：原型分段 -> Actor/Mixed 原几何查询 -> 无伤害接触事实；原生命由调用方账本提供。
#include "AbilitySystem/Unified/AbilityPrototypeFlightQueries.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "Character/BaseCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Interface/CombatInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace AbilityPrototypeFlightQueries
{
    bool GatherActorSegment(const FAbilitySourceToken& Source, const FVector& Start, const FVector& End, float Radius,
                            const TSet<FPureMassCombatTarget>& Ignored,
                            AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                            TArray<FAbilityPrototypeFlightQueryHit>& Out, FString& Error, AActor* IgnoredEffect)
    {
        Out.Reset();
        Error.Reset();
        AActor* Avatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar) || Start.ContainsNaN() || End.ContainsNaN() ||
            !FMath::IsFinite(Radius) || Radius <= 0.f)
        {
            Error = TEXT("Actor flight query requires original source life and a finite segment");
            return false;
        }
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeFlight), false, Avatar);
        Query.AddIgnoredActor(IgnoredEffect);
        for (const auto& Previous : Ignored)
        {
            if (Previous.Actor.IsValid())
            {
                Query.AddIgnoredActor(Previous.Actor.Get());
            }
        }
        auto CaptureActor = [&](AActor* Actor, FAbilityTargetToken& Token)
        {
            if (!APrototypeRuntime::IsEnemy(Avatar, Actor))
            {
                return false;
            }
            FVector Location;
            FString CaptureError;
            return ResolveLife(FPureMassCombatTarget::FromActor(Actor), Token, Location, CaptureError);
        };
        // Preserve the original pre-sweep observation of every BaseCharacter, including out-of-segment enemies.
        for (TActorIterator<ABaseCharacter> It(Avatar->GetWorld()); It; ++It)
        {
            FAbilityTargetToken Token;
            if (!CaptureActor(*It, Token))
            {
                Query.AddIgnoredActor(*It);
            }
        }
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_Pawn);
        Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        TArray<FHitResult> Hits;
        Avatar->GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
                                                   FCollisionShape::MakeSphere(Radius), Query);
        Hits.Sort(
            [](const FHitResult& A, const FHitResult& B)
            {
                return A.Time < B.Time;
            });
        for (const auto& Hit : Hits)
        {
            FAbilityPrototypeFlightQueryHit Result;
            Result.Hit = Hit;
            Out.Add(MoveTemp(Result));
        }
        return true;
    }

    bool ClassifyActorContact(const FAbilitySourceToken& Source, const FHitResult& Hit,
                              AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                              FAbilityPrototypeFlightQueryHit& Out, AActor* IgnoredEffect)
    {
        Out = {};
        auto* Actor = Hit.GetActor();
        auto* Avatar = Source.Avatar.Get();
        if (Actor == Avatar || Actor == IgnoredEffect || Cast<APrototypeRuntime>(Actor))
        {
            return false;
        }
        FAbilityTargetToken Token;
        FVector Location;
        FString Error;
        const bool bLive = APrototypeRuntime::IsEnemy(Avatar, Actor) &&
                           ResolveLife(FPureMassCombatTarget::FromActor(Actor), Token, Location, Error);
        if ((IsValid(Actor) && Actor->Implements<UCombatInterface>() && !bLive) ||
            (!bLive &&
             (!Hit.GetComponent() || Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)))
        {
            return false;
        }
        Out.Hit = Hit;
        if (bLive)
        {
            Out.Candidate = FPureMassCombatTarget::FromActor(Actor);
            Out.Life = Token;
        }
        return true;
    }

    bool SweepMixedSegment(const FAbilitySourceToken& Source, const FVector& Start, const FVector& End, float Radius,
                           const TSet<FPureMassCombatTarget>& Ignored,
                           AbilityTargetQueryService::FAbilityQueryLifeResolver ResolveLife,
                           FAbilityPrototypeFlightQueryHit& Out, FHitResult& OutWall, FString& Error,
                           AActor* IgnoredEffect)
    {
        Out = {};
        OutWall = {};
        Error.Reset();
        AActor* Avatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar) || Start.ContainsNaN() || End.ContainsNaN() ||
            !FMath::IsFinite(Radius) || Radius <= 0.f)
        {
            Error = TEXT("Mixed flight query requires original source life and a finite segment");
            return false;
        }
        float Time = 1.f;
        Out.Candidate = PureMassCombat::Sweep(Avatar->GetWorld(), Avatar, Start, End, Radius, Time, OutWall, &Ignored,
                                              IgnoredEffect);
        if (Out.Candidate.IsSet())
        {
            Out.Hit.Time = Time;
            Out.Hit.Location = Out.Hit.ImpactPoint = FMath::Lerp(Start, End, Time);
            FAbilityTargetToken Token;
            FVector Location;
            FString CaptureError;
            const bool bLive = ResolveLife(Out.Candidate, Token, Location, CaptureError);
            // A rejected replacement still consumes the old Mixed collision admission; it must never be recaptured.
            if (bLive || Token.Target.IsSet())
            {
                Out.Life = Token;
            }
        }
        return true;
    }
} // namespace AbilityPrototypeFlightQueries
