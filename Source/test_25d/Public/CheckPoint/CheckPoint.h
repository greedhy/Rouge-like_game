// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Interface/SaveInterface.h"
#include "test_25d/test_25d.h"

#include "Interface/HighLightInterface.h"
#include "Interface/SaveInterface.h"
#include "CheckPoint.generated.h"

class USphereComponent;
/**
 * 
 */
UCLASS()
class TEST_25D_API ACheckPoint : public APlayerStart, public ISaveInterface, public IHighLightInterface
{
    GENERATED_BODY()
public:
    ACheckPoint(const FObjectInitializer& ObjectInitializer);

    /* Save Interface */
    virtual bool ShouldLoadTransform_Implementation() override
    {
        return false;
    };
    virtual void LoadActor_Implementation() override;
    /* end Save Interface */

    UPROPERTY(BlueprintReadWrite, SaveGame)
    bool bReached = false;

    UPROPERTY(EditAnywhere)
    bool bBindOverlapCallback = true;

protected:
    UFUNCTION()
    virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                 UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                 const FHitResult& SweepResult);

    virtual void BeginPlay() override;

    /* Highlight Interface */
    virtual void SetMoveToLocation_Implementation(FVector& OutDestination) override;
    virtual void HighLightActor_Implementation() override;
    virtual void UnHighLightActor_Implementation() override;
    /* Highlight Interface */

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> MoveToComponent;

    UPROPERTY(EditDefaultsOnly)
    int32 CustomDepthStencilOverride = CUSTOM_DEPTH_TAN;

    UFUNCTION(BlueprintImplementableEvent)
    void CheckpointReached(UMaterialInstanceDynamic* DynamicMaterialInstance);

    UFUNCTION(BlueprintCallable)
    void HandleGlowEffects();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMeshComponent> CheckpointMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USphereComponent> Sphere;
};
