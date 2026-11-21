// 调用链：行为驱动/图节点 -> 纯目标选择、形状筛选或节拍 -> 原驱动的投递/效果入口。
// 此模块没有复制状态和对象所有权；World/目标生命复核仍由现有调用者负责。
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"

bool AbilityBehaviorPrimitives::SameTargetLife(const FAbilityTargetToken& A, const FAbilityTargetToken& B)
{
    return A.Target.IsSet() && B.Target.IsSet() && A.World == B.World && A.Target == B.Target &&
           A.DungeonRunId == B.DungeonRunId && A.ActorLease == B.ActorLease && A.MassEntity == B.MassEntity;
}

TOptional<FAbilityTargetCandidate> AbilityBehaviorPrimitives::SelectNearest(
    const TArray<FAbilityTargetCandidate>& Candidates, FVector Origin, float MaxDistance,
    const TArray<FAbilityTargetToken>& Visited, EAbilityCandidateTieBreak TieBreak, bool bIncludeBoundary)
{
    if (Origin.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance < 0.f ||
        (TieBreak != EAbilityCandidateTieBreak::First && TieBreak != EAbilityCandidateTieBreak::Last))
    {
        return {};
    }
    TOptional<FAbilityTargetCandidate> Selected;
    float BestDistance = FMath::Square(MaxDistance);
    for (const auto& Candidate : Candidates)
    {
        if (!Candidate.Target.Target.IsSet() || Candidate.Location.ContainsNaN() ||
            Visited.ContainsByPredicate(
                [&](const FAbilityTargetToken& Earlier)
                {
                    return SameTargetLife(Earlier, Candidate.Target);
                }))
        {
            continue;
        }
        // 原 Actor/Mixed/Extension 最近距离比较均先存入 float；不在提取时提高精度改变同距胜者。
        const float Distance = FVector::DistSquared(Origin, Candidate.Location);
        if (!FMath::IsFinite(Distance) || Distance > FMath::Square(MaxDistance) ||
            (!bIncludeBoundary && Distance == FMath::Square(MaxDistance)))
        {
            continue;
        }
        if (!Selected.IsSet() || Distance < BestDistance ||
            (TieBreak == EAbilityCandidateTieBreak::Last && Distance == BestDistance))
        {
            Selected = Candidate;
            BestDistance = Distance;
        }
    }
    return Selected;
}

bool AbilityBehaviorPrimitives::ContainsPoint(const FAbilityShapeFilter& Filter, FVector Origin, FVector Forward,
                                              FVector Point)
{
    const bool bKnownShape = Filter.Kind >= EAbilityBehaviorShape::Circle && Filter.Kind <= EAbilityBehaviorShape::Line;
    const bool bKnownDistance = Filter.DistanceMode == EAbilityShapeDistanceMode::Squared ||
                                Filter.DistanceMode == EAbilityShapeDistanceMode::FloatMagnitude;
    // 只验证本形状使用的维度；旧圆形/扇形不应因无关的直线宽度或内圈值被提取代码拒绝。
    const bool bBadRadius =
        Filter.Kind != EAbilityBehaviorShape::Line && (!FMath::IsFinite(Filter.Radius) || Filter.Radius <= 0.f);
    const bool bBadRing =
        Filter.Kind == EAbilityBehaviorShape::Ring &&
        (!FMath::IsFinite(Filter.InnerRadius) || Filter.InnerRadius < 0.f || Filter.InnerRadius > Filter.Radius);
    const bool bBadLine =
        Filter.Kind == EAbilityBehaviorShape::Line && (!FMath::IsFinite(Filter.Range) || Filter.Range <= 0.f ||
                                                       !FMath::IsFinite(Filter.Width) || Filter.Width <= 0.f);
    const bool bBadCone =
        Filter.Kind == EAbilityBehaviorShape::Cone &&
        (!FMath::IsFinite(Filter.SpreadDegrees) || Filter.SpreadDegrees < 0.f || Filter.SpreadDegrees > 360.f ||
         !FMath::IsFinite(Filter.AngleTolerance) || Filter.AngleTolerance < 0.f);
    const bool bBadHeight =
        Filter.bRejectOutsideHeight && (!FMath::IsFinite(Filter.HalfHeight) || Filter.HalfHeight < 0.f);
    if (!bKnownShape || !bKnownDistance || Origin.ContainsNaN() || Forward.ContainsNaN() || Point.ContainsNaN() ||
        bBadRadius || bBadRing || bBadLine || bBadCone || bBadHeight)
    {
        return false;
    }
    const FVector Offset = Point - Origin;
    if (Filter.bRejectOutsideHeight && FMath::Abs(Offset.Z) > Filter.HalfHeight)
    {
        return false;
    }
    const double DistanceSquared = Filter.bUse3DDistance ? Offset.SizeSquared() : Offset.SizeSquared2D();
    const float Distance = Filter.bUse3DDistance ? Offset.Size() : Offset.Size2D();
    const bool bInsideOuter = Filter.DistanceMode == EAbilityShapeDistanceMode::FloatMagnitude
                                  ? Distance <= Filter.Radius
                                  : DistanceSquared <= FMath::Square(Filter.Radius);
    if (Filter.Kind == EAbilityBehaviorShape::Circle)
    {
        return bInsideOuter;
    }
    if (Filter.Kind == EAbilityBehaviorShape::Ring)
    {
        return bInsideOuter && (Filter.DistanceMode == EAbilityShapeDistanceMode::FloatMagnitude
                                    ? Distance >= Filter.InnerRadius
                                    : DistanceSquared >= FMath::Square(Filter.InnerRadius));
    }
    const FVector Facing =
        Forward.GetSafeNormal2D(SMALL_NUMBER, Filter.bFallbackForward ? FVector::ForwardVector : FVector::ZeroVector);
    if (Filter.Kind == EAbilityBehaviorShape::Cone)
    {
        const bool bAtOrigin = Filter.bConeOriginUsesPlanarDistance
                                   ? static_cast<float>(Offset.Size2D()) <= KINDA_SMALL_NUMBER
                                   : Offset.IsNearlyZero();
        return bInsideOuter &&
               (bAtOrigin || FVector::DotProduct(Facing, Offset.GetSafeNormal2D()) + Filter.AngleTolerance >=
                                 FMath::Cos(FMath::DegreesToRadians(Filter.SpreadDegrees * .5f)));
    }
    const float Along = FVector::DotProduct(Offset, Facing);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
    const double Across = FMath::Abs(FVector::DotProduct(Offset, Right));
    return Along >= 0.f && Along <= Filter.Range &&
           (Filter.bUseFloatLateralDistance ? static_cast<float>(Across) <= Filter.Width * .5f
                                            : Across <= Filter.Width * .5f);
}

