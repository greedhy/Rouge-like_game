// Fill out your copyright notice in the Description page of Project Settings.

#include "Actor/MyEffectActor.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
// Sets default values
AMyEffectActor::AMyEffectActor()
{

    PrimaryActorTick.bCanEverTick = true;

    SetRootComponent(CreateDefaultSubobject<USceneComponent>("SceneRoot"));
}

void AMyEffectActor::BeginPlay()
{
    Super::BeginPlay();
}

void AMyEffectActor::ApplyEffectToTarget(AActor* TargetActor, TSubclassOf<UGameplayEffect> GameplayEffectClass)
{

    if (TargetActor->ActorHasTag(FName("Enemy")) && !bApplyEffectToEnemy)
        return;

    //这个函数是工具库,可以获取实现了IAbilitySystem接口的Actor的AbilitySystem
    UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
    if (!TargetASC)
        return;
    //检查是否Class不为null
    check(GameplayEffectClass)
        //获取ASC的EffectContext
        FGameplayEffectContextHandle EffectContextHandle = TargetASC->MakeEffectContext();
    //为ContextHandle添加源（就是这个效果的添加者）
    EffectContextHandle.AddSourceObject(this);
    //制作一个Spec
    FGameplayEffectSpecHandle EffectSpecHandle =
        TargetASC->MakeOutgoingSpec(GameplayEffectClass, ActorLevel, EffectContextHandle);
    //为Target添加效果

    //为Target添加效果,然后获取FActiveGameplayEffectHandle
    FActiveGameplayEffectHandle ActiveEffectHandle =
        TargetASC->ApplyGameplayEffectSpecToSelf(*EffectSpecHandle.Data.Get());
    //如果这个Effect为无限时长的类,且当前无限时长类的policy为RemovedOnEndOverlap,
    //则需要存储ActiveEffectHandle,以便后续删除
    bool bIsInfinite = EffectSpecHandle.Data->Def.Get()->DurationPolicy == EGameplayEffectDurationType::Infinite;

    if (bIsInfinite && InfiniteEffectRemovalPolicy == EEffectRemovalPolicy::RemoveOnEndOverlap)
    {
        ActiveEffectHandles.Add(ActiveEffectHandle, TargetASC);
    }
    if (!bIsInfinite)
    {
        Destroy();
    }
}

void AMyEffectActor::OnOverlap(AActor* TargetActor)
{
    if (TargetActor->ActorHasTag(FName("Enemy")) && !bApplyEffectToEnemy)
        return;

    if (InstantEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnOverlap)
    {
        ApplyEffectToTarget(TargetActor, InstantGameplayEffectClass);
    }
    if (DurationEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnOverlap)
    {
        ApplyEffectToTarget(TargetActor, DurationGameplayEffectClass);
    }
    if (InfiniteEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnOverlap)
    {
        ApplyEffectToTarget(TargetActor, InfiniteGameplayEffectClass);
    }
}

void AMyEffectActor::OnEndOverlap(AActor* TargetActor)
{
    if (TargetActor->ActorHasTag(FName("Enemy")) && !bApplyEffectToEnemy)
        return;

    if (InstantEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnEndOverlap)
    {
        ApplyEffectToTarget(TargetActor, InstantGameplayEffectClass);
    }
    if (DurationEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnEndOverlap)
    {
        ApplyEffectToTarget(TargetActor, DurationGameplayEffectClass);
    }
    if (InfiniteEffectApplicationPolicy == EEffectApplicationPolicy::ApplyOnEndOverlap)
    {
        ApplyEffectToTarget(TargetActor, InstantGameplayEffectClass);
    }
    if (InfiniteEffectRemovalPolicy == EEffectRemovalPolicy::RemoveOnEndOverlap)
    {
        //去除InfiniteEffect
        UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);

        if (!IsValid(TargetASC))
            return;

        TArray<FActiveGameplayEffectHandle> RemovedKeys;
        for (TTuple<FActiveGameplayEffectHandle, UAbilitySystemComponent*>& Tuple : ActiveEffectHandles)
        {
            if (TargetASC == Tuple.Value)
            {
                TargetASC->RemoveActiveGameplayEffect(Tuple.Key, 1);
                RemovedKeys.Add(Tuple.Key);
            }
        }
        for (auto& Key : RemovedKeys)
        {
            ActiveEffectHandles.FindAndRemoveChecked(Key);
        }
    }
}
