#include "PureMass/PureMassTypes.h"

bool PureMassGeometry::SweepCapsule(const FVector& Start, const FVector& End, float SweepRadius, const FVector& Center,
                                    float Radius, float HalfHeight, float& OutTime)
{
    OutTime = 1.f;
    if (Start.ContainsNaN() || End.ContainsNaN() || Center.ContainsNaN() || !FMath::IsFinite(SweepRadius) ||
        !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || SweepRadius < 0 || Radius <= 0 ||
        HalfHeight < Radius)
        return false;
    const double R = Radius + SweepRadius;
    const double H = HalfHeight - Radius;
    const FVector P = Start - Center;
    const FVector D = End - Start;
    const FVector Closest(0, 0, FMath::Clamp(P.Z, -H, H));
    if (FVector::DistSquared(P, Closest) <= R * R)
    {
        OutTime = 0;
        return true;
    }
    bool bHit = false;
    auto Accept = [&](double T)
    {
        if (T >= 0 && T <= OutTime && T <= 1)
        {
            OutTime = float(T);
            bHit = true;
        }
    };
    const double A = D.X * D.X + D.Y * D.Y;
    const double B = P.X * D.X + P.Y * D.Y;
    const double C = P.X * P.X + P.Y * P.Y - R * R;
    const double Disc = B * B - A * C;
    if (A > UE_SMALL_NUMBER && Disc >= 0)
    {
        const double T = (-B - FMath::Sqrt(Disc)) / A;
        if (FMath::Abs(P.Z + T * D.Z) <= H)
            Accept(T);
    }
    const double LengthSquared = D.SizeSquared();
    if (LengthSquared > UE_SMALL_NUMBER)
        for (const double Z : {-H, H})
        {
            const FVector Offset = P - FVector(0, 0, Z);
            const double Dot = FVector::DotProduct(Offset, D);
            const double Discriminant = Dot * Dot - LengthSquared * (Offset.SizeSquared() - R * R);
            if (Discriminant >= 0)
                Accept((-Dot - FMath::Sqrt(Discriminant)) / LengthSquared);
        }
    return bHit;
}
