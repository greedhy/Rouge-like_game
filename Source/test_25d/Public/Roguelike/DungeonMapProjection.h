#pragma once
#include "CoreMinimal.h"

namespace DungeonMapProjection
{
    // Floor cells share world X/Y axes. Slate Y points down; camera forward points up.
    inline FVector2D Project(const FVector2D& Cell, double CameraYaw)
    {
        double Sin, Cos;
        FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(CameraYaw));
        return FVector2D(-Sin * Cell.X + Cos * Cell.Y, -Cos * Cell.X - Sin * Cell.Y);
    }
} // namespace DungeonMapProjection
