// 调用链：图节点 -> 本服务 -> PureMassCombat 查询/可见性 -> AbilityExecutionIdentity 捕获实际生命。
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "GameFramework/Actor.h"

namespace AbilityQueryDetails
{
    /** 参数映射共用已有纯几何谓词；Generic 的每个精度/高度开关均保留原 Query.Shape 值。 */
    FAbilityShapeFilter Shape(const FAbilityQueryRequest& Request)
    {
        FAbilityShapeFilter Result;
        Result.Kind =
            Request.Geometry == EAbilityQueryGeometry::Cone   ? EAbilityBehaviorShape::Cone
            : Request.Geometry == EAbilityQueryGeometry::Line ? EAbilityBehaviorShape::Line
            : Request.Geometry == EAbilityQueryGeometry::Ring || Request.Geometry == EAbilityQueryGeometry::WaveFront
                ? EAbilityBehaviorShape::Ring
                : EAbilityBehaviorShape::Circle;
        Result.Radius = Request.Radius;
        Result.InnerRadius =
            Request.Geometry == EAbilityQueryGeometry::WaveFront ? Request.PreviousRadius : Request.InnerRadius;
        Result.Range = Request.Range;
        Result.Width = Request.Width;
        Result.HalfHeight = Request.HalfHeight;
        Result.SpreadDegrees = Request.HalfAngleDegrees * 2.f;
        if (Request.Profile == EAbilityShapeQueryProfile::Generic)
        {
            Result.AngleTolerance = KINDA_SMALL_NUMBER;
            Result.bUse3DDistance = Request.Geometry == EAbilityQueryGeometry::Sphere;
            Result.bRejectOutsideHeight = Request.Geometry != EAbilityQueryGeometry::Sphere;
            Result.DistanceMode = Request.Geometry == EAbilityQueryGeometry::Sphere
                                      ? EAbilityShapeDistanceMode::Squared
                                      : EAbilityShapeDistanceMode::FloatMagnitude;
            Result.bConeOriginUsesPlanarDistance = true;
            Result.bUseFloatLateralDistance = true;
            Result.bFallbackForward = false;
        }
        else
        {
            Result.AngleTolerance = Request.Profile == EAbilityShapeQueryProfile::LegacyActor &&
                                            Request.Geometry == EAbilityQueryGeometry::Cone
                                        ? KINDA_SMALL_NUMBER
                                        : 0.f;
            Result.bUse3DDistance =
                Request.Geometry == EAbilityQueryGeometry::Sphere || Request.Geometry == EAbilityQueryGeometry::Cone;
            Result.bRejectOutsideHeight =
                Request.Geometry == EAbilityQueryGeometry::Ring || Request.Geometry == EAbilityQueryGeometry::Line;
            Result.DistanceMode = EAbilityShapeDistanceMode::Squared;
            Result.bConeOriginUsesPlanarDistance = false;
            Result.bUseFloatLateralDistance = false;
            Result.bFallbackForward = true;
        }
        return Result;
    }

    /** 旧 broadphase 的 float 平方和顺序本身会改变边界候选，不能用 double Size 替换。 */
    float BroadphaseRadius(const FAbilityQueryRequest& Request)
    {
        if (Request.Profile == EAbilityShapeQueryProfile::Generic)
        {
            return Request.Geometry == EAbilityQueryGeometry::Line
                       ? FVector(Request.Range, Request.Width * .5f, Request.HalfHeight).Size()
                       : FVector(Request.Radius, 0.f, Request.HalfHeight).Size();
        }
        if (Request.Geometry == EAbilityQueryGeometry::Line)
        {
            return FMath::Sqrt(FMath::Square(Request.Range) + FMath::Square(Request.Width * .5f) +
                               FMath::Square(Request.HalfHeight));
        }
        if (Request.Geometry == EAbilityQueryGeometry::Ring)
        {
            return FMath::Sqrt(FMath::Square(Request.Radius) + FMath::Square(Request.HalfHeight));
        }
        return Request.Radius;
    }
} // namespace AbilityQueryDetails

