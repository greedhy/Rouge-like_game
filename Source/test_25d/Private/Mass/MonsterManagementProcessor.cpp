#include "Mass/MonsterManagementProcessor.h"
#include "Mass/MonsterFragments.h"
#include "Mass/MonsterManagementSubsystem.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"

UMonsterManagementProcessor::UMonsterManagementProcessor() : Query(*this)
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionFlags = int32(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
    bRequiresGameThreadExecution = true;
    QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UMonsterManagementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    Query.AddRequirement<FMonsterFragment>(EMassFragmentAccess::ReadWrite);
}

void UMonsterManagementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    UWorld* World = Context.GetWorld();
    auto* Manager = World ? World->GetSubsystem<UMonsterManagementSubsystem>() : nullptr;
    if (!Manager || !Manager->BeginUpdate(Context.GetDeltaTimeSeconds()))
        return;
    Query.ForEachEntityChunk(Context,
                             [Manager](FMassExecutionContext& Chunk)
                             {
                                 auto Monsters = Chunk.GetMutableFragmentView<FMonsterFragment>();
                                 for (FMonsterFragment& Monster : Monsters)
                                     Manager->UpdateEnemy(Monster);
                             });
    Manager->FinishUpdate();
}
