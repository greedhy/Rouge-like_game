/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.Mixed.BurstAndTeams
 * - test_25d.PureMass.Mixed.PiercingOrderAndWalls
 * - test_25d.PureMass.Mixed.AlternatingChain
 * - test_25d.PureMass.Mixed.HomingBeamAndLifetime
 * - test_25d.PureMass.Mixed.TargetValidation
 * - test_25d.PureMass.Mixed.ShapesAndTimedKinds
 * - test_25d.PureMass.Mixed.ChildrenAndBudgets
 * - test_25d.PureMass.Mixed.OrbitActorTarget
 * - test_25d.PureMass.Mixed.GroundValidationIgnoresCombatants
 * - test_25d.PureMass.Mixed.DeadCasterCannotSpawnBurst
 * - test_25d.PureMass.Mixed.SweepEndpointAndWallTie
 * - test_25d.Lifecycle.RadialSubscriptionScope
 * - test_25d.PureMass.Movement.WorldConstraint
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/CurveTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/MyGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace PureMassMixedTests
{
    using K = EAbilityPrototypeKind;
    using FTarget = FPureMassCombatTarget;

    // Real GAS damage needs the project's calculation curves and a source ASC. Keep
    // characters out of BeginPlay so the fixture does not grant production AI abilities.
    struct FFixture
    {
        UWorld* World = nullptr;
        AEnemyCharacter* Caster = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        UPureMassMonsterType* Type = nullptr;
        TSubclassOf<UGameplayEffect> DamageEffect;
        bool bDamageReady = false;
        FEditorScriptExecutionGuard Guard;

        FFixture()
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PureMassMixed")));
            if (!World)
                return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
            World->SetGameMode(URL);
            auto* Mode = Cast<AMyGameModeBase>(World->GetAuthGameMode());
            if (Mode)
            {
                Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
                    nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
                auto* Curves = Mode->CharacterClassInfo ? Mode->CharacterClassInfo->DamageCalConst.Get() : nullptr;
                bDamageReady = Curves && Curves->FindCurve(TEXT("ArmorPene"), TEXT("Mixed fixture"), false) &&
                               Curves->FindCurve(TEXT("EffectiveArmor"), TEXT("Mixed fixture"), false) &&
                               Curves->FindCurve(TEXT("CriticalHitResistance"), TEXT("Mixed fixture"), false);
            }
            DamageEffect = LoadClass<UGameplayEffect>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C"));
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            if (Mass)
                Mass->Enable();
            Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000.f;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            Caster = Character(FVector(-500, 0, 0), TEXT("Player"));
            ASC = Caster ? Cast<UMyAbilitySystemComponent>(Caster->GetAbilitySystemComponent()) : nullptr;
            if (ASC)
            {
                auto* Controller = World->SpawnActor<APlayerController>();
                if (Controller)
                {
                    Controller->SetPawn(Caster);
                    ASC->AbilityActorInfo->PlayerController = Controller;
                }
            }
        }
        ~FFixture()
        {
            if (ASC)
                ASC->CancelAllAbilities();
            if (World)
            {
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }
        bool Ready(FAutomationTestBase& Test) const
        {
            return Test.TestNotNull(TEXT("Mixed test world"), World) &&
                   Test.TestNotNull(TEXT("Mass subsystem"), Mass) && Test.TestNotNull(TEXT("Source ASC"), ASC) &&
                   Test.TestNotNull(TEXT("Real GE_Damage asset"), DamageEffect.Get()) &&
                   Test.TestTrue(TEXT("Damage calculation curves loaded"), bDamageReady);
        }
        AEnemyCharacter* Character(FVector Position, FName Team = TEXT("Enemy"))
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Actor = World->SpawnActor<AEnemyCharacter>(Position, FRotator::ZeroRotator, Params);
            if (!Actor)
                return nullptr;
            Actor->Tags.Reset();
            Actor->Tags.Add(Team);
            auto* Capsule = Actor->GetCapsuleComponent();
            Capsule->SetCapsuleSize(20.f, 40.f);
            Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Capsule->SetCollisionObjectType(ECC_Pawn);
            Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            auto* Component = Actor->GetAbilitySystemComponent();
            if (!Component)
                return nullptr;
            Component->InitAbilityActorInfo(Actor, Actor);
            Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            return Actor;
        }
        int32 Enemy(FVector Position)
        {
            return Mass->SpawnMonster(Type, Position);
        }
        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }
        float Health(AEnemyCharacter* Actor) const
        {
            return Actor && Actor->GetAbilitySystemComponent()
                       ? Actor->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute())
                       : -1.f;
        }
        UAbilityPrototype* Config(K Kind)
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
            P->Count = 4;
            P->InnerRadius = 50.f;
            P->ArmDelay = .1f;
            P->DamageFalloff = 1.f;
            return P;
        }
        UPrototypeCastContext* Context()
        {
            auto* C = NewObject<UPrototypeCastContext>(Caster);
            C->Caster = Caster;
            C->CastId = FGuid::NewGuid();
            C->DamageParams.SourceAbilitySystemComponent = ASC;
            C->DamageParams.DamageGameplayEffectClass = DamageEffect;
            C->DamageParams.WorldContextObject = Caster;
            C->DamageParams.DebuffChance = 0.f;
            return C;
        }
        APureMassRuntime* Start(UAbilityPrototype* P, FTarget Target = {}, UPrototypeCastContext* C = nullptr)
        {
            auto* R =
                APureMassRuntime::Spawn(C ? C : Context(), P, FVector::ZeroVector, FVector::ForwardVector, Target);
            if (R && !R->HasActorBegunPlay())
                R->DispatchBeginPlay();
            return R;
        }
        void BeginChildren()
        {
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
        void Wall(FVector Center)
        {
            auto* Actor = World->SpawnActor<AActor>();
            if (!Actor)
                return;
            auto* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(10, 300, 300));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Actor->SetActorLocation(Center);
        }
    };
} // namespace PureMassMixedTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedBurst, "test_25d.PureMass.Mixed.BurstAndTeams",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.BurstAndTeams 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedBurst::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    if (!F.Ready(*this))
        return false;
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* Elite = F.Character(FVector(150, 0, 0));
    auto* Ally = F.Character(FVector(50, 0, 0), TEXT("Player"));
    auto* Outside = F.Character(FVector(250, 0, 0));
    if (!TestNotNull(TEXT("Elite"), Elite) || !TestNotNull(TEXT("Ally"), Ally) ||
        !TestNotNull(TEXT("Outside enemy"), Outside))
        return false;
    auto* Context = F.Context();
    if (!TestNotNull(TEXT("Single mixed burst"), F.Start(F.Config(K::Burst), {}, Context)))
        return false;
    TestEqual(TEXT("Mass enemy receives one damage application"), F.Health(Id), 975.f);
    TestEqual(TEXT("Actor enemy receives one real GAS damage application"), F.Health(Elite), 975.f);
    TestEqual(TEXT("Ally neither takes damage nor shields enemies"), F.Health(Ally), 1000.f);
    TestEqual(TEXT("Actor outside radius excluded"), F.Health(Outside), 1000.f);
    TestEqual(TEXT("Both representations share one runtime"), Context->Spawned, 1);
    const auto Enemies = PureMassCombat::Gather(F.World, F.Caster, FVector::ZeroVector, 200.f);
    TestEqual(TEXT("Unified query contains exactly two hostiles"), Enemies.Num(), 2);
    TestTrue(TEXT("Query contains Mass identity"), Enemies.Contains(FTarget(Id)));
    TestTrue(TEXT("Query contains Actor identity"), Enemies.Contains(FTarget::FromActor(Elite)));
    FVector Location;
    TestFalse(TEXT("Enemy caster cannot target Mass allies"), PureMassCombat::Resolve(F.World, Elite, Id, Location));
    TestFalse(TEXT("Self cannot be a target"),
              PureMassCombat::Resolve(F.World, F.Caster, FTarget::FromActor(F.Caster), Location));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedPiercing, "test_25d.PureMass.Mixed.PiercingOrderAndWalls",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.PiercingOrderAndWalls 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedPiercing::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    // Swap the first representation to catch implementations that always prioritize one backend.
    for (bool bActorFirst : {false, true})
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        auto* Actor = F.Character(FVector(bActorFirst ? 100 : 200, 0, 0));
        if (!TestNotNull(TEXT("Piercing Actor"), Actor))
            return false;
        const int32 Id = F.Enemy(FVector(bActorFirst ? 200 : 100, 0, 0));
        auto* Ally = F.Character(FVector(50, 0, 0), TEXT("Player"));
        if (!TestNotNull(TEXT("Blocking ally"), Ally))
            return false;
        Actor->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        auto* P = F.Config(K::Piercing);
        P->DamageFalloff = .5f;
        auto* R = F.Start(P);
        if (!TestNotNull(TEXT("Piercing runtime"), R))
            return false;
        R->Tick(.25f);
        R->Tick(.25f);
        TestEqual(TEXT("Actor damage follows spatial order and is not repeated"), F.Health(Actor),
                  bActorFirst ? 975.f : 987.5f);
        TestEqual(TEXT("Mass damage follows spatial order and is not repeated"), F.Health(Id),
                  bActorFirst ? 987.5f : 975.f);
        TestEqual(TEXT("Friendly collision is transparent"), F.Health(Ally), 1000.f);
    }
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        const int32 Near = F.Enemy(FVector(100, 0, 0));
        const int32 Far = F.Enemy(FVector(400, 0, 0));
        auto* NearActor = F.Character(FVector(180, 0, 0));
        auto* FarActor = F.Character(FVector(350, 0, 0));
        if (!TestNotNull(TEXT("Near Actor"), NearActor) || !TestNotNull(TEXT("Far Actor"), FarActor))
            return false;
        F.Wall(FVector(260, 0, 0));
        auto* R = F.Start(F.Config(K::Piercing));
        if (!TestNotNull(TEXT("Wall test runtime"), R))
            return false;
        R->Tick(.5f);
        TestEqual(TEXT("Mass before wall is hit"), F.Health(Near), 975.f);
        TestEqual(TEXT("Actor before wall is hit"), F.Health(NearActor), 975.f);
        TestEqual(TEXT("Mass behind wall protected"), F.Health(Far), 1000.f);
        TestEqual(TEXT("Actor behind wall protected"), F.Health(FarActor), 1000.f);
        TestTrue(TEXT("Wall terminates mixed projectile"), R->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedChain, "test_25d.PureMass.Mixed.AlternatingChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.AlternatingChain 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedChain::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    if (!F.Ready(*this))
        return false;
    const int32 First = F.Enemy(FVector(100, 0, 0));
    auto* Middle = F.Character(FVector(250, 0, 0));
    const int32 Last = F.Enemy(FVector(400, 0, 0));
    if (!TestNotNull(TEXT("Chain Actor"), Middle))
        return false;
    auto* P = F.Config(K::Chain);
    P->Radius = 170.f;
    P->Count = 3;
    P->DamageFalloff = .5f;
    auto* R = F.Start(P, First);
    if (!TestNotNull(TEXT("Mixed chain"), R))
        return false;
    R->Tick(.01f);
    R->Tick(.11f);
    R->Tick(.11f);
    R->Tick(.2f);
    TestEqual(TEXT("First Mass receives full damage once"), F.Health(First), 975.f);
    TestEqual(TEXT("Chain crosses to Actor with falloff"), F.Health(Middle), 987.5f);
    TestEqual(TEXT("Chain crosses back to Mass with next falloff"), F.Health(Last), 993.75f);
    TestTrue(TEXT("Unique target count finishes chain"), R->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedLifetime, "test_25d.PureMass.Mixed.HomingBeamAndLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.HomingBeamAndLifetime 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedLifetime::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    for (K Kind : {K::Homing, K::Beam})
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        auto* Enemy = F.Character(FVector(100, 0, 0));
        if (!TestNotNull(TEXT("Targeted Actor"), Enemy))
            return false;
        auto* R = F.Start(F.Config(Kind), FTarget::FromActor(Enemy));
        if (!TestNotNull(TEXT("Actor-targeted runtime"), R))
            return false;
        if (Kind == K::Beam)
        {
            TestTrue(TEXT("Beam starts at caster before its first tick"),
                     R->GetActorLocation().Equals(F.Caster->GetActorLocation() + FVector(0, 0, 30), .01f));
            TestTrue(TEXT("Beam endpoint resolves actual target before its first tick"),
                     R->BeamEnd.Equals(Enemy->GetActorLocation(), .01f));
        }
        R->Tick(.15f);
        TestEqual(TEXT("Actor target takes one GAS hit"), F.Health(Enemy), 975.f);
        auto* Lost = F.Character(FVector(600, 0, 0));
        if (!TestNotNull(TEXT("Lifetime Actor"), Lost))
            return false;
        const auto LostTarget = FTarget::FromActor(Lost);
        auto* Waiting = F.Start(F.Config(Kind), LostTarget);
        if (!TestNotNull(TEXT("Waiting runtime"), Waiting))
            return false;
        Lost->Destroy();
        Waiting->Tick(.15f);
        TestTrue(TEXT("Destroyed Actor ends targeted runtime safely"), Waiting->IsFinished());
        FVector Location;
        TestFalse(TEXT("Weak identity cannot resolve destroyed Actor"),
                  PureMassCombat::Resolve(F.World, F.Caster, LostTarget, Location));
    }
    FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* DeadActor = F.Character(FVector(100, 0, 0));
    if (!TestNotNull(TEXT("Dead Actor fixture"), DeadActor))
        return false;
    auto* DeadProperty = FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"));
    if (!TestNotNull(TEXT("Combat death state property"), DeadProperty))
        return false;
    // Exercise the actual CombatInterface death predicate without invoking mesh physics/loot.
    DeadProperty->SetPropertyValue_InContainer(DeadActor, true);
    FVector Location;
    TestFalse(TEXT("Dead but allocated Actor is rejected"),
              PureMassCombat::Resolve(F.World, F.Caster, FTarget::FromActor(DeadActor), Location));
    const int32 DeadId = F.Enemy(FVector(150, 0, 0));
    FPureMassDamage Damage;
    Damage.Source = F.Caster;
    Damage.Amount = 2000.f;
    Damage.DamageType = FMyGameplayTags::Get().Damage_Magic;
    F.Mass->ApplyDamage(DeadId, Damage);
    TestFalse(TEXT("Dead Mass identity is rejected"), PureMassCombat::Resolve(F.World, F.Caster, DeadId, Location));
    TestTrue(TEXT("Dead representations excluded from gather"),
             PureMassCombat::Gather(F.World, F.Caster, FVector::ZeroVector, 200.f).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedValidation, "test_25d.PureMass.Mixed.TargetValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.TargetValidation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedValidation::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    FFixture Other;
    if (!F.Ready(*this) || !Other.Ready(*this))
        return false;
    auto* Actor = F.Character(FVector(100, 0, 0));
    auto* Foreign = Other.Character(FVector(100, 0, 0));
    if (!TestNotNull(TEXT("Validation Actor"), Actor) || !TestNotNull(TEXT("Foreign world Actor"), Foreign))
        return false;
    const int32 Id = F.Enemy(FVector(100, 30, 0));
    auto* P = F.Config(K::Homing);
    FVector Location;
    TestFalse(TEXT("Empty identity rejected"), PureMassCombat::Resolve(F.World, F.Caster, {}, Location));
    TestFalse(TEXT("Unknown Mass identity rejected"), PureMassCombat::Resolve(F.World, F.Caster, 999999, Location));
    TestFalse(TEXT("Foreign world Actor rejected"),
              PureMassCombat::Resolve(F.World, F.Caster, FTarget::FromActor(Foreign), Location));
    TestFalse(TEXT("Foreign world caster rejected"), PureMassCombat::Resolve(F.World, Other.Caster, Id, Location));
    auto Ambiguous = FTarget::FromActor(Actor);
    Ambiguous.EntityId = Id;
    TestFalse(TEXT("Conflicting Actor and Mass identities rejected"),
              PureMassCombat::Resolve(F.World, F.Caster, Ambiguous, Location));
    TestTrue(TEXT("Nonfinite query rejected"),
             PureMassCombat::Gather(F.World, F.Caster, FVector::ZeroVector, std::numeric_limits<float>::infinity())
                 .IsEmpty());
    for (const auto Target : {FTarget(Id), FTarget::FromActor(Actor)})
    {
        FVector Point;
        if (!TestTrue(TEXT("Live target resolves"), PureMassCombat::Resolve(F.World, F.Caster, Target, Point)))
            continue;
        TestTrue(TEXT("Both target representations validate"),
                 UPureMassAbility::ValidateCombatTarget(F.Caster, P, Target, Point, Location));
        TestFalse(TEXT("Forged target point rejected"),
                  UPureMassAbility::ValidateCombatTarget(F.Caster, P, Target, Point + FVector(800, 0, 0), Location));
        TestFalse(TEXT("Nonfinite target point rejected"),
                  UPureMassAbility::ValidateCombatTarget(
                      F.Caster, P, Target, FVector(std::numeric_limits<float>::quiet_NaN(), 0, 0), Location));
        P->Range = 100.f;
        TestFalse(TEXT("Both target representations enforce range"),
                  UPureMassAbility::ValidateCombatTarget(F.Caster, P, Target, Point, Location));
        P->Range = 1200.f;
    }
    F.Wall(FVector(-100, 0, 0));
    TestFalse(TEXT("Wall blocks Actor request"),
              UPureMassAbility::ValidateCombatTarget(F.Caster, P, FTarget::FromActor(Actor), Actor->GetActorLocation(),
                                                     Location));
    TestFalse(TEXT("Wall blocks Mass request"),
              UPureMassAbility::ValidateCombatTarget(F.Caster, P, Id, FVector(100, 30, 0), Location));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedShapes, "test_25d.PureMass.Mixed.ShapesAndTimedKinds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.ShapesAndTimedKinds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedShapes::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    for (K Kind : {K::Cone, K::Line, K::Ring, K::Wave, K::Returning, K::Field, K::Trap})
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        const int32 Id = F.Enemy(FVector(100, 0, 0));
        auto* Actor = F.Character(FVector(140, 0, 0));
        if (!TestNotNull(TEXT("Shape Actor"), Actor))
            return false;
        auto* R = F.Start(F.Config(Kind));
        if (!TestNotNull(FString::Printf(TEXT("Kind %d runtime"), int32(Kind)), R))
            continue;
        R->Tick(.15f);
        TestEqual(FString::Printf(TEXT("Kind %d hits Mass once"), int32(Kind)), F.Health(Id), 975.f);
        TestEqual(FString::Printf(TEXT("Kind %d hits Actor once"), int32(Kind)), F.Health(Actor), 975.f);
        if (Kind == K::Wave)
        {
            R->Tick(.1f);
            TestEqual(TEXT("Wave never re-hits Mass on expansion"), F.Health(Id), 975.f);
            TestEqual(TEXT("Wave never re-hits Actor on expansion"), F.Health(Actor), 975.f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedChildren, "test_25d.PureMass.Mixed.ChildrenAndBudgets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.ChildrenAndBudgets 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedChildren::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    for (bool bActorTarget : {false, true})
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        auto* Actor = F.Character(FVector(100, 0, 0));
        if (!TestNotNull(TEXT("Composition Actor"), Actor))
            return false;
        const int32 Id = F.Enemy(FVector(100, 100, 0));
        const FTarget Target = bActorTarget ? FTarget::FromActor(Actor) : FTarget(Id);
        auto* P = F.Config(K::Chain);
        P->Count = 1;
        P->OnHit = F.Config(K::Beam);
        auto* Context = F.Context();
        auto* Root = F.Start(P, Target, Context);
        if (!TestNotNull(TEXT("Parent with targeted child"), Root))
            return false;
        Root->Tick(.01f);
        F.BeginChildren();
        TestEqual(TEXT("One hit creates exactly one child"), Context->Spawned, 2);
        for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
            if (It->Prototype == P->OnHit)
                It->Tick(.11f);
        TestEqual(TEXT("Child preserves Actor/Mass identity and source damage context"),
                  bActorTarget ? F.Health(Actor) : F.Health(Id), 950.f);
        TestEqual(TEXT("Targeted child excludes other representation"), bActorTarget ? F.Health(Id) : F.Health(Actor),
                  1000.f);
    }
    for (K Kind : {K::Emitter, K::Delay})
    {
        FFixture F;
        if (!F.Ready(*this))
            return false;
        auto* Actor = F.Character(FVector(100, 0, 0));
        if (!TestNotNull(TEXT("Timed child Actor"), Actor))
            return false;
        const int32 Id = F.Enemy(FVector(150, 0, 0));
        auto* P = F.Config(Kind);
        P->Count = 1;
        if (Kind == K::Emitter)
            P->OnPeriod = F.Config(K::Burst);
        else
            P->OnExpire = F.Config(K::Burst);
        auto* Context = F.Context();
        auto* Root = F.Start(P, {}, Context);
        if (!TestNotNull(TEXT("Timed parent"), Root))
            return false;
        Root->Tick(.15f);
        F.BeginChildren();
        Root->Tick(.15f);
        F.BeginChildren();
        TestEqual(TEXT("Timed child damages Actor exactly once"), F.Health(Actor), 975.f);
        TestEqual(TEXT("Same child damages Mass exactly once"), F.Health(Id), 975.f);
        TestEqual(TEXT("Finished parent does not emit again"), Context->Spawned, 2);
    }
    FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Actor = F.Character(FVector(100, 0, 0));
    if (!TestNotNull(TEXT("Recursive composition Actor"), Actor))
        return false;
    F.Enemy(FVector(100, 100, 0));
    auto* P = F.Config(K::Burst);
    P->OnHit = P;
    auto* Context = F.Context();
    if (!TestNotNull(TEXT("Recursive parent"), F.Start(P, {}, Context)))
        return false;
    F.BeginChildren();
    TestEqual(TEXT("Two targets at each level remain bounded to depth three"), Context->Spawned, 15);
    TestFalse(TEXT("Depth four rejected"), Context->ReserveSpawn(4));
    while (Context->ReserveSpawn(0))
    {
    }
    TestEqual(TEXT("Mixed casts retain shared sixty-four spawn limit"), Context->Spawned, 64);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedOrbit, "test_25d.PureMass.Mixed.OrbitActorTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.OrbitActorTarget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedOrbit::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    if (!F.Ready(*this))
        return false;
    F.Caster->SetActorLocation(FVector::ZeroVector);
    auto* Actor = F.Character(FVector(150, 0, 0));
    if (!TestNotNull(TEXT("Orbit Actor"), Actor))
        return false;
    auto* P = F.Config(K::Orbit);
    P->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(F.World);
    P->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
    P->OrbitSettings->SpawnInterval = .1f;
    P->OrbitSettings->LaunchInterval = .05f;
    P->OrbitSettings->OrbitRadius = 50.f;
    P->OrbitSettings->OrbitHeight = 0.f;
    P->OrbitSettings->OrbitDegreesPerSecond = 0.f;
    P->OrbitSettings->ProjectileSpeed = 1000.f;
    auto* R = F.Start(P, FTarget::FromActor(Actor));
    if (!TestNotNull(TEXT("Actor orbit runtime"), R))
        return false;
    R->Tick(.11f);
    TestEqual(TEXT("Orbit uses real GAS damage for Actor hit"), F.Health(Actor), 975.f);
    TestEqual(TEXT("Orbit removes consumed bolt"), R->OrbitBolts.Num(), 0);
    Actor->Destroy();
    R->Tick(.11f);
    TestEqual(TEXT("Lost Actor leaves a new bolt orbiting"), R->OrbitBolts.Num(), 1);
    if (R->OrbitBolts.Num() == 1)
        TestFalse(TEXT("Stale target does not launch bolt"), R->OrbitBolts[0].bLaunched);
    R->CancelCast();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedGroundValidation,
                                 "test_25d.PureMass.Mixed.GroundValidationIgnoresCombatants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.GroundValidationIgnoresCombatants 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedGroundValidation::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Ally = F.Character(FVector(-300, 0, 0), TEXT("Player"));
    auto* Corpse = F.Character(FVector(-150, 0, 0));
    if (!TestNotNull(TEXT("Ground ray ally"), Ally) || !TestNotNull(TEXT("Ground ray corpse"), Corpse))
        return false;
    auto* DeadProperty = FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"));
    if (!TestNotNull(TEXT("Ground fixture death state"), DeadProperty))
        return false;
    DeadProperty->SetPropertyValue_InContainer(Corpse, true);
    auto* P = F.Config(K::Burst);
    const FVector Point(110, 0, 0);
    FVector Resolved;
    TestTrue(TEXT("Ally and corpse capsules do not block ground targeting"),
             UPureMassAbility::ValidateCombatTarget(F.Caster, P, {}, Point, Resolved));
    TestTrue(TEXT("Ground target retains requested point"), Resolved.Equals(Point));
    // The front face is 10 cm before the requested point, inside the existing 15 cm allowance.
    F.Wall(FVector(110, 0, 0));
    TestTrue(TEXT("Ground geometry within endpoint tolerance accepted"),
             UPureMassAbility::ValidateCombatTarget(F.Caster, P, {}, Point, Resolved));
    F.Wall(FVector(-100, 0, 0));
    TestFalse(TEXT("Actual wall before ground target rejects the cast"),
              UPureMassAbility::ValidateCombatTarget(F.Caster, P, {}, Point, Resolved));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedDeadCaster, "test_25d.PureMass.Mixed.DeadCasterCannotSpawnBurst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.DeadCasterCannotSpawnBurst 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedDeadCaster::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Actor = F.Character(FVector(100, 0, 0));
    if (!TestNotNull(TEXT("Dead caster Actor target"), Actor))
        return false;
    const int32 Id = F.Enemy(FVector(150, 0, 0));
    auto* DeadProperty = FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"));
    if (!TestNotNull(TEXT("Caster death state"), DeadProperty))
        return false;
    DeadProperty->SetPropertyValue_InContainer(F.Caster, true);
    auto* Context = F.Context();
    TestNull(TEXT("Dead caster cannot create immediate Burst"), F.Start(F.Config(K::Burst), {}, Context));
    TestEqual(TEXT("Rejected dead caster does not damage Actor"), F.Health(Actor), 1000.f);
    TestEqual(TEXT("Rejected dead caster does not damage Mass"), F.Health(Id), 1000.f);
    TestEqual(TEXT("Rejected cast does not consume child budget"), Context->Spawned, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMixedSweepEndpoint, "test_25d.PureMass.Mixed.SweepEndpointAndWallTie",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Mixed.SweepEndpointAndWallTie 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMixedSweepEndpoint::RunTest(const FString&)
{
    using namespace PureMassMixedTests;
    for (bool bActor : {false, true})
    {
        for (bool bWallTie : {false, true})
        {
            FFixture F;
            if (!F.Ready(*this))
                return false;
            // A point trace touches the 20 cm capsule at x=100 (endpoint) or x=50 (wall tie).
            const FVector Center(bWallTie ? 70 : 120, 0, 0);
            FTarget Target;
            if (bActor)
            {
                auto* Actor = F.Character(Center);
                if (!TestNotNull(TEXT("Endpoint Actor"), Actor))
                    return false;
                Target = FTarget::FromActor(Actor);
            }
            else
                Target = F.Enemy(Center);
            if (bWallTie)
                F.Wall(FVector(60, 0, 0));
            float Time = 0.f;
            FHitResult Wall;
            const auto Result =
                PureMassCombat::Sweep(F.World, F.Caster, FVector::ZeroVector, FVector(100, 0, 0), 0.f, Time, Wall);
            if (bWallTie)
            {
                TestTrue(TEXT("Wall tie fixture has a blocking wall"), Wall.bBlockingHit);
                TestEqual(TEXT("Wall contact at exact shared time"), Wall.Time, .5f);
                TestFalse(TEXT("Wall wins exact tie against either representation"), Result.IsSet());
            }
            else
            {
                TestTrue(TEXT("Exact endpoint returns the correct Actor/Mass identity"), Result == Target);
                TestEqual(TEXT("Exact endpoint has time one"), Time, 1.f);
                TestFalse(TEXT("Endpoint fixture has no wall"), Wall.bBlockingHit);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLifecycleRadialSubscriptionTest, "test_25d.Lifecycle.RadialSubscriptionScope",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Lifecycle.RadialSubscriptionScope 回归，失败信息由下方断言交给自动化框架收集。 */
bool FLifecycleRadialSubscriptionTest::RunTest(const FString&)
{
    PureMassMixedTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Target = F.Character(FVector::ZeroVector);
    if (!TestNotNull(TEXT("Target"), Target))
        return false;
    for (int32 Iteration = 0; Iteration < 3; ++Iteration)
    {
        auto Params = F.Context()->DamageParams;
        Params.TargetAbilitySystemComponent = Target->GetAbilitySystemComponent();
        Params.BaseDamage = 10;
        Params.DamageType = FMyGameplayTags::Get().Damage_Fire;
        Params.bIsRadialDamage = true;
        Params.RadialDamageInnerRadius = 100;
        Params.RadialDamageOuterRadius = 200;
        Params.RadialDamageOrigin = FVector::ZeroVector;
        const float Before = F.Health(Target);
        UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
        TestTrue(TEXT("Actual GE applied damage"), F.Health(Target) < Before);
        TestFalse(TEXT("No stack-capturing delegate survives synchronous damage"),
                  Target->GetOnDamageSignature().IsBound());
        // Old implementation leaves a dangling stack reference. Remove it WITHOUT broadcasting.
        Target->GetOnDamageSignature().Clear();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMovementIntegrationTest, "test_25d.PureMass.Movement.WorldConstraint",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Movement.WorldConstraint 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassMovementIntegrationTest::RunTest(const FString&)
{
    PureMassMixedTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    F.Mass->BeginUpdate(1.f / 60.f);
    FPureMassFragment Front;
    Front.Id = 10001;
    Front.Type = F.Type;
    Front.Health = 100;
    Front.Location = FVector(100, 0, 200);
    F.Mass->Publish(Front);
    FPureMassFragment Back = Front;
    Back.Id = 10002;
    Back.Location = FVector(0, 0, 200);
    F.Mass->Publish(Back);
    // Exercise the production movement + spatial-index integration, not just geometry.
    F.Mass->Move(Back, FVector(200, 0, 0));
    TestNearlyEqual(TEXT("Cannot tunnel through front entity"), float(Back.Location.X), 59.f, .01f);
    TestNearlyEqual(TEXT("Crowd contact does not raise capsule"), float(Back.Location.Z), 200.f, .01f);
    F.Mass->Publish(Back);
    F.Mass->Move(Back, FVector(20, 0, 0));
    TestNearlyEqual(TEXT("Repeated push remains outside neighbour"), float(Back.Location.X), 59.f, .01f);
    F.Mass->Move(Back, FVector(0, 80, 0));
    TestTrue(TEXT("Can move laterally around blocked row"), Back.Location.Y > 79.f);
    Back.Location = FVector(0, 0, 400);
    F.Mass->Move(Back, FVector(200, 0, 0));
    TestNearlyEqual(TEXT("Other floor does not block XY"), float(Back.Location.X), 200.f, .01f);
    return true;
}
#endif
