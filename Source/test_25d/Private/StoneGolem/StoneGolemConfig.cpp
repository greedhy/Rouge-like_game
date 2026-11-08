#include "StoneGolem/StoneGolemConfig.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"

bool FStoneGolemAttackTuning::Validate(FString& Error) const
{
    auto Fail = [&Error](const TCHAR* Text)
    {
        Error = Text;
        return false;
    };
    for (float Value : {Radius, Angle, HeightTolerance, AnimationDelay, LockTime, PlayRate, ContactTime,
                        ContactTolerance, RecoveryEnd, DamageMultiplier, Cooldown, KnockbackSpeed, KnockbackUpSpeed,
                        Length, Width, ActivationRange, SelectionWeight, ImpactScale, ImpactLifetime})
        if (!FMath::IsFinite(Value))
            return Fail(TEXT("Attack numbers must be finite"));
    if (CenterOffset.ContainsNaN() || HeadOffset.ContainsNaN())
        return Fail(TEXT("Offsets must be finite"));
    if (Radius <= 0 || Radius > 2000 || HeightTolerance <= 0 || HeightTolerance > 200 || Angle <= 0 || Angle > 360 ||
        Length <= 0 || Length > 2000 || Width <= 0 || Width > 2000 || KnockbackUpSpeed < 0)
        return Fail(TEXT("Invalid shape dimensions"));
    if (PlayRate <= 0 || PlayRate > 3 || AnimationDelay < 0 || LockTime < 0 || ContactTime <= 0 ||
        ContactTolerance <= 0 || ContactTolerance > .1f)
        return Fail(TEXT("Invalid timing"));
    if (HitTime() < .3f || LockTime >= HitTime() || RecoveryEnd < HitTime() || Cooldown <= 0 || DamageMultiplier < 0 ||
        ActivationRange <= 0 || SelectionWeight <= 0 || KnockbackSpeed < 0 || ImpactScale <= 0 || ImpactLifetime <= 0)
        return Fail(TEXT("Invalid reaction, recovery, cooldown or combat values"));
    if (!Montage || Montage->GetPlayLength() <= ContactTime + ContactTolerance ||
        RecoveryEnd < AnimationDelay + Montage->GetPlayLength() / PlayRate || !WarningMaterial)
        return Fail(TEXT("Missing presentation or montage/recovery timing mismatch"));
    return true;
}
bool FStoneGolemAttackTuning::Contains(const FVector& Center, float Yaw, const FVector& Point) const
{
    if (Point.ContainsNaN() || Center.ContainsNaN() || !FMath::IsFinite(Yaw))
        return false;
    const FVector Delta = Point - Center;
    if (Shape == EStoneGolemShape::Rectangle)
    {
        const FVector Local = FRotator(0, Yaw, 0).UnrotateVector(Delta);
        return FMath::Abs(Local.Z) <= HeightTolerance && Local.X >= -1.e-4 && Local.X <= Length + 1.e-4 &&
               FMath::Abs(Local.Y) <= Width * .5f + 1.e-4;
    }
    // BuildSurface samples the exact rim using trig; roundoff must not cull alternate rim cells.
    if (FMath::Abs(Delta.Z) > HeightTolerance || Delta.SizeSquared2D() > FMath::Square(Radius) + 1.e-4)
        return false;
    return Shape == EStoneGolemShape::Circle || Delta.SizeSquared2D() < KINDA_SMALL_NUMBER ||
           FVector::DotProduct(FRotator(0, Yaw, 0).Vector(), Delta.GetSafeNormal2D()) >=
               FMath::Cos(FMath::DegreesToRadians(Angle * .5f)) - 1.e-6f;
}
#if WITH_EDITOR
EDataValidationResult UStoneGolemAttackConfig::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if (!Tuning.Validate(Error))
    {
        Context.AddError(FText::FromString(Error));
        return EDataValidationResult::Invalid;
    }
    return EDataValidationResult::Valid;
}
#endif
bool StoneGolemGeometry::Project(UWorld* World, const AActor* Source, const FVector& Candidate, float Height,
                                 FVector& Ground)
{
    if (!World)
        return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(StoneGolemFloor), false, Source);
    const bool Found =
        World->LineTraceSingleByObjectType(Hit, Candidate + FVector(0, 0, Height), Candidate - FVector(0, 0, Height),
                                           FCollisionObjectQueryParams(ECC_WorldStatic), Params);
    if (!Found || Hit.ImpactNormal.Z < .65f)
        return false;
    Ground = Hit.ImpactPoint;
    return FMath::Abs(Ground.Z - Candidate.Z) <= Height;
}
bool StoneGolemGeometry::Visible(UWorld* World, const AActor* Source, const FVector& Center, const FVector& Ground)
{
    if (!World)
        return false;
    FHitResult Hit;
    const FVector Lift(0, 0, 25);
    return !World->LineTraceSingleByObjectType(Hit, Center + Lift, Ground + Lift,
                                               FCollisionObjectQueryParams(ECC_WorldStatic),
                                               FCollisionQueryParams(SCENE_QUERY_STAT(StoneGolemWall), false, Source));
}
void StoneGolemGeometry::BuildSurface(UWorld* World, const AActor* Source, const FStoneGolemAttackTuning& T,
                                      const FVector& Center, float Yaw, TArray<FVector>& Vertices,
                                      TArray<int32>& Triangles)
{
    Vertices.Reset();
    Triangles.Reset();
    TArray<bool> Valid;
    if (T.Shape == EStoneGolemShape::Rectangle)
    {
        constexpr int32 Rows = 24, Columns = 8;
        for (int32 X = 0; X <= Rows; ++X)
            for (int32 Y = 0; Y <= Columns; ++Y)
            {
                const FVector Point = Center + FRotator(0, Yaw, 0).RotateVector(FVector(
                                                   T.Length * X / Rows, T.Width * (float(Y) / Columns - .5f), 0));
                FVector Floor = Point;
                Valid.Add(Project(World, Source, Point, T.HeightTolerance, Floor) && T.Contains(Center, Yaw, Floor) &&
                          Visible(World, Source, Center, Floor));
                Vertices.Add(Floor);
            }
        auto Add = [&](int32 A, int32 B, int32 C)
        {
            if (Valid[A] && Valid[B] && Valid[C])
            {
                Triangles.Add(A);
                Triangles.Add(B);
                Triangles.Add(C);
            }
        };
        for (int32 X = 0; X < Rows; ++X)
            for (int32 Y = 0; Y < Columns; ++Y)
            {
                const int32 A = X * (Columns + 1) + Y, B = A + 1, C = A + Columns + 1, D = C + 1;
                Add(A, C, D);
                Add(A, D, B);
            }
        return;
    }
    constexpr int32 Steps = 96, Rings = 8;
    const float Sweep = T.Shape == EStoneGolemShape::Circle ? 360.f : T.Angle;
    for (int32 Ring = 0; Ring <= Rings; ++Ring)
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const float Angle = Yaw - Sweep * .5f + Sweep * Step / Steps;
            const FVector Point = Center + FRotator(0, Angle, 0).Vector() * (T.Radius * Ring / Rings);
            FVector Floor = Point;
            Valid.Add(Project(World, Source, Point, T.HeightTolerance, Floor) && T.Contains(Center, Yaw, Floor) &&
                      Visible(World, Source, Center, Floor));
            Vertices.Add(Floor);
        }
    auto Add = [&](int32 A, int32 B, int32 C)
    {
        if (Valid[A] && Valid[B] && Valid[C])
        {
            Triangles.Add(A);
            Triangles.Add(B);
            Triangles.Add(C);
        }
    };
    for (int32 Ring = 0; Ring < Rings; ++Ring)
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            int32 A = Ring * (Steps + 1) + Step, B = A + 1, C = A + Steps + 1, D = C + 1;
            Add(A, C, D);
            Add(A, D, B);
        }
}
bool StoneGolemGeometry::SurfaceContains(const TArray<FVector>& Vertices, const TArray<int32>& Triangles,
                                         const FVector& Point)
{
    auto Cross = [](const FVector& A, const FVector& B)
    {
        return A.X * B.Y - A.Y * B.X;
    };
    for (int32 I = 0; I + 2 < Triangles.Num(); I += 3)
    {
        const auto& A = Vertices[Triangles[I]];
        const auto& B = Vertices[Triangles[I + 1]];
        const auto& C = Vertices[Triangles[I + 2]];
        const double Area = Cross(B - A, C - A);
        if (FMath::Abs(Area) < .0001)
            continue;
        const double U = Cross(B - Point, C - Point) / Area, V = Cross(C - Point, A - Point) / Area, W = 1 - U - V;
        if (U >= -1.e-6 && V >= -1.e-6 && W >= -1.e-6)
            return true;
    }
    return false;
}