bool AbilityBehaviorPrimitives::ContainsWavePoint(FVector Origin, FVector Point, float PreviousRadius,
                                                  float CurrentRadius, EAbilityWaveCoverage Coverage)
{
    if (Origin.ContainsNaN() || Point.ContainsNaN() || !FMath::IsFinite(PreviousRadius) ||
        !FMath::IsFinite(CurrentRadius) || PreviousRadius < 0.f || CurrentRadius < PreviousRadius)
    {
        return false;
    }
    if (Coverage == EAbilityWaveCoverage::GrowingSphere3D)
    {
        // 普通 Mixed 原来直接比较 double 距离平方与 float 半径平方。
        return FVector::DistSquared(Origin, Point) <= FMath::Square(CurrentRadius);
    }
    if (Coverage == EAbilityWaveCoverage::SweptShell3D)
    {
        // 原 Actor / Batch6 前沿先把三维距离转成 float，保留其接触边界。
        const float Distance = FVector::Distance(Origin, Point);
        return Distance <= CurrentRadius && (PreviousRadius == 0.f || Distance > PreviousRadius);
    }
    return false;
}

int32 AbilityBehaviorPrimitives::AdvanceCadence(float& Clock, float DeltaSeconds, float Period, int32 MaxPulses,
                                                EAbilityCadenceRemainder Remainder, TFunctionRef<bool()> Pulse,
                                                float BoundaryTolerance, bool bClampClockAtZero)
{
    if (!FMath::IsFinite(Clock) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f || !FMath::IsFinite(Period) ||
        Period <= 0.f || MaxPulses <= 0 || !FMath::IsFinite(BoundaryTolerance) || BoundaryTolerance < 0.f ||
        (Remainder != EAbilityCadenceRemainder::Preserve && Remainder != EAbilityCadenceRemainder::ClampToPeriod &&
         Remainder != EAbilityCadenceRemainder::DiscardAfterPulse) ||
        !FMath::IsFinite(Clock + DeltaSeconds))
    {
        return 0;
    }
    Clock += DeltaSeconds;
    int32 Consumed = 0;
    while (Consumed < MaxPulses && Clock + BoundaryTolerance >= Period)
    {
        Clock = bClampClockAtZero ? FMath::Max(0.f, Clock - Period) : Clock - Period;
        ++Consumed;
        // 回调可能同步取消/替换所属执行；不预先计算全部周期，也不在停止后再执行下一次回调。
        const bool bContinue = Pulse();
        if (Remainder == EAbilityCadenceRemainder::DiscardAfterPulse)
        {
            Clock = 0.f;
            break;
        }
        if (!bContinue)
        {
            break;
        }
    }
    if (Remainder == EAbilityCadenceRemainder::ClampToPeriod)
    {
        Clock = FMath::Min(Clock, Period);
    }
    return Consumed;
}
