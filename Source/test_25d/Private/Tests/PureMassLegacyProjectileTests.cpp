/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.LegacyFireBolt.SweptHitOnce
 * - test_25d.PureMass.LegacyFireBolt.WallOcclusion
 * - test_25d.PureMass.LegacyFireBolt.NeverPushesMass
 * - test_25d.PureMass.LegacyFireBolt.AlliesAndScope
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "Actor/MyProjectile.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "GameFramework/PlayerController.h"
#include "PureMass/PureMassLegacyProjectile.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace PureMassLegacyTests
{
    struct FFixture
    {
        FEditorScriptExecutionGuard Guard;
        UWorld* World = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        AEnemyCharacter* Caster = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        UPureMassMonsterType* Type = nullptr;
        FFixture()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("LegacyFireBoltTest")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000;
            Type->Radius = 20;
            Type->HalfHeight = 40;
            Type->Speed = 0;
            Type->XPReward = 0;
            Caster = World->SpawnActor<AEnemyCharacter>();
            Caster->SetActorLocation(FVector(-1000, 0, 0));
            Caster->Tags.Reset();
            Caster->Tags.Add(TEXT("Player"));
            ASC = Caster->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Caster, Caster);
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Caster));
        }
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        int32 Enemy(FVector Location)
        {
            return Mass->SpawnMonster(Type, Location);
        }
        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0;
        }
        AMyProjectile* FireBolt()
        {
            auto* Class = LoadClass<AMyProjectile>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
            const FTransform Transform(FVector::ZeroVector);
            auto* P = World->SpawnActorDeferred<AMyProjectile>(Class, Transform, Caster, Caster,
                                                               ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->DamageEffectParams.SourceAbilitySystemComponent = ASC;
            P->DamageEffectParams.BaseDamage = 30;
            P->DamageEffectParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
            P->FinishSpawning(Transform);
            P->DispatchBeginPlay();
            // The asset enables queries after its 0.1 s muzzle grace period.
            P->Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            return P;
        }
        void Move(AMyProjectile* P, FVector Delta)
        {
            FHitResult Hit;
            P->ProjectileMovement->MoveUpdatedComponent(Delta, FQuat::Identity, true, &Hit);
        }
    };
} // namespace PureMassLegacyTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassLegacyHitTest, "test_25d.PureMass.LegacyFireBolt.SweptHitOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.LegacyFireBolt.SweptHitOnce 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassLegacyHitTest::RunTest(const FString&)
{
    PureMassLegacyTests::FFixture F;
    const int32 First = F.Enemy(FVector(200, 0, 0)), Second = F.Enemy(FVector(400, 0, 0));
    auto* P = F.FireBolt();
    TestNotNull(TEXT("Original FireBolt asset"), P);
    if (!P)
        return false;
    F.Move(P, FVector(1000, 0, 0));
    TestEqual(TEXT("High-speed segment damages first Mass enemy once"), F.Health(First), 970.f);
    TestEqual(TEXT("Second enemy untouched"), F.Health(Second), 1000.f);
    TestTrue(TEXT("Original projectile consumed on Mass hit"), P->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassLegacyWallTest, "test_25d.PureMass.LegacyFireBolt.WallOcclusion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.LegacyFireBolt.WallOcclusion 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassLegacyWallTest::RunTest(const FString&)
{
    PureMassLegacyTests::FFixture F;
    const int32 Behind = F.Enemy(FVector(400, 0, 0));
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(10, 100, 100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(200, 0, 0));
    auto* P = F.FireBolt();
    F.Move(P, FVector(1000, 0, 0));
    TestEqual(TEXT("Mass enemy behind world wall untouched"), F.Health(Behind), 1000.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassLegacyMovementTest, "test_25d.PureMass.LegacyFireBolt.NeverPushesMass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.LegacyFireBolt.NeverPushesMass 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassLegacyMovementTest::RunTest(const FString&)
{
    PureMassLegacyTests::FFixture F;
    F.Type->Speed = 100;
    F.Caster->SetActorLocation(FVector(1000, 0, 0));
    auto* PC = F.World->SpawnActor<APlayerController>();
    F.World->AddController(PC);
    PC->SetPawn(F.Caster);
    const int32 Id = F.Enemy(FVector::ZeroVector);
    auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(F.Mass->GetEntity(Id));
    Fragment.Path = {FVector(1000, 0, 0)};
    Fragment.PathGoal = F.Caster->GetActorLocation();
    Fragment.NextPathTime = MAX_flt;
    auto* P = F.FireBolt();
    // Even an unadapted/hostile projectile configured to block Pawn cannot shove Mass.
    P->Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    P->SetActorLocation(FVector(40, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
    F.Mass->BeginUpdate(.1f);
    F.Mass->UpdateMonster(Fragment);
    F.Mass->FinishUpdate();
    TestEqual(TEXT("Normal 10-unit path step, no projectile depenetration"), Fragment.Location.X, 10.0);
    TestEqual(TEXT("Teleporting projectile never deals a swept hit"), F.Health(Id), 1000.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassLegacyScopeTest, "test_25d.PureMass.LegacyFireBolt.AlliesAndScope",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.LegacyFireBolt.AlliesAndScope 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassLegacyScopeTest::RunTest(const FString&)
{
    PureMassLegacyTests::FFixture F;
    const int32 Id = F.Enemy(FVector(200, 0, 0));
    F.Caster->Tags.Reset();
    F.Caster->Tags.Add(TEXT("Enemy"));
    auto* P = F.FireBolt();
    TestNotNull(TEXT("Original FireBolt automatically adapted"), P->FindComponentByClass<UPureMassLegacyProjectile>());
    F.Move(P, FVector(1000, 0, 0));
    TestEqual(TEXT("Enemy-cast FireBolt cannot hurt friendly Mass enemies"), F.Health(Id), 1000.f);
    TestFalse(TEXT("Friendly pass-through does not consume projectile"), P->IsActorBeingDestroyed());
    auto* Other = F.World->SpawnActor<AMyProjectile>();
    TestNull(TEXT("Unlisted projectile lifecycle untouched"), Other->FindComponentByClass<UPureMassLegacyProjectile>());
    return true;
}
#endif
