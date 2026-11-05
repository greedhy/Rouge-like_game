#pragma once
#include "CoreMinimal.h"

namespace PureMassMovement
{
// Swept XY discs: stop before contact, but allow an already overlapping pair
// to separate. Z filtering belongs to the caller (different floors may overlap).
inline float ContactFraction(FVector Offset, FVector Delta, float Radius)
{
    Offset.Z = Delta.Z = 0;
    const double A = Delta.SizeSquared();
    const double B = FVector::DotProduct(Offset, Delta);
    const double C = Offset.SizeSquared() - FMath::Square(Radius);
    if (A < UE_SMALL_NUMBER || B >= 0) return 1.f;
    if (C <= 0) return 0.f;
    const double D = B*B - A*C;
    if (D < 0) return 1.f;
    return FMath::Clamp(float((-B - FMath::Sqrt(D))/A), 0.f, 1.f);
}

inline FVector Steer(FVector Forward, FVector Separation, bool bBlocked)
{
    Forward = Forward.GetSafeNormal2D();
    // Keep a common passing side for pursuing crowds. Do not normalize a
    // repulsion-dominated backward vector into full-speed reverse movement.
    const FVector Right(-Forward.Y, Forward.X, 0);
    const float Lateral = FMath::Clamp(float(FVector::DotProduct(Separation, Right)), -.6f, .6f);
    const float Advance = FMath::Clamp(1.f + float(FVector::DotProduct(Separation, Forward)), 0.f, 1.f);
    return (Forward*Advance + Right*(bBlocked ? .8f : Lateral)).GetClampedToMaxSize(1.f);
}

inline FQuat Turn(FQuat Current, FQuat Target, float DeltaSeconds)
{
    return FMath::RInterpConstantTo(Current.Rotator(), Target.Rotator(), DeltaSeconds, 240.f).Quaternion();
}
}
