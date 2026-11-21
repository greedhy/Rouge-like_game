// 调用链：旧Surface Start/细World.Acquire -> Spawn；旧Advance/新Phase -> AdvanceClock -> 可选图子片段。
#include "AbilitySystem/Unified/AbilityPrototypeSurfaceService.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Terrain/TerrainSurface.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

ATerrainSurface* AbilityPrototypeSurfaceService::Spawn(AActor& Caster, const UAbilityPrototype& Parameters,
                                                       FVector Position, EAbilityPrototypeSurfaceProfile Profile)
{
    const FTransform Transform(FRotator::ZeroRotator, Position);
    auto* Surface = Caster.GetWorld()->SpawnActorDeferred<ATerrainSurface>(
        Parameters.SurfaceClass, Transform, &Caster, Cast<APawn>(&Caster),
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Surface)
    {
        return nullptr;
    }
    Surface->bOnlyAffectCasterEnemies = true;
    if (Profile == EAbilityPrototypeSurfaceProfile::Mixed)
    {
        Surface->bAffectPlayers = false;
        Surface->bAffectEnemies = true;
    }
    Surface->SurfaceSize = FVector2D(Parameters.Radius * 2.f);
    Surface->FinishSpawning(Transform);
    if (Surface->Ground)
    {
        Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    return Surface;
}

void AbilityPrototypeSurfaceService::AdvanceClock(float& Clock, float Delta, float Period,
                                                  EAbilityPrototypeSurfaceProfile Profile, TFunctionRef<bool()> Pulse)
{
    if (Profile == EAbilityPrototypeSurfaceProfile::Actor)
    {
        Clock += Delta;
        if (Clock >= Period)
        {
            Clock = FMath::Fmod(Clock, Period);
            Pulse();
        }
        return;
    }
    bool bContinue = true;
    // 原Mixed Surface先落入辅助周期，再落入尾部周期；不能提取时把合法16次hitch改成8次。
    AbilityBehaviorPrimitives::AdvanceCadence(Clock, Delta, Period, 8, EAbilityCadenceRemainder::Preserve,
                                              [&]()
                                              {
                                                  bContinue = Pulse();
                                                  return true;
                                              });
    if (bContinue)
    {
        AbilityBehaviorPrimitives::AdvanceCadence(Clock, 0.f, Period, 8, EAbilityCadenceRemainder::Preserve, Pulse);
    }
}
