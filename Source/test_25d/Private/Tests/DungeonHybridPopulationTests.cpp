#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonRunProfile.h"
#include "Roguelike/DungeonRunModel.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonHybridPopulationTest, "test_25d.Roguelike.Hybrid.PopulationAndReservedTargets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDungeonHybridPopulationTest::RunTest(const FString&)
{
    auto* Profile = NewObject<UDungeonRunProfile>();
    TestEqual(TEXT("Legacy profile has no Mass"), Profile->MassCountForWave(true,1), 0);
    TestEqual(TEXT("Legacy cap preserved"), Profile->EncounterCapacity(), 30);
    Profile->bUseMassMelee = true;
    TestEqual(TEXT("Elite and boss get fifty"), Profile->MassCountForWave(false,1), 50);
    TestEqual(TEXT("Combat first wave"), Profile->MassCountForWave(true,1), 25);
    TestEqual(TEXT("Combat second wave"), Profile->MassCountForWave(true,2), 25);
    TestEqual(TEXT("No extra wave"), Profile->MassCountForWave(true,3), 0);
    TestEqual(TEXT("Room capacity includes Actor allowance"), Profile->EncounterCapacity(), 80);
    FDungeonEncounterLedger Ledger;
    Ledger.Start(2);
    for (int32 Wave=1; Wave<=2; ++Wave)
    {
        TestTrue(TEXT("Open wave"), Ledger.BeginWave());
        const FName ActorKey(*FString::Printf(TEXT("Actor_%d"),Wave));
        TestTrue(TEXT("Actor registered"),Ledger.RegisterTarget(ActorKey,true));
        TArray<FName> Reserved;
        for (int32 I=0; I<Profile->MassCountForWave(true,Wave); ++I)
        {
            FName Key(*FString::Printf(TEXT("Mass_%d_%d"),Wave,I));
            Reserved.Add(Key);
            TestTrue(TEXT("Reserve queued entity before spawning"), Ledger.RegisterTarget(Key,true));
        }
        Ledger.EndWave(true);
        Ledger.ResolveTarget(ActorKey,EDungeonTargetResolution::Killed);
        TestFalse(TEXT("Queued Mass prevents early progression"),Ledger.CanSpawnNextWave());
        TestFalse(TEXT("Queued Mass prevents early clear"),Ledger.IsClear());
        for (FName Key:Reserved)
        {
            TestTrue(TEXT("Unique kill"),Ledger.ResolveTarget(Key,EDungeonTargetResolution::Killed));
            TestFalse(TEXT("Repeated death ignored"),Ledger.ResolveTarget(Key,EDungeonTargetResolution::Killed));
        }
    }
    TestTrue(TEXT("Both mixed waves completed"),Ledger.IsClear());
    Profile->MassPerCombatRoom=49;
    TestEqual(TEXT("Odd count never loses an entity"),Profile->MassCountForWave(true,1)+Profile->MassCountForWave(true,2),49);
    return true;
}
#endif
