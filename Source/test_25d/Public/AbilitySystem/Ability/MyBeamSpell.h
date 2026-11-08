// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "MyBeamSpell.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyBeamSpell : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void StoreMouseDataInfo(const FHitResult& HitResult);

    UFUNCTION(BlueprintCallable)
    void StoreOwnerVariables();

    UFUNCTION(BlueprintCallable)
    void TraceFirstTarget(const FVector& BeamTargetLocation);

    UFUNCTION(BlueprintCallable)
    void StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets);

    UFUNCTION(BlueprintImplementableEvent)
    void PrimaryTargetDied(AActor* DeadActor);

    UFUNCTION(BlueprintImplementableEvent)
    void AdditionalTargetDied(AActor* DeadActor);

    /**
 * 技能结束时调用
 */
    UFUNCTION(BlueprintCallable)
    void OnEndAbility(TArray<AActor*> AdditionalTargets);

protected:
    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    FVector MouseHitLocation;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<AActor> MouseHitActor;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<APlayerController> OwnerPlayerController;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<ACharacter> OwnerCharacter;

    UPROPERTY(EditDefaultsOnly, Category = "Beam")
    int32 MaxNumShockTargets = 10;
};
