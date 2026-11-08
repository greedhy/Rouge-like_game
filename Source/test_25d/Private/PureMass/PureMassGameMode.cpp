#include "PureMass/PureMassGameMode.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

void APureMassGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    GetWorld()->GetSubsystem<UPureMassSubsystem>()->Enable();
}

void APureMassGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<APureMassCrowd>();
}

APureMassSpawnRegion::APureMassSpawnRegion()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    PrimaryActorTick.bCanEverTick = false;
}

void APureMassSpawnRegion::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority() || !MonsterType)
        return;
    auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>();
    const int32 SafeCount = FMath::Clamp(Count, 1, 1000);
    const int32 Columns = FMath::CeilToInt(FMath::Sqrt(float(SafeCount)));
    for (int32 Index = 0; Index < SafeCount; ++Index)
    {
        const FVector Offset((Index % Columns) * Spacing, (Index / Columns) * Spacing, 0.f);
        Mass->SpawnMonster(MonsterType, GetActorTransform().TransformPosition(Offset), RoomId);
    }
}
