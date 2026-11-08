// Fill out your copyright notice in the Description page of Project Settings.

#include "Actor/MyEnemySpawnVolume.h"
#include "Actor/MyEnemySpawnPoint.h"
#include "test_25d/test_25d.h"
#include "Interface/PlayerInterface.h"
AMyEnemySpawnVolume::AMyEnemySpawnVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    Box = CreateDefaultSubobject<UBoxComponent>("Box");
    SetRootComponent(Box);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AMyEnemySpawnVolume::LoadActor_Implementation()
{
    if (bReached)
    {
        Destroy();
    }
}

void AMyEnemySpawnVolume::BeginPlay()
{
    Super::BeginPlay();
    Box->OnComponentBeginOverlap.AddDynamic(this, &AMyEnemySpawnVolume::OnBoxOverlap);
}

void AMyEnemySpawnVolume::OnBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                       UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                       const FHitResult& SweepResult)
{
    if (!ShouldSpawnForOverlap(HasAuthority(), IsValid(OtherActor) && OtherActor->Implements<UPlayerInterface>()))
        return;

    bReached = true;
    for (AMyEnemySpawnPoint* Point : SpawnPoints)
    {
        if (IsValid(Point))
        {
            Point->SpawnEnemy();
        }
    }
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
