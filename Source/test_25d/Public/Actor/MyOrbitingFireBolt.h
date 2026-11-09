/** Orbit GA / extracted driver -> LaunchAt -> movement overlap -> inherited damage; optional cast guard checks life before damage. */
#pragma once

#include "CoreMinimal.h"
#include "Actor/MyProjectile.h"
#include "MyOrbitingFireBolt.generated.h"

class UOrbitingFireBoltConfig;
class UPrototypeCastContext;

// Reuses FireBolt's visual Blueprint and MyProjectile's damage/impact implementation.
UCLASS()
class TEST_25D_API AMyOrbitingFireBolt : public AMyProjectile
{
    GENERATED_BODY()
public:
    AMyOrbitingFireBolt();
    void InitializeOrbit(AActor* Avatar, UOrbitingFireBoltConfig* Config, float PhaseDegrees);
    void SetOrbitPhase(float PhaseDegrees)
    {
        OrbitPhase = PhaseDegrees;
    }
    /** Bind the original cast life when this bolt belongs to an extracted graph invocation. */
    void SetCastContext(UPrototypeCastContext* Context);
    void LaunchAt(AActor* Target);
    void ReturnToOrbit();
    bool IsLaunched() const
    {
        return bLaunched;
    }
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsValidOverlap(AActor* OtherActor) override;

protected:
    virtual void BeginPlay() override;
    virtual void OnHit() override;
    virtual bool ShouldPlayImpactOnDestroy() const override
    {
        return false;
    }
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastImpact();

private:
    /** The context owner is the parent invocation; a retired context can never silently disable the guard. */
    TWeakObjectPtr<UPrototypeCastContext> CastContext;
    bool bHasCastLifeGuard = false;
    bool ValidateCastLife(AActor* Target = nullptr) const;
    TWeakObjectPtr<AActor> OrbitAvatar;
    TWeakObjectPtr<AActor> AttackTarget;
    UPROPERTY()
    TObjectPtr<UOrbitingFireBoltConfig> Settings;
    bool bLaunched = false;
    bool bImpactStarted = false;
    float OrbitPhase = 0.f;
    float OrbitClock = 0.f;
    float FlightSeconds = 0.f;
};
