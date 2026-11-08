/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Roster.Composition
 * - test_25d.Roguelike.Roster.RealTypesAndPoolQuota
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonRunProfile.h"
#include "Character/EnemyCharacter.h"
#include "Roguelike/DungeonEnemyPool.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonRosterTest, "test_25d.Roguelike.Roster.Composition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Roster.Composition 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonRosterTest::RunTest(const FString&)
{
    auto* Profile = NewObject<UDungeonRunProfile>();
    const TCHAR* Paths[] = {TEXT("/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C"),
                            TEXT("/Game/BluePrints/Characters/Goblin/BP_Goblin_Slinshot.BP_Goblin_Slinshot_C"),
                            TEXT("/Game/BluePrints/Characters/Ghoul/BP_Ghoul.BP_Ghoul_C")};
    for (int32 I = 0; I < 3; ++I)
    {
        FDungeonNormalEnemy Entry;
        Entry.Id = FName(*FString::Printf(TEXT("Type%d"), I));
        Entry.EnemyClass = TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(Paths[I]));
        Profile->NormalRoster.Add(Entry);
    }
    FString Error;
    TestTrue(TEXT("Valid roster accepted before loading"), Profile->ValidateNormalRoster(Error));
    bool Varied = false;
    const auto Baseline = Profile->BuildNormalComposition(42, 2, 0, 8);
    TestEqual(TEXT("Exact requested population"), Baseline.Num(), 8);
    TestTrue(TEXT("Identical inputs reproduce composition"), Baseline == Profile->BuildNormalComposition(42, 2, 0, 8));
    for (int32 Seed = 0; Seed < 32; ++Seed)
    {
        const auto Group = Profile->BuildNormalComposition(Seed, 2, 0, 8);
        Varied |= Group != Baseline;
        TSet<int32> Types;
        for (int32 I : Group)
        {
            TestTrue(TEXT("Only configured types"), I >= 0 && I < 3);
            Types.Add(I);
        }
        TestEqual(TEXT("Each normal wave includes three types"), Types.Num(), 3);
        for (int32 I = 0; I < 3; ++I)
            TestTrue(TEXT("Balanced population"), Group.FilterByPredicate(
                                                           [I](int32 V)
                                                           {
                                                               return V == I;
                                                           })
                                                          .Num() >= 2);
    }
    TestTrue(TEXT("Different seeds change ordering"), Varied);
    TestTrue(TEXT("Invalid count bounded"), Profile->BuildNormalComposition(42, 2, 0, 1000000).IsEmpty());
    Profile->NormalRoster[1].Id = Profile->NormalRoster[0].Id;
    TestFalse(TEXT("Duplicate IDs rejected"), Profile->ValidateNormalRoster(Error));
    TestFalse(TEXT("Validation supplies diagnostic"), Error.IsEmpty());
    Profile->NormalRoster[1].Id = TEXT("Other");
    Profile->NormalRoster[1].EnemyClass = Profile->NormalRoster[0].EnemyClass;
    TestFalse(TEXT("Duplicate classes rejected"), Profile->ValidateNormalRoster(Error));
    Profile->NormalRoster[1].EnemyClass = TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(Paths[1]));
    Profile->NormalRoster[1].Health = 0;
    TestFalse(TEXT("Invalid health rejected"), Profile->ValidateNormalRoster(Error));
    Profile->NormalRoster.Reset();
    TestTrue(TEXT("Legacy empty roster remains valid"), Profile->ValidateNormalRoster(Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonRosterPoolTest, "test_25d.Roguelike.Roster.RealTypesAndPoolQuota",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Roster.RealTypesAndPoolQuota 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonRosterPoolTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* Profile = NewObject<UDungeonRunProfile>();
    const TCHAR* Paths[] = {TEXT("/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C"),
                            TEXT("/Game/BluePrints/Characters/Goblin/BP_Goblin_Slinshot.BP_Goblin_Slinshot_C"),
                            TEXT("/Game/BluePrints/Characters/Ghoul/BP_Ghoul.BP_Ghoul_C")};
    for (int32 I = 0; I < 3; ++I)
    {
        auto* Class = LoadClass<AEnemyCharacter>(nullptr, Paths[I]);
        if (!TestNotNull(TEXT("Existing GAS Blueprint loads"), Class))
            return false;
        FDungeonNormalEnemy Entry;
        Entry.Id = FName(*FString::FromInt(I));
        Entry.EnemyClass = Class;
        Profile->NormalRoster.Add(Entry);
    }
    for (int32 Capacity : {0, 1, 2, 12, 13, 64})
    {
        Profile->NormalPoolCapacity = Capacity;
        int32 Sum = 0;
        for (const auto& Entry : Profile->NormalRoster)
            Sum += Profile->NormalPoolQuota(Entry.EnemyClass.Get());
        TestEqual(TEXT("Per-class reservations sum to global bound"), Sum, Capacity);
    }
    Profile->NormalPoolCapacity = 12;
    Profile->NormalRoster[2].bPoolEligible = false;
    TestEqual(TEXT("Unpooled type gets no slots"), Profile->NormalPoolQuota(Profile->NormalRoster[2].EnemyClass.Get()),
              0);
    TestEqual(TEXT("Remaining types share slots"), Profile->NormalPoolQuota(Profile->NormalRoster[0].EnemyClass.Get()),
              6);
    Profile->NormalRoster[2].bPoolEligible = true;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    FDungeonEnemyPool Pool;
    for (const auto& Entry : Profile->NormalRoster)
    {
        auto* Enemy = World->SpawnActor<AEnemyCharacter>(Entry.EnemyClass.Get());
        if (!TestNotNull(TEXT("Real type spawns"), Enemy))
            continue;
        TestNotNull(TEXT("GAS component present"), Enemy->GetAbilitySystemComponent());
        TestTrue(TEXT("Adopt under quota"), Pool.Adopt(Enemy, TEXT("R1"), 12));
        const auto Transform = Enemy->GetMesh()->GetRelativeTransform();
        Enemy->EnableDungeonPooling(0);
        for (int32 Lease = 0; Lease < 2; ++Lease)
        {
            Enemy->Die(FVector(100, 0, 0));
            TestTrue(TEXT("Type supports ragdoll"), Enemy->GetMesh()->IsSimulatingPhysics());
            Enemy->SleepForDungeonPool();
            TestTrue(TEXT("Idle type stops physics"), !Enemy->GetMesh()->IsSimulatingPhysics() && Enemy->IsHidden());
            TestTrue(TEXT("Type can wake"), Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("R2")));
            TestTrue(TEXT("Type restores mesh"), Enemy->GetMesh()->GetRelativeTransform().Equals(Transform));
            TestTrue(TEXT("Weapon stays attached"),
                     Enemy->GetWeapon_Implementation()->GetAttachParent() == Enemy->GetMesh());
        }
        TestEqual(TEXT("Type ledger counts one instance"), Pool.CountClass(Entry.EnemyClass.Get()), 1);
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
