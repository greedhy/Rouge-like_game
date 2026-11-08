/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.Combat.NoDoubleHit
 * - test_25d.PureMass.Combat.WallBeforeEntity
 * - test_25d.PureMass.Combat.InvalidTargetRejected
 * - test_25d.PureMass.Combat.BasicKindsAndComposition
 * - test_25d.PureMass.Combat.OrbitImmediateLaunchStartsAtCaster
 * - test_25d.PureMass.Combat.NativeManaCost
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Terrain/TerrainSurface.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "MyGameplayTags.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace PureMassCombatTests
{
    struct FFixture
    {
        UWorld* World;
        AActor* Caster;
        UPureMassSubsystem* Mass;
        UPureMassMonsterType* Type;
        FFixture()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PureMassCombat")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Caster = World->SpawnActor<AActor>();
            auto* Root = NewObject<USceneComponent>(Caster);
            Caster->SetRootComponent(Root);
            Root->RegisterComponent();
            Caster->SetActorLocation(FVector(-500, 0, 0));
            Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000.f;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
        }
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        int32 Enemy(FVector Position)
        {
            return Mass->SpawnMonster(Type, Position);
        }
        float Health(int32 Id)
        {
            FPureMassView V;
            return Mass->Resolve(Id, V) ? V.Health : 0.f;
        }
        UAbilityPrototype* Config(EAbilityPrototypeKind Kind)
        {
            auto* P = NewObject<UAbilityPrototype>(World);
            P->Kind = Kind;
            P->DamageType = FMyGameplayTags::Get().Damage_Magic;
            P->Damage = 25.f;
            P->DamagePerLevel = 0.f;
            P->Cooldown = 0.f;
            P->Period = .1f;
            P->Duration = 3.f;
            P->Radius = 200.f;
            P->Range = 1200.f;
            P->Speed = 1000.f;
            P->Count = 3;
            P->DamageFalloff = 1.f;
            return P;
        }
        UPrototypeCastContext* Context()
        {
            auto* C = NewObject<UPrototypeCastContext>(Caster);
            C->Caster = Caster;
            C->CastId = FGuid::NewGuid();
            return C;
        }
        APureMassRuntime* Start(UAbilityPrototype* P, int32 Id = INDEX_NONE, UPrototypeCastContext* C = nullptr)
        {
            auto* R = APureMassRuntime::Spawn(C ? C : Context(), P, FVector::ZeroVector, FVector::ForwardVector, Id);
            if (R && !R->HasActorBegunPlay())
                R->DispatchBeginPlay();
            return R;
        }
        void Wall(FVector Center, FVector Extent)
        {
            auto* A = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(A);
            A->SetRootComponent(Box);
            Box->SetBoxExtent(Extent);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            A->SetActorLocation(Center);
        }
        void BeginChildren()
        {
            // Spawn during another actor's BeginPlay can add descendants; bounded context caps this loop.
            for (int32 Pass = 0; Pass < 4; ++Pass)
            {
                TArray<APureMassRuntime*> Pending;
                for (TActorIterator<APureMassRuntime> It(World); It; ++It)
                    if (!It->HasActorBegunPlay())
                        Pending.Add(*It);
                for (auto* R : Pending)
                    R->DispatchBeginPlay();
            }
        }
    };
} // namespace PureMassCombatTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassNoDoubleHit, "test_25d.PureMass.Combat.NoDoubleHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.NoDoubleHit 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassNoDoubleHit::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    FFixture F;
    const int32 First = F.Enemy(FVector(100, 0, 0)), Second = F.Enemy(FVector(200, 0, 0));
    auto* R = F.Start(F.Config(EAbilityPrototypeKind::Piercing));
    if (!TestNotNull(TEXT("Piercing runtime"), R))
        return false;
    R->Tick(.25f);
    R->Tick(.25f);
    TestEqual(TEXT("First entity damaged once across repeated sweeps"), F.Health(First), 975.f);
    TestEqual(TEXT("Second entity damaged once in same sweep"), F.Health(Second), 975.f);
    auto* Wave = F.Start(F.Config(EAbilityPrototypeKind::Wave));
    Wave->Tick(.1f);
    Wave->Tick(.1f);
    Wave->Tick(.1f);
    TestEqual(TEXT("Wave hits first entity only once"), F.Health(First), 950.f);
    TestEqual(TEXT("Wave hits boundary entity once"), F.Health(Second), 950.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassWallFirst, "test_25d.PureMass.Combat.WallBeforeEntity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.WallBeforeEntity 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassWallFirst::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    TestFalse(TEXT("Wall wins an exact tie"), APureMassRuntime::EntityBeforeWall(.5f, true, .5f));
    TestFalse(TEXT("Wall before entity"), APureMassRuntime::EntityBeforeWall(.8f, true, .3f));
    TestTrue(TEXT("Entity before wall"), APureMassRuntime::EntityBeforeWall(.2f, true, .8f));
    FFixture F;
    const int32 Id = F.Enemy(FVector(150, 0, 0));
    F.Wall(FVector(70, 0, 0), FVector(10, 100, 100));
    auto* R = F.Start(F.Config(EAbilityPrototypeKind::Piercing));
    if (!TestNotNull(TEXT("Projectile"), R))
        return false;
    R->Tick(.25f);
    TestEqual(TEXT("Occluded entity takes no damage"), F.Health(Id), 1000.f);
    TestTrue(TEXT("Projectile expires at wall"), R->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassTargetValidation, "test_25d.PureMass.Combat.InvalidTargetRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.InvalidTargetRejected 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassTargetValidation::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    FFixture F;
    auto* P = F.Config(EAbilityPrototypeKind::Homing);
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    FVector Out;
    TestTrue(TEXT("Existing visible in-range entity accepted"),
             UPureMassAbility::ValidateTarget(F.Caster, P, Id, FVector(100, 0, 0), Out));
    TestFalse(TEXT("Unknown stable ID rejected"),
              UPureMassAbility::ValidateTarget(F.Caster, P, 999999, FVector(100, 0, 0), Out));
    TestFalse(TEXT("Missing homing target rejected"),
              UPureMassAbility::ValidateTarget(F.Caster, P, INDEX_NONE, FVector(100, 0, 0), Out));
    TestFalse(
        TEXT("Non-finite point rejected"),
        UPureMassAbility::ValidateTarget(F.Caster, P, Id, FVector(std::numeric_limits<float>::quiet_NaN(), 0, 0), Out));
    TestFalse(TEXT("Spoofed point for valid ID rejected"),
              UPureMassAbility::ValidateTarget(F.Caster, P, Id, FVector(900, 0, 0), Out));
    P->Range = 100.f;
    TestFalse(TEXT("Out of range rejected"),
              UPureMassAbility::ValidateTarget(F.Caster, P, Id, FVector(100, 0, 0), Out));
    P->Range = 1200.f;
    F.Wall(FVector(-100, 0, 0), FVector(10, 100, 100));
    TestFalse(TEXT("Server wall rejects entity request"),
              UPureMassAbility::ValidateTarget(F.Caster, P, Id, FVector(100, 0, 0), Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassBasicKinds, "test_25d.PureMass.Combat.BasicKindsAndComposition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.BasicKindsAndComposition 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassBasicKinds::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    using K = EAbilityPrototypeKind;
    for (uint8 Index = 0; Index <= static_cast<uint8>(K::Line); ++Index)
    {
        FFixture F;
        F.Wall(FVector(0, 0, -100), FVector(2000, 2000, 10));
        auto* P = F.Config(static_cast<K>(Index));
        if (P->Kind == K::Surface)
            P->SurfaceClass = ATerrainSurface::StaticClass();
        if (P->Kind == K::Orbit)
        {
            P->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(F.World);
            P->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
        }
        if (P->Kind == K::Emitter)
            P->OnPeriod = F.Config(K::Projectile);
        if (P->Kind == K::Delay)
            P->OnExpire = F.Config(K::Burst);
        TestNotNull(FString::Printf(TEXT("All 18 kind dispatch: %d"), Index), F.Start(P));
    }
    for (K Kind : {K::Projectile, K::Burst, K::Field, K::Trap, K::Homing, K::Piercing, K::Chain, K::Cone, K::Wave,
                   K::Returning, K::Ring, K::Line})
    {
        FFixture F;
        const int32 Id = F.Enemy(FVector(100, 0, 0));
        auto* P = F.Config(Kind);
        P->InnerRadius = 50.f;
        P->ArmDelay = .1f;
        auto* R = F.Start(P, Id);
        if (!TestNotNull(FString::Printf(TEXT("Kind %d creates runtime"), int32(Kind)), R))
            continue;
        R->Tick(.15f);
        TestTrue(FString::Printf(TEXT("Kind %d damages entity"), int32(Kind)), F.Health(Id) < 1000.f);
    }
    {
        FFixture F;
        const int32 Id = F.Enemy(FVector(100, 0, 0));
        auto* P = F.Config(K::Projectile);
        P->OnHit = F.Config(K::Burst);
        auto* C = F.Context();
        auto* R = F.Start(P, Id, C);
        R->Tick(.15f);
        F.BeginChildren();
        TestEqual(TEXT("OnHit child damages entity without Actor target"), F.Health(Id), 950.f);
        TestEqual(TEXT("Root and child share spawn budget"), C->Spawned, 2);
    }
    {
        FFixture F;
        const int32 Id = F.Enemy(FVector(100, 0, 0));
        auto* P = F.Config(K::Delay);
        P->ArmDelay = .1f;
        P->OnExpire = F.Config(K::Burst);
        auto* R = F.Start(P);
        R->Tick(.15f);
        F.BeginChildren();
        TestEqual(TEXT("Delay OnExpire child fires once"), F.Health(Id), 975.f);
        R->Tick(.2f);
        F.BeginChildren();
        TestEqual(TEXT("Finished runtime cannot repeat expiry"), F.Health(Id), 975.f);
    }
    {
        FFixture F;
        const int32 Id = F.Enemy(FVector(100, 0, 0));
        auto* P = F.Config(K::Emitter);
        P->Count = 1;
        P->OnPeriod = F.Config(K::Burst);
        auto* R = F.Start(P);
        R->Tick(.15f);
        F.BeginChildren();
        TestEqual(TEXT("Emitter OnPeriod child"), F.Health(Id), 975.f);
        TestTrue(TEXT("Emitter successful shot cap"), R->IsFinished());
    }
    {
        FFixture F;
        auto* P = F.Config(K::Burst);
        P->OnHit = P;
        F.Enemy(FVector(100, 0, 0));
        auto* C = F.Context();
        F.Start(P, INDEX_NONE, C);
        F.BeginChildren();
        TestEqual(TEXT("Recursive graph stops at depth three"), C->Spawned, 4);
        TestFalse(TEXT("Depth four rejected"), C->ReserveSpawn(4));
        while (C->ReserveSpawn(0))
        {
        }
        TestEqual(TEXT("Shared node count bounded at sixty four"), C->Spawned, 64);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassOrbitSpawnOrigin,
                                 "test_25d.PureMass.Combat.OrbitImmediateLaunchStartsAtCaster",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.OrbitImmediateLaunchStartsAtCaster 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassOrbitSpawnOrigin::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    FFixture F;
    F.Caster->SetActorLocation(FVector(1000, 0, 0));
    auto* ASC = NewObject<UAbilitySystemComponent>(F.Caster);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(F.Caster, F.Caster);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(F.Caster));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
    const int32 Bystander = F.Enemy(FVector(20, 0, 0));
    const int32 Target = F.Enemy(FVector(1500, 0, 0));
    auto* P = F.Config(EAbilityPrototypeKind::Orbit);
    P->ManaPerSecond = 0.f;
    P->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(F.World);
    P->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
    P->OrbitSettings->SpawnInterval = .1f;
    P->OrbitSettings->LaunchInterval = .05f;
    P->OrbitSettings->OrbitRadius = 65.f;
    P->OrbitSettings->OrbitHeight = 0.f;
    P->OrbitSettings->OrbitDegreesPerSecond = 0.f;
    P->OrbitSettings->ProjectileSpeed = 1000.f;
    auto* R = F.Start(P, Target);
    if (!TestNotNull(TEXT("Orbit runtime"), R))
        return false;
    // Both timers elapse in this frame, so the newly created bolt immediately launches.
    R->Tick(.11f);
    TestEqual(TEXT("No phantom hit near world origin"), F.Health(Bystander), 1000.f);
    TestEqual(TEXT("New bolt remains in flight"), R->OrbitBolts.Num(), 1);
    if (R->OrbitBolts.Num() == 1)
    {
        TestTrue(TEXT("Spawn frame launches bolt"), R->OrbitBolts[0].bLaunched);
        TestTrue(TEXT("Flight starts at caster orbit position"),
                 R->OrbitBolts[0].Position.Equals(FVector(1175, 0, 0), .01f));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassNativeManaCost, "test_25d.PureMass.Combat.NativeManaCost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Combat.NativeManaCost 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassNativeManaCost::RunTest(const FString&)
{
    using namespace PureMassCombatTests;
    FFixture F;
    const auto* Definition = GetDefault<UPureMassManaEffect>();
    TestTrue(TEXT("Mana definition is the native CDO"), Definition->HasAnyFlags(RF_ClassDefaultObject));
    TestTrue(TEXT("Mana definition has a stable script path"),
             Definition->GetPathName().StartsWith(TEXT("/Script/test_25d.")));
    TestTrue(TEXT("Mana effect is instant"), Definition->DurationPolicy == EGameplayEffectDurationType::Instant);
    TestEqual(TEXT("One mana modifier"), Definition->Modifiers.Num(), 1);
    if (Definition->Modifiers.Num() == 1)
    {
        TestTrue(TEXT("Modifier targets player mana"),
                 Definition->Modifiers[0].Attribute == UMyAttributeSet::GetManaAttribute());
        TestTrue(TEXT("Modifier applies an additive per-spec amount"),
                 Definition->Modifiers[0].ModifierOp == EGameplayModOp::Additive);
    }
    const auto AddPlayer = [&F](float Mana, ACharacter*& Character)
    {
        Character = F.World->SpawnActor<ACharacter>();
        auto* ASC = NewObject<UAbilitySystemComponent>(Character);
        ASC->RegisterComponent();
        ASC->InitAbilityActorInfo(Character, Character);
        ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Character));
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), Mana);
        return ASC;
    };
    ACharacter* First = nullptr;
    ACharacter* Second = nullptr;
    auto* FirstASC = AddPlayer(100.f, First);
    auto* SecondASC = AddPlayer(50.f, Second);
    TestTrue(TEXT("First independent cost applies"), UPureMassAbility::SpendMana(First, 17.5f));
    TestEqual(TEXT("First mana charged exactly"), FirstASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              82.5f);
    TestTrue(TEXT("Second independent cost applies"), UPureMassAbility::SpendMana(Second, 13.f));
    TestEqual(TEXT("Second application uses its own amount"),
              SecondASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 37.f);
    TestEqual(TEXT("Second application leaves first caster unchanged"),
              FirstASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 82.5f);
    TestTrue(TEXT("Fractional continuous cost applies"), UPureMassAbility::SpendMana(First, 2.5f));
    TestEqual(TEXT("Continuous and activation charges compose"),
              FirstASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 80.f);
    TestTrue(TEXT("Zero cost succeeds"), UPureMassAbility::SpendMana(First, 0.f));
    TestFalse(TEXT("Insufficient mana rejects charge"), UPureMassAbility::SpendMana(First, 81.f));
    TestFalse(TEXT("Negative cost rejects charge"), UPureMassAbility::SpendMana(First, -1.f));
    TestFalse(TEXT("Non-finite cost rejects charge"),
              UPureMassAbility::SpendMana(First, std::numeric_limits<float>::infinity()));
    TestEqual(TEXT("Rejected charges do not change mana"),
              FirstASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 80.f);
    return true;
}
#endif
