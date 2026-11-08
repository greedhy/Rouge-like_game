/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Mass.SpatialMigrationRoomAndBounds
 * - test_25d.Mass.ActivityHysteresisAndCombatProtection
 * - test_25d.Mass.RealEntitiesProcessorAndMultiplePlayers
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Mass/MonsterSpatialIndex.h"
#include "Mass/MonsterManagementSubsystem.h"
#include "Mass/MonsterManagementConfig.h"
#include "Mass/MonsterFragments.h"
#include "Mass/MonsterManagementProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Character/EnemyCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "AI/MyAIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterGridTest, "test_25d.Mass.SpatialMigrationRoomAndBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Mass.SpatialMigrationRoomAndBounds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMonsterGridTest::RunTest(const FString&)
{
    FMonsterSpatialIndex Grid;
    Grid.Reset(100);
    Grid.Update(1, FVector(-1, -1, 0), TEXT("RoomA"));
    Grid.Update(2, FVector(0, 0, 500), TEXT("RoomA"));
    Grid.Update(3, FVector(50, 0, 0), TEXT("RoomB"));
    TestEqual(TEXT("Negative coordinates floor into negative cells"), Grid.CellFor(FVector(-1, -1, 0)),
              FIntPoint(-1, -1));
    TestEqual(TEXT("Exact 3D sphere excludes different floors"), Grid.Query(FVector::ZeroVector, 50).Num(), 2);
    TestEqual(TEXT("Room filter"), Grid.Query(FVector::ZeroVector, 100, TEXT("RoomA")).Num(), 1);
    Grid.Update(1, FVector(1000, 0, 0), TEXT("RoomB"));
    TestEqual(TEXT("Move removes old membership"), Grid.Query(FVector::ZeroVector, 10).Num(), 0);
    TestEqual(TEXT("Repeated update does not duplicate"), Grid.Num(), 3);
    TestEqual(TEXT("New room and exact zero radius"), Grid.Query(FVector(1000, 0, 0), 0, TEXT("RoomB")).Num(), 1);
    TestEqual(TEXT("Huge queries are bounded by occupied cells"), Grid.Query(FVector::ZeroVector, 1.e12).Num(), 3);
    TestEqual(TEXT("Negative radius rejected"), Grid.Query(FVector::ZeroVector, -1).Num(), 0);
    Grid.Remove(1);
    Grid.Remove(1);
    TestEqual(TEXT("Remove is idempotent"), Grid.Num(), 2);
    Grid.Reset();
    TestEqual(TEXT("World teardown clears grid"), Grid.NumCells(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterActivityTest, "test_25d.Mass.ActivityHysteresisAndCombatProtection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Mass.ActivityHysteresisAndCombatProtection 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMonsterActivityTest::RunTest(const FString&)
{
    auto Active = [](bool Was, double Distance, bool Players = true, bool Combat = false)
    {
        return FMonsterActivityPolicy::ShouldStayActive(Was, Distance * Distance, Players, Combat, 1000, 1500);
    };
    TestTrue(TEXT("Near any player wakes sleeping enemy"), Active(false, 900));
    TestFalse(TEXT("Sleep persists in hysteresis band"), Active(false, 1200));
    TestTrue(TEXT("Active persists in hysteresis band"), Active(true, 1200));
    TestFalse(TEXT("Far idle enemy sleeps"), Active(true, 1600));
    TestTrue(TEXT("Combat protects far enemy"), Active(false, 5000, true, true));
    TestTrue(TEXT("No players at login/travel must not freeze AI"), Active(false, 5000, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterMassIntegrationTest, "test_25d.Mass.RealEntitiesProcessorAndMultiplePlayers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Mass.RealEntitiesProcessorAndMultiplePlayers 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMonsterMassIntegrationTest::RunTest(const FString&)
{
    UWorld* W = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    auto* Manager = W->GetSubsystem<UMonsterManagementSubsystem>();
    auto* Mass = W->GetSubsystem<UMassEntitySubsystem>();
    if (!TestNotNull(TEXT("Management subsystem"), Manager) || !TestNotNull(TEXT("Mass entity subsystem"), Mass))
    {
        GEngine->DestroyWorldContext(W);
        W->DestroyWorld(false);
        return false;
    }
    auto* Config = NewObject<UMonsterManagementConfig>();
    Config->WakeRadius = 100;
    Config->SleepRadius = 200;
    Config->UpdateInterval = .02f;
    auto* EnemyClass =
        LoadClass<AEnemyCharacter>(nullptr, TEXT("/Game/BluePrints/Characters/Goblin/BP_Goblin.BP_Goblin_C"));
    auto* Enemy = W->SpawnActor<AEnemyCharacter>(EnemyClass);
    auto* AI = W->SpawnActor<AMyAIController>();
    AI->Possess(Enemy);
    // Real behavior-tree execution is covered in PIE; this non-playing world tests external pause ownership.
    auto* Brain = AI->FindComponentByClass<UBehaviorTreeComponent>();
    AI->BrainComponent = Brain;
    Manager->RegisterEnemy(Enemy);
    TestEqual(TEXT("Legacy worlds are opt-in"), Manager->GetStats().Registered, 0);
    Manager->Enable(Config);
    Manager->RegisterEnemy(Enemy);
    Manager->RegisterEnemy(Enemy);
    auto& Entities = Mass->GetMutableEntityManager();
    const FMassEntityHandle Handle = Manager->GetEntityForEnemy(Enemy);
    TestTrue(TEXT("A real Mass entity is created"), Entities.IsEntityValid(Handle));
    TestEqual(TEXT("Registration is idempotent"), Manager->GetStats().Registered, 1);
    auto* Processor = NewObject<UMonsterManagementProcessor>(W);
    Processor->CallInitialize(W, Entities.AsShared());
    auto Step = [&]()
    {
        auto Context = Entities.CreateExecutionContext(.1f);
        Context.SetExecutionType(EMassExecutionContextType::Processor);
        Processor->CallExecute(Entities, Context);
    };
    auto* First = W->SpawnActor<APlayerController>();
    auto* Second = W->SpawnActor<APlayerController>();
    // This lightweight world has not initialized gameplay actors, hence no controller PostInitializeComponents.
    W->AddController(First);
    W->AddController(Second);
    // Use inert characters as pawn locations, without BeginPlay/possession.
    auto* P1 = W->SpawnActor<AEnemyCharacter>(FVector(1000, 0, 0), FRotator::ZeroRotator);
    auto* P2 = W->SpawnActor<AEnemyCharacter>(FVector(1500, 0, 0), FRotator::ZeroRotator);
    First->SetPawn(P1);
    Second->SetPawn(P2);
    Step();
    TestFalse(TEXT("Far from both players sleeps"), Manager->IsMonsterActive(Enemy));
    P2->SetActorLocation(FVector(50, 0, 0));
    Step();
    TestTrue(TEXT("Second player wakes even while host is far"), Manager->IsMonsterActive(Enemy));
    Brain->PauseLogic(TEXT("External test owner"));
    P2->SetActorLocation(FVector(1500, 0, 0));
    Step();
    TestEqual(TEXT("Externally paused tree not claimed"), Manager->GetStats().PausedBrains, 0);
    P2->SetActorLocation(FVector(50, 0, 0));
    Step();
    TestTrue(TEXT("External pause not resumed by wake"), Brain->IsPaused());
    Brain->ResumeLogic(TEXT("External owner finished"));
    P2->SetActorLocation(FVector(1500, 0, 0));
    Step();
    Manager->NotifyDamaged(Enemy);
    TestTrue(TEXT("Damage wakes immediately"), Manager->IsMonsterActive(Enemy));
    Step();
    TestTrue(TEXT("Damage grace protects remote combat"), Manager->IsMonsterActive(Enemy));
    Enemy->MonsterRoomId = TEXT("Room2");
    Enemy->SetActorLocation(FVector(-1100, 0, 0));
    Step();
    TestEqual(TEXT("Processor publishes moved location and room"),
              Manager->FindMonstersInRadius(FVector(-1100, 0, 0), 1, TEXT("Room2")).Num(), 1);
    TestEqual(TEXT("Old position no longer queries"), Manager->FindMonstersInRadius(FVector::ZeroVector, 10).Num(), 0);
    TestTrue(TEXT("Processor really executed"), Manager->GetStats().UpdateCount > 0);
    Manager->UnregisterEnemy(Enemy);
    Manager->UnregisterEnemy(Enemy);
    TestFalse(TEXT("Unregister destroys Mass entity"), Entities.IsEntityValid(Handle));
    TestEqual(TEXT("Unregister clears cells"), Manager->GetStats().OccupiedCells, 0);
    // A never-started actor has no EndPlay callback: subsystem teardown must still be safe.
    Manager->RegisterEnemy(Enemy);
    TestEqual(TEXT("Leave a live registration for world teardown"), Manager->GetStats().Registered, 1);
    GEngine->DestroyWorldContext(W);
    W->DestroyWorld(false);
    return true;
}
#endif
