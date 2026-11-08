// Fill out your copyright notice in the Description page of Project Settings.

#include "Actor/MyEnemySpawnPoint.h"

void AMyEnemySpawnPoint::SpawnEnemy()
{
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AEnemyCharacter* Enemy = GetWorld()->SpawnActorDeferred<AEnemyCharacter>(EnemyClass, GetActorTransform());
    Enemy->SetLevel(EnemyLevel);
    Enemy->MonsterRoomId = MonsterRoomId;
    Enemy->SetCharacterClass(CharacterClass);
    Enemy->FinishSpawning(GetActorTransform());
    Enemy->SpawnDefaultController();
}
