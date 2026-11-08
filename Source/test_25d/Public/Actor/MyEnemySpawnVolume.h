// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MyEnemySpawnPoint.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "Interface/SaveInterface.h"
#include "MyEnemySpawnVolume.generated.h"

UCLASS()
class TEST_25D_API AMyEnemySpawnVolume : public AActor, public ISaveInterface
{
    GENERATED_BODY()

public:
    AMyEnemySpawnVolume();
    static bool ShouldSpawnForOverlap(bool bHasAuthority, bool bIsPlayer)
    {
        return bHasAuthority && bIsPlayer;
    }

    /* Save Interface */
    virtual void LoadActor_Implementation() override;
    /* end Save Interface */

    UPROPERTY(BlueprintReadOnly, SaveGame)
    bool bReached = false;

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    virtual void OnBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                              UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                              const FHitResult& SweepResult);

    UPROPERTY(EditAnywhere)
    TArray<AMyEnemySpawnPoint*> SpawnPoints;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Box;
};
