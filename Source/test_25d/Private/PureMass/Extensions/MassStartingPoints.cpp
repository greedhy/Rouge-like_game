#include "PureMass/Extensions/MassStartingPoints.h"
#include "PureMass/PureMassGameMode.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

int32 UMassStartingPointsSession::EnterPlayer(int32 Slot)
{
    if (const int32* Points = RemainingPoints.Find(Slot))
        return *Points;
    RemainingPoints.Add(Slot, 999);
    return 999;
}
void UMassStartingPointsSession::RememberPlayer(int32 Slot, int32 Points)
{
    RemainingPoints.Add(Slot, Points);
}
void UMassStartingPointsSession::ObservePlayer(AMyPlayerState* State)
{
    if (!IsValid(State) || !IsValid(State->GetPawn()))
        return;
    const int32 Slot = State->GetTravelPlayerSlot();
    const auto* PreviousPawn = InitializedPlayers.Find(State);
    if (!PreviousPawn || PreviousPawn->Get() != State->GetPawn())
    {
        State->SetSpellPoints(EnterPlayer(Slot));
        InitializedPlayers.Add(State, State->GetPawn());
        const TWeakObjectPtr<AMyPlayerState> WeakState(State);
        const TWeakObjectPtr<APawn> Pawn(State->GetPawn());
        State->OnSpellPointsChangedDelegate.AddWeakLambda(this,
                                                          [this, Slot, WeakState, Pawn](int32 Points)
                                                          {
                                                              // Ignore save restoration during possession of a replacement pawn.
                                                              if (WeakState.IsValid() && Pawn.IsValid() &&
                                                                  WeakState->GetPawn() == Pawn.Get())
                                                                  RememberPlayer(Slot, Points);
                                                          });
    }
    RememberPlayer(Slot, State->GetSpellPoints());
    for (auto It = InitializedPlayers.CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();
}
bool UMassStartingPointsWorld::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}
TStatId UMassStartingPointsWorld::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(MassStartingPoints, STATGROUP_Tickables);
}
void UMassStartingPointsWorld::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || !World->HasBegunPlay() || !Cast<APureMassGameMode>(World->GetAuthGameMode()))
        return;
    auto* GameState = World->GetGameState();
    auto* GI = World->GetGameInstance();
    auto* Session = GI ? GI->GetSubsystem<UMassStartingPointsSession>() : nullptr;
    if (!GameState || !Session)
        return;
    for (APlayerState* BaseState : GameState->PlayerArray)
    {
        auto* State = Cast<AMyPlayerState>(BaseState);
        if (!State || !State->GetPawn() || State->GetTravelPlayerSlot() == INDEX_NONE)
            continue;
        const auto* ASC = Cast<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        if (!ASC || !ASC->bStartupAbilityGiven)
            continue;
        Session->ObservePlayer(State);
    }
}
