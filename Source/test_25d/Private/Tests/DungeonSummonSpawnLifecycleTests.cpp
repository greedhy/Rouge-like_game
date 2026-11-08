// 调用链：Automation -> 真实 SpawnSummonedEnemy -> FinishSpawning 碰撞拒绝/成功登记 -> 死亡或来源清理。
#include "Misc/AutomationTest.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonRunProfile.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSummonSpawnLifecycleTest, "test_25d.Shaman.SummonSpawnLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 隔离世界不启动 AI/BeginPlay；开启组件初始化以走引擎真实出生碰撞检查。退出时统一销毁夹具。
bool FDungeonSummonSpawnLifecycleTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Generation = World->SpawnActor<ADungeonGenerationRuntime>();
    Generation->Manifest.RoomSpacing = 2400.f;
    auto& Room = Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef();
    Room.RoomId = TEXT("R01");
    Room.Cell = FIntPoint(1, 0);
    Room.Role = EDungeonRoomRole::Combat;
    auto* Director = World->SpawnActor<ADungeonRunDirector>();
    Director->Profile = NewObject<UDungeonRunProfile>(Director);
    Director->Phase = EDungeonRunPhase::Encounter;
    Director->ActiveRoom = 0;
    auto* Source = World->SpawnActor<AEnemyCharacter>(FVector(1900, 0, 100), FRotator::ZeroRotator);
    Source->MonsterRoomId = TEXT("R01");
    Director->Model.Encounter.Start(1);
    Director->Model.Encounter.BeginWave();
    Director->Model.Encounter.RegisterTarget(TEXT("Source"), true);
    Director->Model.Encounter.EndWave(true);
    Director->ActorTargets.Add(Source, TEXT("Source"));

    // 两侧阻挡产生相反的最小脱离向量，禁止 FindTeleportSpot 将胶囊移到单块障碍外。
    auto* Blocker = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(100, 1000, 1000));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Blocker->SetActorLocation(FVector(2600, 0, 100));
    auto* OtherBox = NewObject<UBoxComponent>(Blocker);
    OtherBox->SetupAttachment(Box);
    OtherBox->SetRelativeLocation(FVector(200, 0, 0));
    OtherBox->SetBoxExtent(FVector(100, 1000, 1000));
    OtherBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    OtherBox->SetCollisionObjectType(ECC_WorldStatic);
    OtherBox->SetCollisionResponseToAllChannels(ECR_Block);
    OtherBox->RegisterComponent();
    World->bActorsInitialized = true;
    const FTransform SpawnAt(FRotator::ZeroRotator, FVector(2700, 0, 100));
    auto* Rejected = Director->SpawnSummonedEnemy(Source, AEnemyCharacter::StaticClass(), SpawnAt);
    TestNull(TEXT("Collision-rejected summon returns null"), Rejected);
    TestTrue(TEXT("Failed spawn does not fail the encounter"), Director->Model.Encounter.Error.IsEmpty());
    TestEqual(TEXT("Only source remains registered"), Director->Model.Encounter.NumLiving(), 1);
    TestEqual(TEXT("No rejected summon retained by director"), Director->ActorTargets.Num(), 1);
    TestEqual(TEXT("No rejected summon retained as optional"), Director->OptionalActors.Num(), 0);

    Blocker->Destroy();
    auto* Minion = Director->SpawnSummonedEnemy(Source, AEnemyCharacter::StaticClass(), SpawnAt);
    if (TestNotNull(TEXT("Clear position successfully spawns summon"), Minion))
    {
        TestEqual(TEXT("Successful summon is counted"), Director->Model.Encounter.NumLiving(), 2);
        Minion->FOnDeath.Broadcast(Minion);
        Minion->Destroy();
        TestTrue(TEXT("Death then destruction resolves once without failure"),
                 Director->Model.Encounter.Error.IsEmpty());
        TestEqual(TEXT("Death removes optional target"), Director->Model.Encounter.NumLiving(), 1);
    }
    Minion = Director->SpawnSummonedEnemy(Source, AEnemyCharacter::StaticClass(), SpawnAt);
    if (TestNotNull(TEXT("Source cleanup fixture spawned"), Minion))
    {
        Director->RetireSummonedEnemy(Source, Minion);
        TestTrue(TEXT("Source-owned cleanup does not fail the run"), Director->Model.Encounter.Error.IsEmpty());
        TestEqual(TEXT("Source cleanup removes optional target"), Director->Model.Encounter.NumLiving(), 1);
    }
    Minion = Director->SpawnSummonedEnemy(Source, AEnemyCharacter::StaticClass(), SpawnAt);
    if (TestNotNull(TEXT("Unexpected removal fixture spawned"), Minion))
    {
        Minion->Destroy();
        TestTrue(TEXT("Established target destruction still fails"),
                 Director->Model.Encounter.Error.StartsWith(TEXT("TargetRemovedWithoutKill:")));
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
