// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SphereComponent.h"
#include "AbilitySystemComponent.h"
#include "MyEffectActor.generated.h"

UENUM(BlueprintType)
enum class EEffectApplicationPolicy : uint8
{
    ApplyOnOverlap,
    ApplyOnEndOverlap,
    DoNotApply
};

//在Overlap结束时是否移除Effect效果（无限时长的Effect）
UENUM(BlueprintType)
enum class EEffectRemovalPolicy : uint8
{
    RemoveOnEndOverlap,
    DoNotRemove
};

class UGameplayEffect;
UCLASS()
class TEST_25D_API AMyEffectActor : public AActor
{
    GENERATED_BODY()

public:
    // Sets default values for this actor's properties
    AMyEffectActor();

protected:
    // Called when the game starts or when spawned
    virtual void BeginPlay() override;

    TMap<FActiveGameplayEffectHandle, UAbilitySystemComponent*> ActiveEffectHandles;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    bool bDestroyOnEffectApplication = false;
    //...
    UFUNCTION(BlueprintCallable)
    void OnOverlap(AActor* TargetActor);

    UFUNCTION(BlueprintCallable)
    void OnEndOverlap(AActor* TargetActor);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    bool bApplyEffectToEnemy = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    TSubclassOf<UGameplayEffect> InstantGameplayEffectClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    EEffectApplicationPolicy InstantEffectApplicationPolicy = EEffectApplicationPolicy::DoNotApply;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    TSubclassOf<UGameplayEffect> DurationGameplayEffectClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    EEffectApplicationPolicy DurationEffectApplicationPolicy = EEffectApplicationPolicy::DoNotApply;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    TSubclassOf<UGameplayEffect> InfiniteGameplayEffectClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    EEffectApplicationPolicy InfiniteEffectApplicationPolicy = EEffectApplicationPolicy::DoNotApply;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    EEffectRemovalPolicy InfiniteEffectRemovalPolicy = EEffectRemovalPolicy::DoNotRemove;

    //对目标添加Effects的执行逻辑
    UFUNCTION(BlueprintCallable)
    void ApplyEffectToTarget(AActor* TargetActor, TSubclassOf<UGameplayEffect> GameplayEffectClass);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Applied Effects")
    float ActorLevel = 1.f;

private:
};