bool FAbilityQueryRequest::Validate(FString& Error) const
{
    Error.Reset();
    const bool bKnownGeometry =
        Geometry >= EAbilityQueryGeometry::Sphere && Geometry <= EAbilityQueryGeometry::WaveFront;
    if (Profile < EAbilityShapeQueryProfile::Generic || Profile > EAbilityShapeQueryProfile::LegacyMixed ||
        (Profile != EAbilityShapeQueryProfile::Generic && Geometry == EAbilityQueryGeometry::WaveFront))
    {
        Error = TEXT("Query requires a registered shape profile; legacy wave behavior uses Spatial.Wavefront.");
        return false;
    }
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
    const FAbilityShapeFilter Shape = AbilityQueryDetails::Shape(Request);
    const float QueryRadius = AbilityQueryDetails::BroadphaseRadius(Request);
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
        const bool bInside =
            AbilityBehaviorPrimitives::ContainsPoint(Shape, Request.Origin, Request.Direction, Location);
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

bool FAbilityQueryRequest::ValidateForBatch(FString& Error) const
{
    if (MaxTargets < 1 || MaxTargets > 65536)
    {
        Error = TEXT("Complete batch query requires MaxTargets in 1..65536.");
        return false;
    }
    // 只替换集合上限检查；其余有限维度/方向/形状合同继续来自同一验证入口。
    FAbilityQueryRequest Dimensions = *this;
    Dimensions.MaxTargets = 1;
    return Dimensions.Validate(Error);
}

bool AbilityTargetQueryService::GatherBatch(const FAbilitySourceToken& Source, const FAbilityQueryRequest& Request,
                                            FAbilityQueryLifeResolver ResolveLife, TArray<FAbilityTargetToken>& Out,
                                            FString& Error)
{
    Out.Reset();
    AActor* Avatar = nullptr;
    if (!Request.ValidateForBatch(Error))
    {
        return false;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        Error = TEXT("Batch query source life is no longer valid.");
        return false;
    }
    const auto Shape = AbilityQueryDetails::Shape(Request);
    const float QueryRadius = AbilityQueryDetails::BroadphaseRadius(Request);
    if (!FMath::IsFinite(QueryRadius))
    {
        Error = TEXT("Batch query derived broadphase is not finite.");
        return false;
    }
    const bool bObserveBeforeFilter = Request.Profile != EAbilityShapeQueryProfile::Generic;
    for (const auto& Candidate : PureMassCombat::Gather(Source.World.Get(), Avatar, Request.Origin, QueryRadius))
    {
        if ((Candidate.IsActor() && !Request.bIncludeActors) || (!Candidate.IsActor() && !Request.bIncludeMass))
        {
            continue;
        }
        FAbilityTargetToken Token;
        FVector Location;
        auto Resolve = [&]()
        {
            FString CandidateError;
            // 回调只决定使用首次原生命还是 fresh 捕获；本服务仍复核它返回的当前目标证明。
            return ResolveLife(Candidate, Token, Location, CandidateError) && Token.Target == Candidate &&
                   AbilityExecutionIdentity::ResolveTarget(Source, Token, Location);
        };
        bool bResolved =
            bObserveBeforeFilter ? Resolve() : PureMassCombat::Resolve(Source.World.Get(), Avatar, Candidate, Location);
        if (bResolved && AbilityBehaviorPrimitives::ContainsPoint(Shape, Request.Origin, Request.Direction, Location) &&
            (!Request.bRequireVisibility ||
             PureMassCombat::Visible(Source.World.Get(), Avatar, Request.Origin, Candidate)))
        {
            bResolved = bObserveBeforeFilter || Resolve();
            if (bResolved)
            {
                if (Out.Num() == Request.MaxTargets)
                {
                    Out.Reset();
                    Error = TEXT("Complete batch query exceeded its actual target cap; no prefix was accepted.");
                    return false;
                }
                Out.Add(MoveTemp(Token));
            }
        }
        // 单个旧 lease 失效是正常空候选；来源在 resolver 中失效则关闭整个查询，不返回部分集合。
        AActor* CurrentAvatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Source, CurrentAvatar))
        {
            Out.Reset();
            Error = TEXT("Batch query source changed while resolving candidates.");
            return false;
        }
    }
    Error.Reset();
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
