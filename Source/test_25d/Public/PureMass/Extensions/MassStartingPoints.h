#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassStartingPoints.generated.h"

/** Session ledger: survives map travel, separate for each PIE game instance. */
UCLASS()
class TEST_25D_API UMassStartingPointsSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    int32 EnterPlayer(int32 Slot);
    void RememberPlayer(int32 Slot, int32 Points);
    void ObservePlayer(class AMyPlayerState* State);

private:
    TMap<int32, int32> RemainingPoints;
    TMap<TWeakObjectPtr<class AMyPlayerState>, TWeakObjectPtr<class APawn>> InitializedPlayers;
};

/** Opt-in by the existing Mass game mode; does not change legacy player classes. */
UCLASS()
class TEST_25D_API UMassStartingPointsWorld : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
};
