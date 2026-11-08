// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyBeamSpell.h"

#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"

void UMyBeamSpell::StoreMouseDataInfo(const FHitResult& HitResult)
{
    if (HitResult.bBlockingHit)
    {
        MouseHitLocation = HitResult.ImpactPoint;
        MouseHitActor = HitResult.GetActor();
        SetOffensiveAttackTarget(MouseHitActor);
    }
    else
    {
        CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
        //EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true,false);
    }
}

void UMyBeamSpell::StoreOwnerVariables()
{
    if (CurrentActorInfo)
    {
        OwnerPlayerController = CurrentActorInfo->PlayerController.Get();
        OwnerCharacter = Cast<ACharacter>(CurrentActorInfo->AvatarActor);
    }
}

void UMyBeamSpell::TraceFirstTarget(const FVector& BeamTargetLocation)
{
    check(OwnerCharacter);
    if (OwnerCharacter->Implements<UCombatInterface>())
    {
        if (USkeletalMeshComponent* Weapon = ICombatInterface::Execute_GetWeapon(OwnerCharacter))
        {
            TArray<AActor*> ActorsToIgnore;
            ActorsToIgnore.Add(OwnerCharacter);
            FHitResult HitResult;
            const FVector SocketLocation = Weapon->GetSocketLocation(FName("TipSocket"));
            UKismetSystemLibrary::SphereTraceSingle(OwnerCharacter, SocketLocation, BeamTargetLocation, 10.f,
                                                    TraceTypeQuery1, false, ActorsToIgnore, EDrawDebugTrace::None,
                                                    HitResult, true);

            if (HitResult.bBlockingHit)
            {
                MouseHitLocation = HitResult.ImpactPoint;
                MouseHitActor = HitResult.GetActor();
            }
        }
    }
    if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(MouseHitActor))
    {
        ReportOffensiveAttack(MouseHitActor);
        if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UMyBeamSpell::PrimaryTargetDied))
        {
            CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UMyBeamSpell::PrimaryTargetDied);
        }
    }
}

void UMyBeamSpell::StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets)
{
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(GetAvatarActorFromActorInfo());
    ActorsToIgnore.Add(MouseHitActor);

    TArray<AActor*> OverlappingActors;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(GetAvatarActorFromActorInfo(), OverlappingActors,
                                                        ActorsToIgnore, 850.f, MouseHitActor->GetActorLocation());

    int32 NumAdditionalTargets = FMath::Min(GetAbilityLevel() + 1, MaxNumShockTargets);
    //int32 NumAdditionalTargets = 5;

    UMyAbilitySystemLibrary::GetClosestTargets(NumAdditionalTargets, OverlappingActors, OutAdditionalTargets,
                                               MouseHitActor->GetActorLocation());

    for (AActor* Target : OutAdditionalTargets)
    {
        if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(Target))
        {
            if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UMyBeamSpell::AdditionalTargetDied))
            {
                CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UMyBeamSpell::AdditionalTargetDied);
            }
        }
    }
}
void UMyBeamSpell::OnEndAbility(TArray<AActor*> AdditionalTargets)
{
    //取消主要目标的死亡回调
    if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(MouseHitActor))
    {
        CombatInterface->GetOnDeathDelegate().RemoveDynamic(this, &UMyBeamSpell::PrimaryTargetDied);
    }

    //取消额外目标的死亡回调
    for (AActor* TargetActor : AdditionalTargets)
    {
        if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(TargetActor))
        {
            CombatInterface->GetOnDeathDelegate().RemoveDynamic(this, &UMyBeamSpell::AdditionalTargetDied);
        }
    }
}
