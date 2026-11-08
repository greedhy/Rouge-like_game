// Fill out your copyright notice in the Description page of Project Settings.

#include "CheckPoint/CheckPoint.h"

#include "Components/SphereComponent.h"
#include "Game/MyGameModeBase.h"
#include "Interface/PlayerInterface.h"
#include "Kismet/GameplayStatics.h"

ACheckPoint::ACheckPoint(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = false;

    CheckpointMesh = CreateDefaultSubobject<UStaticMeshComponent>("CheckpointMesh");
    CheckpointMesh->SetupAttachment(GetRootComponent());
    CheckpointMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CheckpointMesh->SetCollisionResponseToAllChannels(ECR_Block);

    CheckpointMesh->SetCustomDepthStencilValue(CustomDepthStencilOverride);
    CheckpointMesh->MarkRenderStateDirty();

    Sphere = CreateDefaultSubobject<USphereComponent>("Sphere");
    Sphere->SetupAttachment(CheckpointMesh);
    Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    MoveToComponent = CreateDefaultSubobject<USceneComponent>("MoveToComponent");
    MoveToComponent->SetupAttachment(GetRootComponent());
}

void ACheckPoint::LoadActor_Implementation()
{
    if (bReached)
    {
        HandleGlowEffects();
    }
}

void ACheckPoint::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                  UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                  const FHitResult& SweepResult)
{
    if (OtherActor->Implements<UPlayerInterface>())
    {
        bReached = true;

        if (AMyGameModeBase* MyGM = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this)))
        {
            const UWorld* World = GetWorld();
            FString MapName = World->GetMapName();
            MapName.RemoveFromStart(World->StreamingLevelsPrefix);

            MyGM->SaveWorldState(GetWorld(), MapName);
        }

        IPlayerInterface::Execute_SaveProgress(OtherActor, PlayerStartTag);
        HandleGlowEffects();
    }
}

void ACheckPoint::BeginPlay()
{
    Super::BeginPlay();

    if (bBindOverlapCallback)
    {
        Sphere->OnComponentBeginOverlap.AddDynamic(this, &ACheckPoint::OnSphereOverlap);
    }
}

void ACheckPoint::SetMoveToLocation_Implementation(FVector& OutDestination)
{
    OutDestination = MoveToComponent->GetComponentLocation();
}

void ACheckPoint::HighLightActor_Implementation()
{
    if (!bReached)
    {
        CheckpointMesh->SetRenderCustomDepth(true);
    }
}

void ACheckPoint::UnHighLightActor_Implementation()
{
    CheckpointMesh->SetRenderCustomDepth(false);
}

void ACheckPoint::HandleGlowEffects()
{
    Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UMaterialInstanceDynamic* DynamicMaterialInstace =
        UMaterialInstanceDynamic::Create(CheckpointMesh->GetMaterial(0), this);
    CheckpointMesh->SetMaterial(0, DynamicMaterialInstace);
    CheckpointReached(DynamicMaterialInstace);
}
