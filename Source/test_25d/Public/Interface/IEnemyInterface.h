// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IEnemyInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UEnemyInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * 
 */
class TEST_25D_API IEnemyInterface
{
    GENERATED_BODY()

    // Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    void SetCombatTarget(AActor* InCombatTarget); //设置敌人的攻击目标

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    AActor* GetCombatTarget() const; //获取敌人的攻击目标
};
