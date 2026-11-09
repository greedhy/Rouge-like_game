// 自动化入口 → 共享掉落纯规则 / 真实目录生成 / 死亡与池复用 / 房间图等级，验证不依赖 PIE 输入。
#include "Misc/AutomationTest.h"
#include "Equipment/SharedLootSubsystem.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/ItemDefinition.h"
#include "Backpack/WorldPickupComponent.h"
#include "Character/EnemyCharacter.h"
#include "Roguelike/DungeonRunModel.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedLootRulesTest, "test_25d.Loot.SharedRules",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// 验证边界、独立概率分布及固定种子的可复现性。
bool FSharedLootRulesTest::RunTest(const FString&)
{
    auto* Rules = NewObject<USharedLootSettings>();
    FRandomStream Random(81373);
    int32 Health = 0;
    int32 Mana = 0;
    int32 Both = 0;
    for (int32 Index = 0; Index < 10000; ++Index)
    {
        const auto Normal = FSharedLootCounts::Roll(*Rules, EEnemyLootRank::Normal, Random);
        Health += Normal.HealthPotions;
        Mana += Normal.ManaPotions;
        Both += Normal.HealthPotions && Normal.ManaPotions;
        TestEqual(TEXT("Normals do not drop equipment"), Normal.Equipment, 0);
        const auto Elite = FSharedLootCounts::Roll(*Rules, EEnemyLootRank::Elite, Random);
        TestTrue(TEXT("Elite 1-3 consumables"),
                 Elite.HealthPotions + Elite.ManaPotions >= 1 && Elite.HealthPotions + Elite.ManaPotions <= 3);
        TestEqual(TEXT("Elite one equipment"), Elite.Equipment, 1);
        const auto Boss = FSharedLootCounts::Roll(*Rules, EEnemyLootRank::Boss, Random);
        TestTrue(TEXT("Boss 2-4 consumables"),
                 Boss.HealthPotions + Boss.ManaPotions >= 2 && Boss.HealthPotions + Boss.ManaPotions <= 4);
        TestEqual(TEXT("Boss two equipment"), Boss.Equipment, 2);
        const auto None = FSharedLootCounts::Roll(*Rules, EEnemyLootRank::None, Random);
        TestEqual(TEXT("No loot policy"), None.HealthPotions + None.ManaPotions + None.Equipment, 0);
    }
    TestTrue(TEXT("Health near 10 percent"), Health > 850 && Health < 1150);
    TestTrue(TEXT("Mana near 10 percent"), Mana > 850 && Mana < 1150);
    TestTrue(TEXT("Rolls independent: both near 1 percent"), Both > 60 && Both < 140);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedLootAssetsTest, "test_25d.Loot.AuthoredAffixedEquipment",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// 遍历真实目录的每件随机装备与当前等级范围，保证不会奖励无词缀装备。
bool FSharedLootAssetsTest::RunTest(const FString&)
{
    const auto* Rules = GetDefault<USharedLootSettings>();
    auto* Catalog = Cast<UBackpackItemDefinitions>(Rules->ItemCatalog.TryLoad());
    if (!TestNotNull(TEXT("Shared catalog"), Catalog) ||
        !TestNotNull(TEXT("Equipment table"), Catalog->EquipmentTable.Get()))
    {
        return false;
    }
    TArray<FBackpackItemDefEntry*> Rows;
    Catalog->EquipmentTable->GetAllRows(TEXT("LootTests"), Rows);
    int32 Tested = 0;
    for (const auto* Def : Rows)
    {
        if (!Def->bRandomizedEquipment)
        {
            continue;
        }
        ++Tested;
        for (int32 Level = 1; Level <= Rules->MaximumEquipmentLevel; ++Level)
        {
            for (int32 Seed = 0; Seed < 8; ++Seed)
            {
                FEquipmentRollData Roll;
                FString Error;
                if (!TestTrue(Def->ItemDefinitionId.ToString() + FString::Printf(TEXT(" L%d seed%d: "), Level, Seed) +
                                  Error,
                              FEquipmentGenerator::Generate(*Catalog, *Def, Level, Seed, Roll, Error, true)))
                {
                    return false;
                }
                TestFalse(TEXT("Guaranteed affix exists"), Roll.Affixes.IsEmpty());
                TestEqual(TEXT("Source level preserved"), Roll.ItemLevel, Level);
                TestTrue(TEXT("Roll matches definition"), FEquipmentGenerator::ValidateForDefinition(Def, Roll));
            }
        }
    }
    TestTrue(TEXT("Real randomized equipment coverage"), Tested >= 10);
    for (const auto& Path : Rules->EliteClasses)
    {
        auto* Class = Path.TryLoadClass<AEnemyCharacter>();
        TestNotNull(*Path.ToString(), Class);
        TestTrue(TEXT("Shared elite classification"),
                 Rules->ResolveRank(Class, EEnemyLootRank::Normal) == EEnemyLootRank::Elite);
    }
    for (const auto& Path : Rules->BossClasses)
    {
        auto* Class = Path.TryLoadClass<AEnemyCharacter>();
        TestNotNull(*Path.ToString(), Class);
        TestTrue(TEXT("Shared boss classification"),
                 Rules->ResolveRank(Class, EEnemyLootRank::Normal) == EEnemyLootRank::Boss);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedLootWorldTest, "test_25d.Loot.DeathPoolRoomAndMass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// 两条池生命各结算一次，清房与死亡各自去重，Mass 击杀没有掉落；检查真实拾取实例。
bool FSharedLootWorldTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Loot = World->GetSubsystem<USharedLootSubsystem>();
    if (!TestNotNull(TEXT("World loot subsystem"), Loot))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    Enemy->LootRank = EEnemyLootRank::Elite;
    Enemy->Level = 7;
    Enemy->EnableDungeonPooling(0.f);
    Enemy->Die(FVector::ZeroVector);
    const int32 First = Loot->GetPendingCount();
    TestTrue(TEXT("Elite queues 2-4 total items"), First >= 2 && First <= 4);
    Enemy->Die(FVector::ZeroVector);
    TestEqual(TEXT("Repeated death cannot duplicate"), Loot->GetPendingCount(), First);
    Enemy->SleepForDungeonPool();
    Enemy->Level = 11;
    TestTrue(TEXT("New pool life"), Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("SecondRoom")));
    Enemy->Die(FVector::ZeroVector);
    const int32 Second = Loot->GetPendingCount();
    TestTrue(TEXT("New life earns separate loot"), Second >= First + 2 && Second <= First + 4);
    const FGuid Run = FGuid::NewGuid();
    Loot->QueueRoomLoot(Run, TEXT("CombatRoom"), 9, FVector::ZeroVector);
    Loot->QueueRoomLoot(Run, TEXT("CombatRoom"), 9, FVector::ZeroVector);
    TestEqual(TEXT("Room clear awards exactly two once"), Loot->GetPendingCount(), Second + 2);
    auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(World);
    const int32 MassId = Mass->SpawnMonster(Type, FVector(500, 0, 100), NAME_None, 12);
    TestTrue(TEXT("Mass spawned with room level"), MassId != INDEX_NONE);
    if (MassId != INDEX_NONE)
    {
        TestEqual(TEXT("Mass level stored per entity"),
                  World->GetSubsystem<UMassEntitySubsystem>()
                      ->GetMutableEntityManager()
                      .GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(MassId))
                      .Level,
                  12);
        TestEqual(TEXT("Shared Mass type unchanged"), Type->Level, 1);
    }
    FPureMassDamage Damage;
    Damage.Amount = 100000.f;
    Mass->ApplyDamage(MassId, Damage);
    FPureMassView MassView;
    TestFalse(TEXT("Mass actually killed"), Mass->Resolve(MassId, MassView));
    TestEqual(TEXT("Mass kill cannot enqueue loot"), Loot->GetPendingCount(), Second + 2);
    for (int32 Step = 0; Step < 5; ++Step)
    {
        Loot->Tick(1.f);
    }
    TestEqual(TEXT("All real pickup classes spawned"), Loot->GetPendingCount(), 0);
    TSet<FGuid> Instances;
    TMap<int32, int32> EquipmentLevels;
    int32 Consumables = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (const auto* Pickup = It->FindComponentByClass<UWorldPickupComponent>())
        {
            TestTrue(TEXT("Loot replicates"), It->GetIsReplicated());
            if (Pickup->RollData.GenerationVersion != 0)
            {
                TestTrue(TEXT("Unique equipment identity"),
                         Pickup->ItemInstanceId.IsValid() && !Instances.Contains(Pickup->ItemInstanceId));
                Instances.Add(Pickup->ItemInstanceId);
                TestFalse(TEXT("Ground equipment has affix"), Pickup->RollData.Affixes.IsEmpty());
                ++EquipmentLevels.FindOrAdd(Pickup->RollData.ItemLevel);
            }
            else
            {
                ++Consumables;
            }
        }
    }
    TestEqual(TEXT("First life item level snapshot"), EquipmentLevels.FindRef(7), 1);
    TestEqual(TEXT("Second life item level snapshot"), EquipmentLevels.FindRef(11), 1);
    TestEqual(TEXT("Room equipment level"), EquipmentLevels.FindRef(9), 2);
    TestEqual(TEXT("Potion count preserved"), Consumables, Second - 2);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedLootRoomLevelTest, "test_25d.Loot.RoomDepthLevels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// 非线性房间顺序也按入口距离分级，角色身份提供额外等级。
