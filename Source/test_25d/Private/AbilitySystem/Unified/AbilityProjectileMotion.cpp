// 调用链：原驱动/图投递 -> 纯函数分段与准入；参数/状态属于调用者，不生成对象或结算伤害。
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"

bool AbilityProjectileMotion::ComputeFlightSegment(const FAbilityProjectileMotionConfig& Config,
                                                   const FAbilityProjectileMotionState& State, float DeltaSeconds,
                                                   FVector SourcePosition, const TOptional<FVector>& TargetPosition,
                                                   FAbilityFlightSegment& OutSegment)
{
    OutSegment = {};
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f || !FMath::IsFinite(Config.Speed) || Config.Speed <= 0.f ||
        !FMath::IsFinite(Config.Range) || Config.Range <= 0.f || !FMath::IsFinite(Config.CollisionRadius) ||
        Config.CollisionRadius <= 0.f || !FMath::IsFinite(State.OutboundDistance) || State.OutboundDistance < 0.f ||
        State.Position.ContainsNaN() || State.Direction.ContainsNaN() || SourcePosition.ContainsNaN() ||
        State.Direction.IsNearlyZero() || Config.Motion == EAbilityProjectileMotion::NativeHoming)
    {
        return false;
    }
    FVector Direction = State.Direction.GetSafeNormal();
    float Remaining = FMath::Max(0.f, Config.Range - State.OutboundDistance);
    const bool bReturning =
        Config.Motion == EAbilityProjectileMotion::Returning && State.Phase == EAbilityProjectilePhase::Returning;
    if (bReturning)
    {
        Remaining = FVector::Distance(State.Position, SourcePosition);
        Direction = (SourcePosition - State.Position).GetSafeNormal();
    }
    else if (Config.Motion == EAbilityProjectileMotion::LegacyTurnHoming)
    {
        if (!TargetPosition.IsSet() || TargetPosition->ContainsNaN() || !FMath::IsFinite(Config.HomingTurnRate) ||
            Config.HomingTurnRate <= 0.f)
        {
            return false;
        }
        Direction =
            FMath::VInterpNormalRotationTo(Direction, (TargetPosition.GetValue() - State.Position).GetSafeNormal(),
                                           DeltaSeconds, Config.HomingTurnRate);
    }
    OutSegment.Start = State.Position;
    OutSegment.Direction = Direction;
    if (bReturning && Remaining <= Config.CollisionRadius)
    {
        OutSegment.End = State.Position;
        OutSegment.bReachesPhaseEnd = true;
        OutSegment.RemainingSeconds = DeltaSeconds;
        return true;
    }
    const float Distance = FMath::Min(Config.Speed * DeltaSeconds, Remaining);
    OutSegment.End = State.Position + Direction * Distance;
    OutSegment.SecondsUsed = Distance / Config.Speed;
    OutSegment.RemainingSeconds = FMath::Max(0.f, DeltaSeconds - OutSegment.SecondsUsed);
    OutSegment.bReachesPhaseEnd = Distance >= Remaining;
    return true;
}

FAbilityImpactAdmission AbilityProjectileMotion::AdmitImpact(EAbilityProjectileImpactPolicy Policy,
                                                             EAbilityProjectilePhase Phase, int32 UniqueOrdinal,
                                                             bool bAlreadyVisited, int32 Limit, float Falloff)
{
    FAbilityImpactAdmission Result;
    if (bAlreadyVisited || UniqueOrdinal < 0 || Limit <= 0 || UniqueOrdinal >= Limit || !FMath::IsFinite(Falloff) ||
        Falloff < 0.f || Falloff > 1.f)
    {
        return Result;
    }
    Result.bAccepted = true;
    Result.bTerminateAfterImpact = Policy == EAbilityProjectileImpactPolicy::StopFirst ||
                                   (Policy == EAbilityProjectileImpactPolicy::Pierce && UniqueOrdinal + 1 >= Limit);
    Result.DamageScale = Policy == EAbilityProjectileImpactPolicy::ReturnPerPhase
                             ? (Phase == EAbilityProjectilePhase::Returning ? Falloff : 1.f)
                             : FMath::Pow(Falloff, UniqueOrdinal);
    return Result;
}
