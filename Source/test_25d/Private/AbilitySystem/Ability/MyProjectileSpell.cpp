// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyProjectileSpell.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Interface/CombatInterface.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"

AMyProjectile* UMyProjectileSpell::CreateProjectileDeferred(TSubclassOf<AMyProjectile> Class,
                                                            const FTransform& Transform)
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!IsValid(Avatar) || !Avatar->HasAuthority() || !Class)
        return nullptr;
    AMyProjectile* Projectile = GetWorld()->SpawnActorDeferred<AMyProjectile>(
        Class, Transform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Projectile)
        Projectile->DamageEffectParams = MakeDamageEffectParamsFromClassDefaults();
    return Projectile;
}
void UMyProjectileSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo,
                                         const FGameplayAbilityActivationInfo ActivationInfo,
                                         const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMyProjectileSpell::SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag,
                                         const FName SocketName, const bool bOverridePitch, const float PitchOverride)
{
    const bool bIsServer = GetAvatarActorFromActorInfo()->HasAuthority();
    if (!bIsServer)
        return;

    const FVector SocketLocation =
        ICombatInterface::Execute_GetCombatSocketLocation(GetAvatarActorFromActorInfo(), SocketTag);
    FRotator Rotation = (ProjectileTargetLocation - SocketLocation).Rotation();

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(SocketLocation);
    SpawnTransform.SetRotation(Rotation.Quaternion());

    if (bOverridePitch)
    {
        Rotation.Pitch = PitchOverride; //覆写发射角度
    }

    //SpawnActorDeferred将异步创建实例，在实例创建完成时，相应的数据已经应用到了实例身上
    AMyProjectile* Projectile = CreateProjectileDeferred(ProjectileClass, SpawnTransform);
    if (!Projectile)
        return;

    //const UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetAvatarActorFromActorInfo());
    //const FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, GetAbilityLevel(), SourceASC->MakeEffectContext());
    //FMyGameplayTags GameplayTags = FMyGameplayTags::Get();
    //const	float ScaledDamage = Damage.GetValueAtLevel(GetAbilityLevel());
    //UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, DamageType, ScaledDamage);
    //Projectile->DamageEffectSpecHandle = SpecHandle;

    Projectile->FinishSpawning(SpawnTransform);
}
