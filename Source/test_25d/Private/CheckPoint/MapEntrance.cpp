// Fill out your copyright notice in the Description page of Project Settings.

#include "CheckPoint/MapEntrance.h"

#include "Components/SphereComponent.h"
#include "Game/MyGameModeBase.h"
#include "Interface/PlayerInterface.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interface/CombatInterface.h"
#include "Net/UnrealNetwork.h"

AMapEntrance::AMapEntrance(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    Sphere->SetupAttachment(MoveToComponent);
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;
}

void AMapEntrance::HighLightActor_Implementation()
{
    CheckpointMesh->SetRenderCustomDepth(true);
}

void AMapEntrance::LoadActor_Implementation()
{
    // Do nothing when loading a Map Entrance
}

void AMapEntrance::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                   const FHitResult& SweepResult)
{
    // Readiness is evaluated on authority for the complete connected party in Tick.
    // In particular, a client overlap must never call OpenLevel or save another player.
}

void AMapEntrance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMapEntrance, ReadyPlayers);
    DOREPLIFETIME(AMapEntrance, RequiredPlayers);
    DOREPLIFETIME(AMapEntrance, CountdownEndServerTime);
    DOREPLIFETIME(AMapEntrance, bTravelInProgress);
    DOREPLIFETIME(AMapEntrance, TravelError);
}

float AMapEntrance::GetCountdownRemaining() const
{
    const AGameStateBase* State = GetWorld()->GetGameState();
    return CountdownEndServerTime < 0
               ? 0.f
               : FMath::Max(0.0, CountdownEndServerTime -
                                     (State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()));
}

void AMapEntrance::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || bTravelInProgress)
        return;
    ReadyPlayers = 0;
    RequiredPlayers = 0;
    TArray<int32> Roster;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC) || !IsValid(PC->PlayerState) || PC->PlayerState->IsOnlyASpectator())
            continue;
        ++RequiredPlayers;
        Roster.Add(PC->PlayerState->GetPlayerId());
        APawn* Pawn = PC->GetPawn();
        if (!IsValid(Pawn) || (Pawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Pawn)))
            continue;
        if (FVector::DistSquared(Pawn->GetActorLocation(), Sphere->GetComponentLocation()) <=
            FMath::Square(Sphere->GetScaledSphereRadius()))
            ++ReadyPlayers;
    }
    const bool bAllReady = RequiredPlayers > 0 && ReadyPlayers == RequiredPlayers;
    if (!bAllReady)
    {
        bWaitForPartyToLeaveAfterFailure = false;
        TravelError.Reset();
    }
    if (bWaitForPartyToLeaveAfterFailure)
        return;
    const bool bComplete = Countdown.Update(Roster, bAllReady, GetWorld()->GetTimeSeconds(), RequiredStaySeconds);
    CountdownEndServerTime = Countdown.EndTime;
    if (!bComplete)
        return;
    AMyGameModeBase* GM = GetWorld()->GetAuthGameMode<AMyGameModeBase>();
    bTravelInProgress = GM && GM->TravelPartyThroughPortal(DestinationMap, DestinationPlayerStartTag, TravelError);
    if (!bTravelInProgress)
    {
        bWaitForPartyToLeaveAfterFailure = true;
        Countdown.Reset();
        CountdownEndServerTime = -1;
        UE_LOG(LogTemp, Warning, TEXT("[PortalTravel] %s: %s"), *GetName(), *TravelError);
    }
    ForceNetUpdate();
}
