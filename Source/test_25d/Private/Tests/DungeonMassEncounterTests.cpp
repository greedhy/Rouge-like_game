/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Mass.KilledNotificationIsImmediateAndUnique
 * - test_25d.Roguelike.Mass.RecycleDoesNotAwardKillOrXP
 * - test_25d.Roguelike.Mass.BoundsValidateSpawnsAndDamageSources
 * - test_25d.Roguelike.Mass.TargetsOnlyLivingPlayersInsideRoom
 * - test_25d.Roguelike.Mass.MovementKeepsWholeCapsuleInsideRoom
 * - test_25d.Roguelike.Mass.ExternalEntityDestructionIsUnexpected
 * - test_25d.Roguelike.Mass.ClientCannotRemoveOrRegisterBounds
 * - test_25d.Roguelike.Mass.WarningLifecycle
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "MyGameplayTags.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace DungeonMassEncounterTests
{
    struct FRemoval
    {
        int32 Id;
        FName Room;
        EPureMassRemovalReason Reason;
    };

    struct FWorld
    {
        UWorld* World = nullptr;
        UPureMassSubsystem* Sim = nullptr;
        UMassEntitySubsystem* Mass = nullptr;
        UPureMassMonsterType* Type = nullptr;
        UPureMassProcessor* Processor = nullptr;

        explicit FWorld(bool bClient = false)
        {
            const EWorldType::Type WorldType = bClient ? EWorldType::PIE : EWorldType::Game;
            World = UWorld::CreateWorld(WorldType, false);
            GEngine->CreateNewWorldContext(WorldType).SetCurrentWorld(World);
#if WITH_EDITOR
            if (bClient)
                World->SetPlayInEditorInitialNetMode(NM_Client);
#endif
            Sim = World->GetSubsystem<UPureMassSubsystem>();
            Mass = World->GetSubsystem<UMassEntitySubsystem>();
            if (Sim)
                Sim->Enable();
            Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 13;
            Type->Radius = 32.f;
            Type->HalfHeight = 60.f;
            Type->Speed = 1000.f;
            Type->AttackRange = 60.f;
        }
        ~FWorld()
        {
            // Captured test locals can be destroyed before World; teardown is not a gameplay removal.
            if (Sim)
                Sim->OnMonsterRemoved.Clear();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        bool Ready(FAutomationTestBase& Test) const
        {
            return Test.TestNotNull(TEXT("Real simulation subsystem"), Sim) &&
                   Test.TestNotNull(TEXT("Real Mass entity subsystem"), Mass);
        }
        ACharacter* Character(FVector Location)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Pawn = World->SpawnActor<ACharacter>(Location, FRotator::ZeroRotator, Params);
            if (Pawn)
                Pawn->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            return Pawn;
        }
        void Player(APawn* Pawn)
        {
            auto* Controller = World->SpawnActor<APlayerController>();
            World->AddController(Controller);
            Controller->SetPawn(Pawn);
        }
        UAbilitySystemComponent* AddASC(AActor* Actor)
        {
            auto* ASC = NewObject<UAbilitySystemComponent>(Actor);
            Actor->AddInstanceComponent(ASC);
            ASC->RegisterComponent();
            ASC->InitAbilityActorInfo(Actor, Actor);
            return ASC;
        }
        void Step(float DeltaTime = .1f)
        {
            auto& Manager = Mass->GetMutableEntityManager();
            if (!Processor)
            {
                Processor = NewObject<UPureMassProcessor>(World);
                Processor->CallInitialize(World, Manager.AsShared());
            }
            auto Context = Manager.CreateExecutionContext(DeltaTime);
            Context.SetExecutionType(EMassExecutionContextType::Processor);
            Processor->CallExecute(Manager, Context);
        }
    };
} // namespace DungeonMassEncounterTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassKilledOnce,
                                 "test_25d.Roguelike.Mass.KilledNotificationIsImmediateAndUnique",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.KilledNotificationIsImmediateAndUnique 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassKilledOnce::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    const FName Room(TEXT("R07"));
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(0, 0, 60), Room);
    if (!TestTrue(TEXT("Fixture entity spawned"), Id > 0))
        return false;
    const auto Handle = F.Sim->GetEntity(Id);
    auto* Source = F.Character(FVector(50, 0, 60));
    auto* ASC = F.AddASC(Source);
    int32 XPEvents = 0;
    float XP = 0.f;
    ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
        .AddLambda(
            [&](const FGameplayEventData* Event)
            {
                ++XPEvents;
                XP += Event->EventMagnitude;
            });
    TArray<FRemoval> Removals;
    bool bTombstonedBeforeCallback = false;
    bool bPhysicalEntityStillPresent = false;
    F.Sim->OnMonsterRemoved.AddLambda(
        [&](int32 Removed, FName RemovedRoom, EPureMassRemovalReason Reason)
        {
            Removals.Add({Removed, RemovedRoom, Reason});
            FPureMassView View;
            FPureMassDamage Reentrant;
            Reentrant.Amount = 999.f;
            Reentrant.Source = Source;
            bTombstonedBeforeCallback = !F.Sim->Resolve(Removed, View) &&
                                        !F.Sim->QueryRadius(FVector(0, 0, 60), 100.f).Contains(Removed) &&
                                        F.Sim->ApplyDamage(Removed, Reentrant) == 0.f && !F.Sim->RemoveMonster(Removed);
            bPhysicalEntityStillPresent = F.Mass->GetMutableEntityManager().IsEntityValid(Handle);
        });
    FPureMassDamage Lethal;
    Lethal.Amount = 999.f;
    Lethal.Source = Source;
    TestEqual(TEXT("Lethal damage removes actual health"), F.Sim->ApplyDamage(Id, Lethal), 100.f);
    TestEqual(TEXT("Exactly one immediate lifecycle notification"), Removals.Num(), 1);
    if (Removals.Num() == 1)
    {
        TestEqual(TEXT("Death carries exact entity id"), Removals[0].Id, Id);
        TestEqual(TEXT("Death carries originating room"), Removals[0].Room, Room);
        TestTrue(TEXT("Only lethal damage reports Killed"), Removals[0].Reason == EPureMassRemovalReason::Killed);
    }
    TestTrue(TEXT("Callback cannot see or remove a live target again"), bTombstonedBeforeCallback);
    TestTrue(TEXT("Entity destruction waits until FinishUpdate"), bPhysicalEntityStillPresent);
    TestEqual(TEXT("Duplicate damage cannot award XP"), F.Sim->ApplyDamage(Id, Lethal), 0.f);
    TestFalse(TEXT("Duplicate cleanup fails"), F.Sim->RemoveMonster(Id));
    TestEqual(TEXT("One XP event for a real kill"), XPEvents, 1);
    TestEqual(TEXT("Real kill retains the type XP amount"), XP, 13.f);
    F.Sim->FinishUpdate();
    F.Sim->FinishUpdate();
    TestFalse(TEXT("FinishUpdate physically destroys killed entity"),
              F.Mass->GetMutableEntityManager().IsEntityValid(Handle));
    TestEqual(TEXT("Physical cleanup emits no second removal"), Removals.Num(), 1);
    ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP).Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassRecycleNoKill, "test_25d.Roguelike.Mass.RecycleDoesNotAwardKillOrXP",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.RecycleDoesNotAwardKillOrXP 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassRecycleNoKill::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(0, 0, 60), TEXT("R08"));
    if (!TestTrue(TEXT("Fixture entity spawned"), Id > 0))
        return false;
    const auto Handle = F.Sim->GetEntity(Id);
    auto* Source = F.Character(FVector(50, 0, 60));
    auto* ASC = F.AddASC(Source);
    int32 XPEvents = 0;
    ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
        .AddLambda(
            [&](const FGameplayEventData*)
            {
                ++XPEvents;
            });
    // A living target with an attributed DOT must not turn recycling into a credited death.
    FPureMassDamage Dot;
    Dot.Source = Source;
    Dot.StatusDuration = 5.f;
    Dot.DamagePerSecond = 1.f;
    F.Sim->ApplyDamage(Id, Dot);
    TArray<FRemoval> Removals;
    F.Sim->OnMonsterRemoved.AddLambda(
        [&](int32 Removed, FName Room, EPureMassRemovalReason Reason)
        {
            Removals.Add({Removed, Room, Reason});
        });
    TestFalse(TEXT("Public caller cannot forge a kill"), F.Sim->RemoveMonster(Id, EPureMassRemovalReason::Killed));
    FPureMassView View;
    TestTrue(TEXT("Rejected forged kill leaves target live"), F.Sim->Resolve(Id, View));
    TestTrue(TEXT("Default removal recycles live entity"), F.Sim->RemoveMonster(Id));
    TestFalse(TEXT("Recycled entity immediately leaves lookup"), F.Sim->Resolve(Id, View));
    TestFalse(TEXT("Recycled entity immediately leaves spatial queries"),
              F.Sim->QueryRadius(FVector(0, 0, 60), 100.f).Contains(Id));
    TestTrue(TEXT("Recycle defers physical destruction"), F.Mass->GetMutableEntityManager().IsEntityValid(Handle));
    TestFalse(TEXT("Repeated recycling fails"), F.Sim->RemoveMonster(Id));
    TestFalse(TEXT("Unknown id cannot be removed"), F.Sim->RemoveMonster(MAX_int32));
    F.Sim->FinishUpdate();
    TestFalse(TEXT("Recycled entity eventually destroyed"), F.Mass->GetMutableEntityManager().IsEntityValid(Handle));
    TestEqual(TEXT("Recycling does not increment kill count"), F.Sim->GetDeathCount(), int64(0));
    TestEqual(TEXT("Recycling emits no XP event"), XPEvents, 0);
    TestEqual(TEXT("One recycle disposition"), Removals.Num(), 1);
    if (Removals.Num() == 1)
    {
        TestEqual(TEXT("Recycle retains room identity"), Removals[0].Room, FName(TEXT("R08")));
        TestTrue(TEXT("Default reason is Recycled"), Removals[0].Reason == EPureMassRemovalReason::Recycled);
    }
    ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP).Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassBoundsAndDamage,
                                 "test_25d.Roguelike.Mass.BoundsValidateSpawnsAndDamageSources",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.BoundsValidateSpawnsAndDamageSources 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassBoundsAndDamage::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    const FName Room(TEXT("R09"));
    const FVector Extent(200, 200, 200);
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Empty room is reserved for unrestricted content"),
              F.Sim->SetRoomBounds(NAME_None, FVector::ZeroVector, Extent));
    TestFalse(TEXT("NaN centre rejected"), F.Sim->SetRoomBounds(Room, FVector(NaN, 0, 0), Extent));
    TestFalse(TEXT("Infinite extent rejected"),
              F.Sim->SetRoomBounds(Room, FVector::ZeroVector, FVector(Infinity, 200, 200)));
    TestFalse(TEXT("Negative extent rejected"), F.Sim->SetRoomBounds(Room, FVector::ZeroVector, FVector(-1, 200, 200)));
    TestFalse(TEXT("Zero extent rejected"), F.Sim->SetRoomBounds(Room, FVector::ZeroVector, FVector(0, 200, 200)));
    TestTrue(TEXT("Valid room registered"), F.Sim->SetRoomBounds(Room, FVector::ZeroVector, Extent));
    TestTrue(TEXT("Capsule exactly touching positive X boundary fits"),
             F.Sim->SpawnMonster(F.Type, FVector(168, 0, 60), Room) > 0);
    TestTrue(TEXT("Capsule exactly touching negative Y boundary fits"),
             F.Sim->SpawnMonster(F.Type, FVector(0, -168, 60), Room) > 0);
    TestEqual(TEXT("Capsule protruding beyond X boundary rejected"),
              F.Sim->SpawnMonster(F.Type, FVector(168.1, 0, 60), Room), INDEX_NONE);
    TestEqual(TEXT("Capsule protruding beyond Y boundary rejected"),
              F.Sim->SpawnMonster(F.Type, FVector(0, -168.1, 60), Room), INDEX_NONE);
    TestFalse(TEXT("Invalid replacement is rejected"),
              F.Sim->SetRoomBounds(Room, FVector::ZeroVector, FVector(-1, 1, 1)));
    TestEqual(TEXT("Invalid replacement preserves previous boundary"),
              F.Sim->SpawnMonster(F.Type, FVector(500, 0, 60), Room), INDEX_NONE);
    TestTrue(TEXT("Unregistered rooms retain unrestricted spawns"),
             F.Sim->SpawnMonster(F.Type, FVector(500, 0, 60), TEXT("Legacy")) > 0);
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(0, 0, 60), Room);
    if (!TestTrue(TEXT("Damage fixture target exists"), Id > 0))
        return false;
    auto* Source = F.Character(FVector(250, 0, 60));
    FPureMassDamage Damage;
    Damage.Source = Source;
    Damage.Amount = 10.f;
    Damage.StatusDuration = 3.f;
    Damage.DamagePerSecond = 10.f;
    TestEqual(TEXT("Outside source cannot damage this room"), F.Sim->ApplyDamage(Id, Damage), 0.f);
    TestEqual(TEXT("Rejected source cannot apply status"),
              F.Mass->GetMutableEntityManager()
                  .GetFragmentDataChecked<FPureMassFragment>(F.Sim->GetEntity(Id))
                  .StatusRemaining,
              0.f);
    Source->SetActorLocation(FVector(50, 0, 60));
    Damage.StatusDuration = 0.f;
    TestEqual(TEXT("Inside source damage remains legal"), F.Sim->ApplyDamage(Id, Damage), 10.f);
    Damage.Source.Reset();
    TestEqual(TEXT("Unattributed environmental damage remains legal"), F.Sim->ApplyDamage(Id, Damage), 10.f);
    F.Sim->ClearRoomBounds(Room);
    TestTrue(TEXT("ClearRoomBounds restores unrestricted spawning"),
             F.Sim->SpawnMonster(F.Type, FVector(500, 0, 60), Room) > 0);
    Source->SetActorLocation(FVector(250, 0, 60));
    Damage.Source = Source;
    TestEqual(TEXT("ClearRoomBounds restores unrestricted source damage"), F.Sim->ApplyDamage(Id, Damage), 10.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassTargetIsolation,
                                 "test_25d.Roguelike.Mass.TargetsOnlyLivingPlayersInsideRoom",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.TargetsOnlyLivingPlayersInsideRoom 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassTargetIsolation::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    TestTrue(TEXT("Room bounds registered"),
             F.Sim->SetRoomBounds(TEXT("R10"), FVector::ZeroVector, FVector(200, 200, 200)));
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(160, 0, 60), TEXT("R10"));
    if (!TestTrue(TEXT("Targeting fixture entity spawned"), Id > 0))
        return false;
    auto* Outside = F.Character(FVector(201, 0, 60));
    F.Player(Outside);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Inside = F.World->SpawnActor<AEnemyCharacter>(FVector(-100, 0, 60), FRotator::ZeroRotator, Params);
    Inside->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    F.Player(Inside); // SetPawn avoids starting enemy AI; CombatInterface supplies real alive/dead state.
    F.Step();
    auto& Fragment = F.Mass->GetMutableEntityManager().GetFragmentDataChecked<FPureMassFragment>(F.Sim->GetEntity(Id));
    TestTrue(TEXT("Farther inside player selected instead of nearest outside player"),
             Fragment.PathGoal.Equals(FVector(-100, 0, 60), .01));
    TestEqual(TEXT("Outside player is not attacked through room boundary"), Fragment.AttackRemaining, 0.f);
    auto* DeadProperty = FindFProperty<FBoolProperty>(Inside->GetClass(), TEXT("bDead"));
    if (!TestNotNull(TEXT("Combat character has dead state"), DeadProperty))
        return false;
    DeadProperty->SetPropertyValue_InContainer(Inside, true);
    Fragment.AttackRemaining = 0.f;
    F.Step();
    TestEqual(TEXT("No legal living target means no outside-player attack"), Fragment.AttackRemaining, 0.f);
    TestTrue(TEXT("No legal living target means no movement"), Fragment.Velocity.IsNearlyZero());
    F.Sim->ClearRoomBounds(TEXT("R10"));
    F.Step();
    TestTrue(TEXT("Clearing room policy restores legacy nearest-player attack"), Fragment.AttackRemaining > 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassMovementBounds,
                                 "test_25d.Roguelike.Mass.MovementKeepsWholeCapsuleInsideRoom",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.MovementKeepsWholeCapsuleInsideRoom 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassMovementBounds::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    F.Type->AttackRange = 1.f;
    TestTrue(TEXT("Room bounds registered"),
             F.Sim->SetRoomBounds(TEXT("R11"), FVector::ZeroVector, FVector(200, 200, 200)));
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(160, 160, 60), TEXT("R11"));
    if (!TestTrue(TEXT("Movement fixture entity spawned"), Id > 0))
        return false;
    auto* Player = F.Character(FVector(0, 150, 60));
    F.Player(Player);
    auto& Fragment = F.Mass->GetMutableEntityManager().GetFragmentDataChecked<FPureMassFragment>(F.Sim->GetEntity(Id));
    // Reproduce a cached path corner outside the room without depending on navigation build timing.
    Fragment.Path = {FVector(500, 500, 60)};
    Fragment.PathGoal = Player->GetActorLocation();
    Fragment.NextPathTime = MAX_flt;
    F.Step(.25f);
    FPureMassView View;
    TestTrue(TEXT("Moving entity still resolves"), F.Sim->Resolve(Id, View));
    TestTrue(TEXT("Positive X capsule edge stays inside 200 cm boundary"), View.Location.X <= 168.01);
    TestTrue(TEXT("Positive Y capsule edge stays inside 200 cm boundary"), View.Location.Y <= 168.01);
    TestTrue(TEXT("Negative edges also remain inside"), View.Location.X >= -168.01 && View.Location.Y >= -168.01);
    F.Sim->ClearRoomBounds(TEXT("R11"));
    Fragment.Path = {FVector(500, 500, 60)};
    Fragment.PathPoint = 0;
    Fragment.PathGoal = Player->GetActorLocation();
    Fragment.NextPathTime = MAX_flt;
    F.Step(.25f);
    F.Sim->Resolve(Id, View);
    TestTrue(TEXT("Unbounded legacy movement can continue past former room"),
             View.Location.X > 168.01 || View.Location.Y > 168.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassExternalRemoval,
                                 "test_25d.Roguelike.Mass.ExternalEntityDestructionIsUnexpected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.ExternalEntityDestructionIsUnexpected 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassExternalRemoval::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(0, 0, 60), TEXT("R12"));
    if (!TestTrue(TEXT("External destruction fixture spawned"), Id > 0))
        return false;
    TArray<FRemoval> Removals;
    F.Sim->OnMonsterRemoved.AddLambda(
        [&](int32 Removed, FName Room, EPureMassRemovalReason Reason)
        {
            Removals.Add({Removed, Room, Reason});
        });
    F.Mass->GetMutableEntityManager().DestroyEntity(F.Sim->GetEntity(Id));
    F.Step();
    F.Step();
    FPureMassView View;
    TestFalse(TEXT("External destruction cannot leave stale live snapshot"), F.Sim->Resolve(Id, View));
    TestFalse(TEXT("External destruction cannot leave stale spatial hit"),
              F.Sim->QueryRadius(FVector(0, 0, 60), 100.f).Contains(Id));
    TestEqual(TEXT("External destruction emits one disposition"), Removals.Num(), 1);
    if (Removals.Num() == 1)
    {
        TestEqual(TEXT("Unexpected removal retains id"), Removals[0].Id, Id);
        TestEqual(TEXT("Unexpected removal retains original room"), Removals[0].Room, FName(TEXT("R12")));
        TestTrue(TEXT("External removal is not a kill"), Removals[0].Reason == EPureMassRemovalReason::Unexpected);
    }
    TestFalse(TEXT("Already missing entity cannot be recycled"), F.Sim->RemoveMonster(Id));
    TestEqual(TEXT("Unexpected removal does not increment kills"), F.Sim->GetDeathCount(), int64(0));
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassClientAuthority,
                                 "test_25d.Roguelike.Mass.ClientCannotRemoveOrRegisterBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.ClientCannotRemoveOrRegisterBounds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassClientAuthority::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F(true);
    if (!F.Ready(*this))
        return false;
    TestEqual(TEXT("Fixture is a real client world"), F.World->GetNetMode(), NM_Client);
    FPureMassView Snapshot;
    Snapshot.Id = 42;
    Snapshot.Type = F.Type;
    Snapshot.Health = Snapshot.MaxHealth = 100.f;
    F.Sim->SetClientSnapshots({Snapshot});
    int32 RemovalCount = 0;
    F.Sim->OnMonsterRemoved.AddLambda(
        [&](int32, FName, EPureMassRemovalReason)
        {
            ++RemovalCount;
        });
    TestFalse(TEXT("Client cannot register encounter boundary"),
              F.Sim->SetRoomBounds(TEXT("R13"), FVector::ZeroVector, FVector(200, 200, 200)));
    TestFalse(TEXT("Client cannot recycle server entity"), F.Sim->RemoveMonster(42));
    TestFalse(TEXT("Client cannot forge unexpected removal"),
              F.Sim->RemoveMonster(42, EPureMassRemovalReason::Unexpected));
    F.Sim->ClearRoomBounds(TEXT("R13"));
    F.Sim->FinishUpdate();
    FPureMassView View;
    TestTrue(TEXT("Rejected client removal preserves authoritative snapshot"), F.Sim->Resolve(42, View));
    TestEqual(TEXT("Rejected client mutation emits no disposition"), RemovalCount, 0);
    return true;
}
#endif
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMassWarningLifecycle, "test_25d.Roguelike.Mass.WarningLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Mass.WarningLifecycle 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMassWarningLifecycle::RunTest(const FString&)
{
    using namespace DungeonMassEncounterTests;
    FWorld F;
    if (!F.Ready(*this))
        return false;
    F.Type->AttackWindupSeconds = .35f;
    F.Type->AttackContactTime = .29f;
    F.Type->AttackClipDuration = 1.208333f;
    const int32 Id = F.Sim->SpawnMonster(F.Type, FVector(0, 0, 60));
    auto* Player = F.Character(FVector(30, 0, 60));
    F.Player(Player);
    auto& Fragment = F.Mass->GetMutableEntityManager().GetFragmentDataChecked<FPureMassFragment>(F.Sim->GetEntity(Id));
    F.Step(.1f);
    TestEqual(TEXT("First update warns rather than impacts"), int32(Fragment.State), 4);
    TestEqual(TEXT("No attack cooldown before impact"), Fragment.AttackRemaining, 0.f);
    F.Step(.1f);
    F.Step(.1f);
    TestEqual(TEXT("Windup remains visible"), int32(Fragment.State), 4);
    Player->SetActorLocation(FVector(200, 0, 60));
    F.Step(.1f);
    TestFalse(TEXT("Escaping range cancels pending attack"), Fragment.PendingAttackTarget.IsValid());
    Player->SetActorLocation(FVector(30, 0, 60));
    F.Step(.1f);
    TestEqual(TEXT("Reentry restarts full warning"), int32(Fragment.State), 4);
    Fragment.PetrifyRemaining = .2f;
    F.Step(.1f);
    TestFalse(TEXT("Petrify cancels pending impact"), Fragment.PendingAttackTarget.IsValid());
    F.Step(.1f);
    F.Step(.1f);
    F.Step(.1f);
    F.Step(.1f);
    F.Step(.1f);
    F.Step(.1f);
    TestTrue(TEXT("Full new warning allows impact cooldown"), Fragment.AttackRemaining > 0.f);
    TestTrue(TEXT("Strike recovery preserves the remainder of the spear clip"), Fragment.AttackVisualRemaining > .8f);
    Player->SetActorLocation(FVector(250, 0, 60));
    F.Step(.2f);
    TestEqual(TEXT("Target leaving after impact does not truncate follow-through"), int32(Fragment.State), 2);
    Fragment.PetrifyRemaining = .2f;
    F.Step(.1f);
    TestEqual(TEXT("Petrify interrupts attack presentation"), int32(Fragment.State), 0);
    return true;
}

#endif
