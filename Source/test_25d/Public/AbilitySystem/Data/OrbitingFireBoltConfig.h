#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OrbitingFireBoltConfig.generated.h"

class AMyOrbitingFireBolt;

UCLASS(BlueprintType)
class TEST_25D_API UOrbitingFireBoltConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mana", meta = (ClampMin = "0"))
    float ActivationManaCost = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mana", meta = (ClampMin = "0"))
    float ManaPerSecond = 1.f;
    // Orbiting and launched actors both count toward this limit.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolts", meta = (ClampMin = "1", ClampMax = "32"))
    int32 MaxFireBolts = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolts", meta = (ClampMin = "0.05", Units = "s"))
    float SpawnInterval = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolts", meta = (ClampMin = "0.05", Units = "s"))
    float LaunchInterval = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolts")
    TSubclassOf<AMyOrbitingFireBolt> FireBoltClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (ClampMin = "0", Units = "cm"))
    float OrbitRadius = 65.f;
    // Offset above the avatar origin (the player capsule centre).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (Units = "cm"))
    float OrbitHeight = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (Units = "deg/s"))
    float OrbitDegreesPerSecond = 90.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "1", Units = "cm/s"))
    float ProjectileSpeed = 900.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "1", Units = "cm"))
    float MaxTargetDistance = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0.1", Units = "s"))
    float MaxFlightSeconds = 8.f;
    bool IsValidConfiguration() const;
};
