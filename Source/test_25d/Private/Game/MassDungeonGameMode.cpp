#include "Game/MassDungeonGameMode.h"
#include "Mass/MonsterManagementSubsystem.h"
#include "Engine/World.h"

void AMassDungeonGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
        Manager->Enable(MonsterManagementConfig);
}
