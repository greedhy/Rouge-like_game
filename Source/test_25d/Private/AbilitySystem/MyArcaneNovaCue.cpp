#include "AbilitySystem/MyArcaneNovaCue.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

bool UMyArcaneNovaCue::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
    if (!IsValid(MyTarget) || MyTarget->GetNetMode() == NM_DedicatedServer)
        return false;
    const FVector Origin = Parameters.Location;
    const float Radius = FMath::Clamp(Parameters.RawMagnitude, 1.f, 2000.f);
    if (BurstSystem)
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(MyTarget, BurstSystem, Origin);
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const float Angle = 2.f * PI * Index / 8.f;
            const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(MyTarget, BurstSystem, Origin + Offset,
                                                           FRotator::ZeroRotator, FVector(0.6f));
        }
    }
    if (BurstSound)
        UGameplayStatics::PlaySoundAtLocation(MyTarget, BurstSound, Origin);
    return BurstSystem != nullptr || BurstSound != nullptr;
}
