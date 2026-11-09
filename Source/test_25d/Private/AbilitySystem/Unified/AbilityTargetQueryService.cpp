// 调用链：图节点 -> 本服务 -> PureMassCombat 查询/可见性 -> AbilityExecutionIdentity 捕获实际生命。
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"
#include "GameFramework/Actor.h"

bool FAbilityQueryRequest::Validate(FString& Error) const
{
    Error.Reset();
    const bool bKnownGeometry =
        Geometry >= EAbilityQueryGeometry::Sphere && Geometry <= EAbilityQueryGeometry::WaveFront;
    if (!bKnownGeometry || Origin.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsFinite(Radius) ||
        !FMath::IsFinite(InnerRadius) || !FMath::IsFinite(Range) || !FMath::IsFinite(Width) ||
        !FMath::IsFinite(HalfHeight) || !FMath::IsFinite(HalfAngleDegrees) || !FMath::IsFinite(PreviousRadius) ||
        Radius <= 0.f || Range <= 0.f || Width <= 0.f || HalfHeight <= 0.f || InnerRadius < 0.f ||
        PreviousRadius < 0.f || HalfAngleDegrees < 0.f || HalfAngleDegrees > 180.f || MaxTargets < 1 ||
        MaxTargets > 256 || (!bIncludeActors && !bIncludeMass))
    {
        Error = TEXT("Query requires finite positive dimensions, supported geometry/targets, and MaxTargets 1..256.");
        return false;
    }
    if ((Geometry == EAbilityQueryGeometry::Ring && InnerRadius > Radius) ||
        (Geometry == EAbilityQueryGeometry::WaveFront && PreviousRadius > Radius) ||
        ((Geometry == EAbilityQueryGeometry::Cone || Geometry == EAbilityQueryGeometry::Line) &&
         Direction.IsNearlyZero()))
    {
        Error = TEXT("Query inner boundary must not exceed its outer boundary; oriented queries require a direction.");
        return false;
    }
    return true;
}

bool AbilityTargetQueryService::Gather(const FAbilitySourceToken& Source, const FAbilityQueryRequest& Request,
                                       TArray<FAbilityTargetToken>& Out, FString& Error)
{
    Out.Reset();
    AActor* Avatar = nullptr;
    if (!Request.Validate(Error))
    {
        return false;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        Error = TEXT("Query source life is no longer valid.");
        return false;
    }
    const FVector Forward = Request.Direction.GetSafeNormal2D();
    const float QueryRadius = Request.Geometry == EAbilityQueryGeometry::Line
                                  ? FVector(Request.Range, Request.Width * .5f, Request.HalfHeight).Size()
                                  : FVector(Request.Radius, 0.f, Request.HalfHeight).Size();
    const auto Candidates = PureMassCombat::Gather(Source.World.Get(), Avatar, Request.Origin, QueryRadius);
    for (const FPureMassCombatTarget& Candidate : Candidates)
    {
        if ((Candidate.IsActor() && !Request.bIncludeActors) || (!Candidate.IsActor() && !Request.bIncludeMass))
        {
            continue;
        }
        FVector Location;
        if (!PureMassCombat::Resolve(Source.World.Get(), Avatar, Candidate, Location))
        {
            continue;
        }
        const FVector Offset = Location - Request.Origin;
        const float PlanarDistance = Offset.Size2D();
        bool bInside = false;
        switch (Request.Geometry)
        {
        case EAbilityQueryGeometry::Sphere:
            bInside = Offset.SizeSquared() <= FMath::Square(Request.Radius);
            break;
        case EAbilityQueryGeometry::Cone:
            bInside = PlanarDistance <= Request.Radius && FMath::Abs(Offset.Z) <= Request.HalfHeight &&
                      (PlanarDistance <= KINDA_SMALL_NUMBER ||
                       FVector::DotProduct(Forward, Offset.GetSafeNormal2D()) + KINDA_SMALL_NUMBER >=
                           FMath::Cos(FMath::DegreesToRadians(Request.HalfAngleDegrees)));
            break;
        case EAbilityQueryGeometry::Ring:
        case EAbilityQueryGeometry::WaveFront:
        {
            const float Inner =
                Request.Geometry == EAbilityQueryGeometry::Ring ? Request.InnerRadius : Request.PreviousRadius;
            bInside = PlanarDistance >= Inner && PlanarDistance <= Request.Radius &&
                      FMath::Abs(Offset.Z) <= Request.HalfHeight;
            break;
        }
        case EAbilityQueryGeometry::Line:
        {
            const float Along = FVector::DotProduct(Offset, Forward);
            const float Across = FMath::Abs(Offset.X * Forward.Y - Offset.Y * Forward.X);
            bInside = Along >= 0.f && Along <= Request.Range && Across <= Request.Width * .5f &&
                      FMath::Abs(Offset.Z) <= Request.HalfHeight;
            break;
        }
        }
        if (!bInside || (Request.bRequireVisibility &&
                         !PureMassCombat::Visible(Source.World.Get(), Avatar, Request.Origin, Candidate)))
        {
            continue;
        }
        FAbilityTargetToken Token;
        FString CaptureError;
        if (AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Token, CaptureError))
        {
            Out.Add(MoveTemp(Token));
            if (Out.Num() == Request.MaxTargets)
            {
                break;
            }
        }
    }
    return true;
}

bool AbilityTargetQueryService::Sweep(const FAbilitySourceToken& Source, const FAbilitySweepRequest& Request,
                                      FAbilityQueryHit& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    AActor* Avatar = nullptr;
    if (Request.Start.ContainsNaN() || Request.End.ContainsNaN() || !FMath::IsFinite(Request.Radius) ||
        Request.Radius <= 0.f || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        Error = TEXT("Sweep requires a current source life and finite positive geometry.");
        return false;
    }
    const auto Candidate = PureMassCombat::Sweep(Source.World.Get(), Avatar, Request.Start, Request.End, Request.Radius,
                                                 Out.Time, Out.Wall, &Request.Ignored, Request.IgnoredEffect.Get());
    Out.ImpactPoint = Out.Wall.bBlockingHit ? Out.Wall.ImpactPoint : Request.End;
    if (Candidate.IsSet())
    {
        FAbilityTargetToken Token;
        if (AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Token, Error))
        {
            Out.Target = MoveTemp(Token);
            Out.ImpactPoint = FMath::Lerp(Request.Start, Request.End, Out.Time);
        }
    }
    return true;
}
