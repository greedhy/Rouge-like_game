#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Actor/MyOrbitingFireBolt.h"

bool UOrbitingFireBoltConfig::IsValidConfiguration() const
{
    return FireBoltClass && !FireBoltClass->HasAnyClassFlags(CLASS_Abstract) && FMath::IsFinite(ActivationManaCost) &&
           ActivationManaCost >= 0.f && FMath::IsFinite(ManaPerSecond) && ManaPerSecond >= 0.f && MaxFireBolts >= 1 &&
           MaxFireBolts <= 32 && FMath::IsFinite(SpawnInterval) && SpawnInterval >= .05f &&
           FMath::IsFinite(LaunchInterval) && LaunchInterval >= .05f && FMath::IsFinite(OrbitRadius) &&
           OrbitRadius >= 0.f && FMath::IsFinite(OrbitHeight) && FMath::IsFinite(OrbitDegreesPerSecond) &&
           FMath::IsFinite(ProjectileSpeed) && ProjectileSpeed > 0.f && FMath::IsFinite(MaxTargetDistance) &&
           MaxTargetDistance > 0.f && FMath::IsFinite(MaxFlightSeconds) && MaxFlightSeconds > 0.f;
}
