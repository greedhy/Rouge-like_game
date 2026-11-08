/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Combat.LoadingBlocksActorTargetAndDamage
 * - test_25d.Roguelike.Combat.ActiveRoomAllowsOnlyLocalActorCombat
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "MyAbilityTypes.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunDirector.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace DungeonCombatBoundaryTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        ADungeonPlayerRunTestPawn* Source = nullptr;
        AEnemyCharacter* Target = nullptr;
        FDamageEffectParams Damage;
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector(2300, 0, 80), FRotator::ZeroRotator, Params);
            Source->AttachState(World->SpawnActor<AMyPlayerState>());
            Target = World->SpawnActor<AEnemyCharacter>(FVector(2450, 0, 80), FRotator::ZeroRotator, Params);
            Source->Tags = {FName(TEXT("Player"))};
            Target->Tags = {FName(TEXT("Enemy")), FName(TEXT("DungeonEncounter"))};
            Target->MonsterRoomId = TEXT("R01");
            auto* SourceASC = Source->GetAbilitySystemComponent();
            auto* TargetASC = Target->GetAbilitySystemComponent();
            TargetASC->InitAbilityActorInfo(Target, Target);
            Damage.SourceAbilitySystemComponent = SourceASC;
            Damage.TargetAbilitySystemComponent = TargetASC;
            // An ordinary real GE exercises the damage entry without needing balance assets or an ExecCalc.
            Damage.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
            Damage.DamageType = FMyGameplayTags::Get().Damage_Physics;
            Damage.BaseDamage = 10.f;
        }
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        ADungeonGenerationRuntime* PlaceLayout()
        {
            auto* Generation = World->SpawnActor<ADungeonGenerationRuntime>();
            Generation->Manifest.RoomSpacing = 2400.f;
            auto& Entrance = Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef();
            Entrance.RoomId = TEXT("R00");
            Entrance.Cell = FIntPoint(0, 0);
            Entrance.Role = EDungeonRoomRole::Entrance;
            auto& Combat = Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef();
            Combat.RoomId = TEXT("R01");
            Combat.Cell = FIntPoint(1, 0);
            Combat.Role = EDungeonRoomRole::Combat;
            Generation->Phase = EDungeonGenerationPhase::Ready;
            return Generation;
        }
    };
} // namespace DungeonCombatBoundaryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLoadingCombatBoundary,
                                 "test_25d.Roguelike.Combat.LoadingBlocksActorTargetAndDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Combat.LoadingBlocksActorTargetAndDamage 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonLoadingCombatBoundary::RunTest(const FString&)
{
    using namespace DungeonCombatBoundaryTests;
    FFixture F;
    FVector Position;
    TestTrue(TEXT("Ordinary worlds retain Actor targeting"),
             PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    F.Target->SetCombatTarget_Implementation(F.Source);
    TestTrue(TEXT("Ordinary worlds retain enemy target assignment"),
             F.Target->GetCombatTarget_Implementation() == F.Source);
    TestTrue(TEXT("Ordinary worlds retain damage-effect creation"),
             UMyAbilitySystemLibrary::ApplyDamageEffect(F.Damage).IsValid());
    auto* Director = F.World->SpawnActor<ADungeonRunDirector>();
    Director->Phase = EDungeonRunPhase::Loading;
    F.Target->SetCombatTarget_Implementation(F.Source);
    TestNull(TEXT("Loading run clears an enemy combat target"), F.Target->GetCombatTarget_Implementation());
    TestFalse(TEXT("Loading run rejects mixed Actor target resolution"),
              PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    TestFalse(TEXT("Loading run returns no damage context"),
              UMyAbilitySystemLibrary::ApplyDamageEffect(F.Damage).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonActiveCombatBoundary,
                                 "test_25d.Roguelike.Combat.ActiveRoomAllowsOnlyLocalActorCombat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Combat.ActiveRoomAllowsOnlyLocalActorCombat 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonActiveCombatBoundary::RunTest(const FString&)
{
    using namespace DungeonCombatBoundaryTests;
    FFixture F;
    F.PlaceLayout();
    auto* Director = F.World->SpawnActor<ADungeonRunDirector>();
    Director->Phase = EDungeonRunPhase::Encounter;
    Director->ActiveRoom = 1;
    Director->Rooms.SetNum(2);
    Director->Rooms[0].RoomId = TEXT("R00");
    Director->Rooms[0].Role = EDungeonRoomRole::Entrance;
    Director->Rooms[1].RoomId = TEXT("R01");
    Director->Rooms[1].Role = EDungeonRoomRole::Combat;
    Director->Rooms[1].Phase = EDungeonRoomPhase::Active;
    FVector Position;
    F.Target->SetCombatTarget_Implementation(F.Source);
    TestTrue(TEXT("Active room permits enemy target assignment"),
             F.Target->GetCombatTarget_Implementation() == F.Source);
    TestTrue(TEXT("Active room permits mixed Actor target resolution"),
             PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    TestTrue(TEXT("Active room permits damage-effect creation"),
             UMyAbilitySystemLibrary::ApplyDamageEffect(F.Damage).IsValid());
    F.Target->Tags.Remove(TEXT("DungeonEncounter"));
    TestFalse(TEXT("Unmanaged enemy remains rejected"),
              PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    F.Target->bDungeonEncounter = true;
    TestTrue(TEXT("Replicated membership works without server-only Actor tag"),
             PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    const FProperty* Membership = FindFProperty<FProperty>(AEnemyCharacter::StaticClass(),
                                                           GET_MEMBER_NAME_CHECKED(AEnemyCharacter, bDungeonEncounter));
    TestTrue(TEXT("Encounter membership is replicated"), Membership && Membership->HasAnyPropertyFlags(CPF_Net));
    F.Source->SetActorLocation(FVector(0, 0, 80));
    F.Target->SetCombatTarget_Implementation(F.Source);
    TestNull(TEXT("Player leaving room clears enemy target"), F.Target->GetCombatTarget_Implementation());
    TestFalse(TEXT("Cross-room mixed Actor target rejected"),
              PureMassCombat::Resolve(F.World, F.Source, FPureMassCombatTarget::FromActor(F.Target), Position));
    TestFalse(TEXT("Cross-room damage returns no context"),
              UMyAbilitySystemLibrary::ApplyDamageEffect(F.Damage).IsValid());
    return true;
}
#endif
