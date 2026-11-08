#pragma once
#include "CoreMinimal.h"
#include "Game/MyGameModeBase.h"
#include "MassDungeonGameMode.generated.h"

class UMonsterManagementConfig;

/** Opt-in game mode foundation. Elite rolls and rewards are a later layer. */
UCLASS()
class TEST_25D_API AMassDungeonGameMode : public AMyGameModeBase
{
    GENERATED_BODY()
public:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mass Monsters")
    TObjectPtr<UMonsterManagementConfig> MonsterManagementConfig;
};