bool FSharedLootRoomLevelTest::RunTest(const FString&)
{
    FDungeonLayout Layout;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto& Room = Layout.Rooms.AddDefaulted_GetRef();
        Room.RoomId = FName(*FString::FromInt(Index));
        const FIntPoint Cells[] = {FIntPoint(0, 0), FIntPoint(2, 0), FIntPoint(1, 1), FIntPoint(1, 0)};
        Room.Cell = Cells[Index];
        Room.Role = Index == 0 ? EDungeonRoomRole::Entrance : EDungeonRoomRole::Combat;
    }
    for (const FIntPoint Edge : {FIntPoint(0, 3), FIntPoint(3, 1), FIntPoint(3, 2)})
    {
        auto& Connection = Layout.Connections.AddDefaulted_GetRef();
        Connection.From = Edge.X;
        Connection.To = Edge.Y;
    }
    FDungeonRunModel Model;
    TestTrue(TEXT("Valid branch graph"), Model.Initialize(Layout));
    if (Model.Rooms.Num() != 4)
    {
        return false;
    }
    const auto* Rules = GetDefault<USharedLootSettings>();
    TestEqual(TEXT("Entrance level"), Model.Rooms[0].RoomLevel, Rules->RoomLevel(0));
    TestEqual(TEXT("First depth despite index 3"), Model.Rooms[3].RoomLevel, Rules->RoomLevel(1));
    TestEqual(TEXT("Branch level"), Model.Rooms[1].RoomLevel, Rules->RoomLevel(2));
    TestEqual(TEXT("Same depth equal level"), Model.Rooms[1].RoomLevel, Model.Rooms[2].RoomLevel);
    TestEqual(TEXT("Elite level"), Rules->MonsterLevel(5, EEnemyLootRank::Elite), 6);
    TestEqual(TEXT("Boss level"), Rules->MonsterLevel(5, EEnemyLootRank::Boss), 7);
    return true;
}
#endif
