// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NiagaraSystem.h"
#include "UObject/Interface.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "CombatInterface.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnASCRegistered, UAbilitySystemComponent*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeathSignature, AActor*, DeadActor);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDamageSignature, float /*DamageAmount*/);

class UAnimMontage;
// This class does not need to be modified.
UINTERFACE(MinimalAPI, BlueprintType)
class UCombatInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * 
 */
//蒙太奇动画和标签以及骨骼位置的映射，用于攻击技能获取和设置攻击范围
USTRUCT(BlueprintType)
struct FTaggedMontage
{
    GENERATED_BODY()

    //使用的蒙太奇
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    UAnimMontage* Montage = nullptr;

    //对应的标签
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag MontageTag;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FGameplayTag SocketTag;

    //攻击时的触发伤害的骨骼插槽
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    FName CombatTipSocketName; //设置技能释放的位置

    //攻击时的触发音效
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    USoundBase* ImpactSound = nullptr;
};

class TEST_25D_API ICombatInterface
{
    GENERATED_BODY()

    // Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    int32 GetPlayerLevel();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    FVector GetCombatSocketLocation(const FGameplayTag& MontageTag);

    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable)
    void UpdateFacingTarget(const FVector& TargetLocation);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    UAnimMontage* GetHitReactMontage();

    virtual void Die(const FVector& DeathImpulse) = 0;
    virtual FOnDamageSignature& GetOnDamageSignature() = 0;
    virtual FOnASCRegistered& GetOnASCRegisteredDelegate() = 0;
    virtual FOnDeathSignature& GetOnDeathDelegate() = 0;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    bool IsDead() const; //获取当前角色是否死亡

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    AActor* GetAvatar(); //获取当前角色

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    TArray<FTaggedMontage> GetAttackMontages();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    UNiagaraSystem* GetBloodEffect(); //获取攻击特效

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    FTaggedMontage GetTaggedMontageByTag(const FGameplayTag& MontageTag); //根据标签获取蒙太奇动画

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    int32 GetMinionCount();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    void IncreaseMinionCount(int32 Amount);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    ECharacterClass GetCharacterClass();

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    USkeletalMeshComponent* GetWeapon();

    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable)
    void SetInShockLoop(bool bInLoop);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    bool IsBeingShocked() const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
    void SetIsBeingShocked(bool bInShock);
};
