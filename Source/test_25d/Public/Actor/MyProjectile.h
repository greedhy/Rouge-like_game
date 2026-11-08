// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MyAbilityTypes.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "MyProjectile.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectileDamageApplied, AActor*);

UCLASS()
class TEST_25D_API AMyProjectile : public AActor
{
    GENERATED_BODY()

public:
    AMyProjectile();
    // Authority notification after the existing damage pipeline, for composable abilities.
    FProjectileDamageApplied DamageApplied;
    UFUNCTION()
    void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                         int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
    UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = true))
    FDamageEffectParams DamageEffectParams;
    virtual bool IsValidOverlap(AActor* OtherActor);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USphereComponent> Sphere;

    UPROPERTY()
    TObjectPtr<USceneComponent> HomingTargetSceneComponent;

    virtual void Destroyed() override;

protected:
    virtual void BeginPlay() override;
    virtual void OnHit();
    virtual bool ShouldPlayImpactOnDestroy() const
    {
        return true;
    }

private:
    bool bHit = false;

    UPROPERTY(EditDefaultsOnly)
    float LifeSpan = 15.f;

    UPROPERTY(EditAnywhere)
    TObjectPtr<UNiagaraSystem> ImpactEffect;

    UPROPERTY(EditAnywhere)
    TObjectPtr<USoundBase> ImpactSound;

    UPROPERTY(EditAnywhere)
    TObjectPtr<USoundBase> LoopingSound;

    UPROPERTY()
    TObjectPtr<UAudioComponent> LoopingSoundComponent;
};
