// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MyDamageGameplayAbility.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Actor/MyProjectile.h"
#include "MyProjectileSpell.generated.h"

/**
 * 
 */
class GameplayEffect;
UCLASS()
class TEST_25D_API UMyProjectileSpell : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
protected:
    // Shared deferred-spawn path for active FireBolt and persistent orbiting fireballs.
    AMyProjectile* CreateProjectileDeferred(TSubclassOf<AMyProjectile> Class, const FTransform& Transform);
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    UFUNCTION(BlueprintCallable, Category = "Projectile")
    void SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, const FName SocketName,
                         const bool bOverridePitch = false, const float PitchOverride = 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AMyProjectile> ProjectileClass;

    UPROPERTY(EditDefaultsOnly)
    int32 NumProjectiles = 5;
};
