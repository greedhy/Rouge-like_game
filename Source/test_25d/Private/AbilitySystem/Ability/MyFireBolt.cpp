// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyFireBolt.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"

#include "Actor/MyProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"

FString UMyFireBolt::GetDescription(int32 Level)
{
    const int32 ScaledDamage = Damage.GetValueAtLevel(Level);
    const float ManaCost = FMath::Abs(GetManaCost(Level));
    const float Cooldown = GetCooldown(Level);
    if (Level == 1)
    {
        return FString::Printf(TEXT(
                                   // Title
                                   "<Title>FIRE BOLT</>\n\n"

                                   // Level
                                   "<Small>Level: </><Level>%d</>\n"
                                   // ManaCost
                                   "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                                   // Cooldown
                                   "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                                   "<Default>Launches a bolt of fire, "
                                   "exploding on impact and dealing: </>"

                                   // Damage
                                   "<Damage>%d</><Default> fire damage with"
                                   " a chance to burn</>"),

                               // Values
                               Level, ManaCost, Cooldown, ScaledDamage);
    }
    else
    {
        return FString::Printf(TEXT(
                                   // Title
                                   "<Title>FIRE BOLT</>\n\n"

                                   // Level
                                   "<Small>Level: </><Level>%d</>\n"
                                   // ManaCost
                                   "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                                   // Cooldown
                                   "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                                   // Number of FireBolts
                                   "<Default>Launches %d bolts of fire, "
                                   "exploding on impact and dealing: </>"

                                   // Damage
                                   "<Damage>%d</><Default> fire damage with"
                                   " a chance to burn</>"),

                               // Values
                               Level, ManaCost, Cooldown, FMath::Min(Level, NumProjectiles), ScaledDamage);
    }
}

FString UMyFireBolt::GetNextLevelDescription(int32 Level)
{
    const int32 ScaledDamage = Damage.GetValueAtLevel(Level);
    const float ManaCost = FMath::Abs(GetManaCost(Level));
    const float Cooldown = GetCooldown(Level);
    return FString::Printf(TEXT(
                               // Title
                               "<Title>NEXT LEVEL: </>\n\n"

                               // Level
                               "<Small>Level: </><Level>%d</>\n"
                               // ManaCost
                               "<Small>ManaCost: </><ManaCost>%.1f</>\n"
                               // Cooldown
                               "<Small>Cooldown: </><Cooldown>%.1f</>\n\n"

                               // Number of FireBolts
                               "<Default>Launches %d bolts of fire, "
                               "exploding on impact and dealing: </>"

                               // Damage
                               "<Damage>%d</><Default> fire damage with"
                               " a chance to burn</>"),

                           // Values
                           Level, ManaCost, Cooldown, FMath::Min(Level, NumProjectiles), ScaledDamage);
}

void UMyFireBolt::SpawnProjectiles(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag,
                                   bool bOverridePitch, float PitchOverride, AActor* HomingTarget)
{
    const bool bIsServer = GetAvatarActorFromActorInfo()->HasAuthority();
    if (!bIsServer)
        return;
    ReportOffensiveAttack(HomingTarget);

    const FVector SocketLocation =
        ICombatInterface::Execute_GetCombatSocketLocation(GetAvatarActorFromActorInfo(), SocketTag);
    FRotator Rotation = (ProjectileTargetLocation - SocketLocation).Rotation();
    if (bOverridePitch)
        Rotation.Pitch = PitchOverride;

    const FVector Forward = Rotation.Vector();
    const int32 EffectiveNumProjectiles = FMath::Min(NumProjectiles, GetAbilityLevel());

    TArray<FRotator> Rotations = UMyAbilitySystemLibrary::EvenlySpacedRotators(
        Forward, FVector::UpVector, ProjectileSpread, EffectiveNumProjectiles);

    for (const FRotator& Rot : Rotations)
    {
        FTransform SpawnTransform;
        SpawnTransform.SetLocation(SocketLocation);
        SpawnTransform.SetRotation(Rot.Quaternion());

        AMyProjectile* Projectile = CreateProjectileDeferred(ProjectileClass, SpawnTransform);
        if (!Projectile)
            continue;

        if (HomingTarget && HomingTarget->Implements<UCombatInterface>())
        {
            Projectile->ProjectileMovement->HomingTargetComponent = HomingTarget->GetRootComponent();
        }
        else
        {
            Projectile->HomingTargetSceneComponent = NewObject<USceneComponent>(USceneComponent::StaticClass());
            Projectile->HomingTargetSceneComponent->SetWorldLocation(ProjectileTargetLocation);
            Projectile->ProjectileMovement->HomingTargetComponent = Projectile->HomingTargetSceneComponent;
        }
        Projectile->ProjectileMovement->HomingAccelerationMagnitude =
            FMath::FRandRange(HomingAccelerationMin, HomingAccelerationMax);
        Projectile->ProjectileMovement->bIsHomingProjectile = bLaunchHomingProjectiles;

        Projectile->FinishSpawning(SpawnTransform);
    }
}
