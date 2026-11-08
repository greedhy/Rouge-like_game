// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CheckPoint/CheckPoint.h"
#include "CheckPoint/PartyPortalCountdown.h"
#include "MapEntrance.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API AMapEntrance : public ACheckPoint
{
    GENERATED_BODY()
public:
    AMapEntrance(const FObjectInitializer& ObjectInitializer);
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.1", Units = "s"))
    float RequiredStaySeconds = 3.f;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Travel")
    int32 ReadyPlayers = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Travel")
    int32 RequiredPlayers = 0;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Travel")
    double CountdownEndServerTime = -1;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Travel")
    bool bTravelInProgress = false;
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Travel")
    FString TravelError;
    UFUNCTION(BlueprintPure, Category = "Travel")
    float GetCountdownRemaining() const;

    /* Highlight Interface */
    virtual void HighLightActor_Implementation() override;
    /* Highlight Interface */

    /* Save Interface */
    virtual void LoadActor_Implementation() override;
    /* end Save Interface */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
    TSoftObjectPtr<UWorld> DestinationMap;

    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = "Travel",
        meta =
            (ToolTip =
                 "Player Start Tag shared by the destination TravelPlayerStart actors. PlayerSlot selects each player's exact spawn."))
    FName DestinationPlayerStartTag;

protected:
    virtual void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                 UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                 const FHitResult& SweepResult) override;

private:
    FPartyPortalCountdown Countdown;
    bool bWaitForPartyToLeaveAfterFailure = false;
};
