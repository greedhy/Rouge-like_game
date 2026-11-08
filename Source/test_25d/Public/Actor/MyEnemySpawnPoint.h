// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Character/EnemyCharacter.h"
#include "Engine/TargetPoint.h"
#include "MyEnemySpawnPoint.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API AMyEnemySpawnPoint : public ATargetPoint
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void SpawnEnemy();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Class")
    TSubclassOf<AEnemyCharacter> EnemyClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Class")
    int32 EnemyLevel = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass Monsters")
    FName MonsterRoomId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Class")
    ECharacterClass CharacterClass = ECharacterClass::Warrior;
};
