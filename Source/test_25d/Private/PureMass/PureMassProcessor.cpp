#include "PureMass/PureMassProcessor.h"
#include "Performance/MonsterPerf.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"

UPureMassProcessor::UPureMassProcessor() : Query(*this)
{
    ProcessingPhase = EMassProcessingPhase::PostPhysics;
    ExecutionFlags = int32(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
    bRequiresGameThreadExecution = true;
    QueryBasedPruning = EMassQueryBasedPruning::Never;
}
void UPureMassProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    Query.AddRequirement<FPureMassFragment>(EMassFragmentAccess::ReadWrite);
}
void UPureMassProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    MONSTER_PERF_SCOPE(MassSim);
    UWorld* World = Context.GetWorld();
    UPureMassSubsystem* Simulation = World ? World->GetSubsystem<UPureMassSubsystem>() : nullptr;
    if (!Simulation || !Simulation->BeginUpdate(Context.GetDeltaTimeSeconds())) return;
    Query.ForEachEntityChunk(Context, [Simulation](FMassExecutionContext& Chunk)
    {
        for (FPureMassFragment& Fragment : Chunk.GetMutableFragmentView<FPureMassFragment>())
            Simulation->UpdateMonster(Fragment);
    });
    Simulation->FinishUpdate();
}
