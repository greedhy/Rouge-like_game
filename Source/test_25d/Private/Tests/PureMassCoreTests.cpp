/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.NativeIncomingDamageDefinition
 * - test_25d.PureMass.LethalImpulsePresentation
 * - test_25d.PureMass.CapsuleSweeps
 * - test_25d.PureMass.ActorlessLifecycleAndStableIds
 * - test_25d.PureMass.ClientAuthorityAndSnapshotRemoval
 * - test_25d.PureMass.CachedPathCollisionTerrainAndNoNav
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassProcessor.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Terrain/TerrainSurface.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassIncomingDamageEffectTest, "test_25d.PureMass.NativeIncomingDamageDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.NativeIncomingDamageDefinition 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassIncomingDamageEffectTest::RunTest(const FString&)
{
    const UPureMassIncomingDamageEffect* Effect = GetDefault<UPureMassIncomingDamageEffect>();
    TestTrue(TEXT("Damage definition is a native CDO"),
             Effect->HasAnyFlags(RF_ClassDefaultObject) && Effect->GetClass()->HasAnyClassFlags(CLASS_Native));
    TestEqual(TEXT("Definition has stable script package instead of transient package"),
              Effect->GetOutermost()->GetName(), FString(TEXT("/Script/test_25d")));
    TestEqual(TEXT("Existing physical damage tag reused"), UPureMassIncomingDamageEffect::GetDamageTag(),
              FMyGameplayTags::Get().Damage_Physics);
    TestTrue(TEXT("Damage is instant"), Effect->DurationPolicy == EGameplayEffectDurationType::Instant);
    if (!TestEqual(TEXT("One incoming damage modifier"), Effect->Modifiers.Num(), 1))
        return false;
    TestTrue(TEXT("Modifier targets player incoming damage"),
             Effect->Modifiers[0].Attribute == UMyAttributeSet::GetIncomingDamageAttribute());
    FGameplayEffectSpec First(Effect, FGameplayEffectContextHandle(), 1.f);
    First.SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), 23.5f);
    First.CalculateModifierMagnitudes();
    TestEqual(TEXT("SetByCaller damage evaluates on first spec"), First.GetModifierMagnitude(0), 23.5f);
    FGameplayEffectSpec Second(Effect, FGameplayEffectContextHandle(), 1.f);
    Second.SetSetByCallerMagnitude(UPureMassIncomingDamageEffect::GetDamageTag(), 7.f);
    Second.CalculateModifierMagnitudes();
    TestEqual(TEXT("Second hit has its own damage value"), Second.GetModifierMagnitude(0), 7.f);
    TestEqual(TEXT("Shared definition does not overwrite another hit"), First.GetModifierMagnitude(0), 23.5f);
    return true;
}

namespace
{
    struct FPureMassTestWorld
    {
        UWorld* World;
        explicit FPureMassTestWorld(EWorldType::Type Type = EWorldType::Game)
        {
            World = UWorld::CreateWorld(Type, false);
            GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
        }
        ~FPureMassTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
    int32 ActorCount(UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
            ++Count;
        return Count;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassDeathImpulseTest, "test_25d.PureMass.LethalImpulsePresentation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.LethalImpulsePresentation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassDeathImpulseTest::RunTest(const FString&)
{
    FPureMassTestWorld Scope;
    auto* Sim = Scope.World->GetSubsystem<UPureMassSubsystem>();
    auto* Crowd = Scope.World->SpawnActor<APureMassCrowd>();
    if (!TestNotNull(TEXT("Simulation"), Sim) || !TestNotNull(TEXT("Crowd"), Crowd))
        return false;
    Sim->Enable();
    auto* Type = NewObject<UPureMassMonsterType>();
    const int32 Id = Sim->SpawnMonster(Type, FVector(0, 0, 60));
    FPureMassDamage Damage;
    Damage.Amount = 1.f;
    Damage.DeathImpulse = FVector(450, 0, 200);
    Sim->ApplyDamage(Id, Damage);
    TestEqual(TEXT("Nonlethal hit does not queue death"), Crowd->PendingDeaths.Num(), 0);
    Damage.Amount = 10000.f;
    Sim->ApplyDamage(Id, Damage);
    if (!TestEqual(TEXT("Lethal hit queues one presentation"), Crowd->PendingDeaths.Num(), 1))
        return false;
    TestTrue(TEXT("Lethal impulse survives subsystem and queue"),
             FVector(Crowd->PendingDeaths[0].DeathImpulse).Equals(Damage.DeathImpulse));
    Sim->ApplyDamage(Id, Damage);
    TestEqual(TEXT("Repeated lethal hit does not duplicate presentation"), Crowd->PendingDeaths.Num(), 1);
    FPureMassDamage DefaultDamage;
    DefaultDamage.Amount = 10000.f;
    Sim->ApplyDamage(Sim->SpawnMonster(Type, FVector(100, 0, 60)), DefaultDamage);
    if (!TestEqual(TEXT("Next life queues independently"), Crowd->PendingDeaths.Num(), 2))
        return false;
    TestTrue(TEXT("Default hit cannot inherit previous impulse"),
             FVector(Crowd->PendingDeaths[1].DeathImpulse).IsZero());
    TestEqual(TEXT("Deaths remain once per entity"), Sim->GetDeathCount(), int64(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassGeometryTest, "test_25d.PureMass.CapsuleSweeps",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.CapsuleSweeps 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassGeometryTest::RunTest(const FString&)
{
    float Time;
    auto Sweep = [&](FVector Start, FVector End, float R = 0)
    {
        return PureMassGeometry::SweepCapsule(Start, End, R, FVector::ZeroVector, 32, 60, Time);
    };
    TestTrue(TEXT("Cylinder side hit"), Sweep(FVector(-100, 0, 0), FVector(100, 0, 0)));
    TestTrue(TEXT("First contact is exact"), FMath::IsNearlyEqual(Time, .34f));
    TestTrue(TEXT("Inflated sweep hit"), Sweep(FVector(-100, 0, 0), FVector(100, 0, 0), 8));
    TestTrue(TEXT("Sphere radius expands capsule"), FMath::IsNearlyEqual(Time, .30f));
    TestTrue(TEXT("Top cap hit"), Sweep(FVector(0, 0, 100), FVector(0, 0, -100)));
    TestTrue(TEXT("Cap contact exact"), FMath::IsNearlyEqual(Time, .20f));
    TestFalse(TEXT("A different floor is excluded"), Sweep(FVector(-100, 0, 100), FVector(100, 0, 100)));
    TestTrue(TEXT("Initial overlap reports t0"), Sweep(FVector::ZeroVector, FVector::ZeroVector));
    TestEqual(TEXT("Initial overlap time"), Time, 0.f);
    TestFalse(TEXT("Stationary non-overlap"), Sweep(FVector(100, 0, 0), FVector(100, 0, 0)));
    TestFalse(TEXT("Negative radius rejected"), Sweep(FVector(-100, 0, 0), FVector(100, 0, 0), -1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassLifecycleTest, "test_25d.PureMass.ActorlessLifecycleAndStableIds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.ActorlessLifecycleAndStableIds 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassLifecycleTest::RunTest(const FString&)
{
    FPureMassTestWorld Scope;
    auto* Sim = Scope.World->GetSubsystem<UPureMassSubsystem>();
    auto* Mass = Scope.World->GetSubsystem<UMassEntitySubsystem>();
    if (!TestNotNull(TEXT("Simulation subsystem"), Sim) || !TestNotNull(TEXT("Real Mass subsystem"), Mass))
        return false;
    UPureMassMonsterType* Type = NewObject<UPureMassMonsterType>();
    TestTrue(TEXT("Existing worlds untouched"), Sim->SpawnMonster(Type, FVector::ZeroVector) <= 0);
    const int32 Before = ActorCount(Scope.World);
    Sim->Enable();
    const int32 First = Sim->SpawnMonster(Type, FVector(0, 0, 60));
    const int32 Second = Sim->SpawnMonster(Type, FVector(100, 0, 60));
    TestTrue(TEXT("Positive stable ids"), First > 0 && Second > First);
    TestEqual(TEXT("No per-enemy Actor created"), ActorCount(Scope.World), Before);
    auto& Manager = Mass->GetMutableEntityManager();
    const FMassEntityHandle Handle = Sim->GetEntity(First);
    TestTrue(TEXT("Real entity owns fragment"), Manager.IsEntityValid(Handle));
    FPureMassFragment& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Handle);
    TestEqual(TEXT("Fragment owns health"), Fragment.Health, Type->Health);
    Manager.GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Second)).Path.Add(FVector(600, 700, 60));
    TestEqual(TEXT("Sphere selects exact centre distance"), Sim->QueryRadius(FVector(0, 0, 60), 32).Num(), 1);
    float Time;
    TestEqual(TEXT("Sweep selects nearest capsule"), Sim->Sweep(FVector(-100, 0, 60), FVector(300, 0, 60), 0, Time),
              First);
    TSet<int32> Ignored;
    Ignored.Add(First);
    TestEqual(TEXT("Piercing ignores previous id"),
              Sim->Sweep(FVector(-100, 0, 60), FVector(300, 0, 60), 0, Time, &Ignored), Second);
    AActor* Caster = Scope.World->SpawnActor<AActor>();
    UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Caster);
    Caster->AddInstanceComponent(ASC);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(Caster, Caster);
    int32 XPEvents = 0;
    float AwardedXP = 0;
    ASC->GenericGameplayEventCallbacks.FindOrAdd(FMyGameplayTags::Get().Attributes_Meta_IncomingXP)
        .AddLambda(
            [&](const FGameplayEventData* Payload)
            {
                ++XPEvents;
                AwardedXP += Payload->EventMagnitude;
            });
    FPureMassDamage Damage;
    Damage.Amount = 1000;
    Damage.Source = Caster;
    TestEqual(TEXT("Damage returns HP actually removed"), Sim->ApplyDamage(First, Damage), 100.f);
    TestEqual(TEXT("Duplicate lethal damage is rejected"), Sim->ApplyDamage(First, Damage), 0.f);
    TestEqual(TEXT("Death processed exactly once"), Sim->GetDeathCount(), int64(1));
    TestEqual(TEXT("XP event awarded exactly once"), XPEvents, 1);
    TestEqual(TEXT("XP amount comes from monster type"), AwardedXP, float(Type->XPReward));
    FPureMassView View;
    TestFalse(TEXT("Stale id immediately fails Resolve"), Sim->Resolve(First, View));
    TestFalse(TEXT("Dead id immediately removed from queries"),
              Sim->QueryRadius(FVector(0, 0, 60), 32).Contains(First));
    Sim->FinishUpdate();
    TestFalse(TEXT("Dead entity is destroyed"), Manager.IsEntityValid(Handle));
    TestEqual(TEXT("Nontrivial cached path survives chunk compaction"),
              Manager.GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Second)).Path[0], FVector(600, 700, 60));
    const int32 Third = Sim->SpawnMonster(Type, FVector(0, 0, 60));
    TestTrue(TEXT("Entity slot reuse never reuses external id"), Third > Second);
    TestEqual(TEXT("Stale id cannot damage replacement"), Sim->ApplyDamage(First, Damage), 0.f);
    Sim->SetClientSnapshots({});
    TestEqual(TEXT("Authority ignores forged client snapshots"), Sim->GetSnapshots().Num(), 2);
    // Scope teardown deliberately leaves living Mass entities, testing subsystem destruction ordering.
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassClientTest, "test_25d.PureMass.ClientAuthorityAndSnapshotRemoval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.ClientAuthorityAndSnapshotRemoval 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassClientTest::RunTest(const FString&)
{
    FPureMassTestWorld Scope(EWorldType::PIE);
    Scope.World->SetPlayInEditorInitialNetMode(NM_Client);
    auto* Sim = Scope.World->GetSubsystem<UPureMassSubsystem>();
    if (!TestNotNull(TEXT("Client subsystem"), Sim))
        return false;
    TestEqual(TEXT("Test really uses client net mode"), Scope.World->GetNetMode(), NM_Client);
    UPureMassMonsterType* Type = NewObject<UPureMassMonsterType>();
    Sim->Enable();
    TestTrue(TEXT("Client cannot create authority entities"), Sim->SpawnMonster(Type, FVector::ZeroVector) <= 0);
    FPureMassView View;
    View.Id = 42;
    View.Type = Type;
    View.Health = View.MaxHealth = 100;
    Sim->SetClientSnapshots({View});
    FPureMassView Read;
    TestTrue(TEXT("Client can resolve replicated ids"), Sim->Resolve(42, Read));
    FPureMassDamage Damage;
    Damage.Amount = 100;
    TestEqual(TEXT("Client damage rejected"), Sim->ApplyDamage(42, Damage), 0.f);
    TestFalse(TEXT("Client processor disabled"), Sim->BeginUpdate(.1f));
    Sim->Resolve(42, Read);
    TestEqual(TEXT("Client health unchanged"), Read.Health, 100.f);
    Sim->SetClientSnapshots({});
    TestFalse(TEXT("Removed replication entries clear registry"), Sim->Resolve(42, Read));
    TestEqual(TEXT("Removed replication entries clear spatial index"), Sim->QueryRadius(FVector::ZeroVector, 100).Num(),
              0);
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassProcessorTest, "test_25d.PureMass.CachedPathCollisionTerrainAndNoNav",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.CachedPathCollisionTerrainAndNoNav 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassProcessorTest::RunTest(const FString&)
{
    FPureMassTestWorld Scope;
    auto* Sim = Scope.World->GetSubsystem<UPureMassSubsystem>();
    auto* Mass = Scope.World->GetSubsystem<UMassEntitySubsystem>();
    if (!Sim || !Mass)
        return false;
    Sim->Enable();
    UPureMassMonsterType* Type = NewObject<UPureMassMonsterType>();
    Type->Speed = 1000;
    Type->AttackRange = 20;
    const int32 Id = Sim->SpawnMonster(Type, FVector(0, 0, 60));
    auto& Manager = Mass->GetMutableEntityManager();
    auto* PC = Scope.World->SpawnActor<APlayerController>();
    Scope.World->AddController(PC);
    ACharacter* Player = Scope.World->SpawnActor<ACharacter>(FVector(1000, 0, 60), FRotator::ZeroRotator);
    PC->SetPawn(Player);
    auto* Processor = NewObject<UPureMassProcessor>(Scope.World);
    Processor->CallInitialize(Scope.World, Manager.AsShared());
    auto Step = [&]()
    {
        auto Context = Manager.CreateExecutionContext(.1f);
        Context.SetExecutionType(EMassExecutionContextType::Processor);
        Processor->CallExecute(Manager, Context);
    };
    Step();
    FPureMassView View;
    Sim->Resolve(Id, View);
    TestEqual(TEXT("Missing navmesh never direct-chases through walls"), View.Location.X, 0.0);
    AActor* Wall = Scope.World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(10, 200, 200));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(100, 0, 60));
    FPureMassFragment& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Id));
    Fragment.Path = {FVector(1000, 0, 60)};
    Fragment.PathGoal = Player->GetActorLocation();
    Fragment.NextPathTime = MAX_flt;
    Step();
    Sim->Resolve(Id, View);
    TestTrue(TEXT("Cached nav path moves entity"), View.Location.X > 0);
    TestTrue(TEXT("World capsule sweep stops before wall"), View.Location.X <= 58.1);
    // Regression: being within melee range behind a wall must continue navigating around it.
    Type->AttackRange = 2000;
    const FVector BeforeBlockedChase = View.Location;
    Fragment.Path = {View.Location + FVector(0, 300, 0), FVector(1000, 300, 60), Player->GetActorLocation()};
    Fragment.PathPoint = 0;
    FHitResult BlockedHit;
    TestTrue(
        TEXT("Close-range target really has blocked line of sight"),
        Scope.World->LineTraceSingleByChannel(BlockedHit, View.Location, Player->GetActorLocation(), ECC_Visibility));
    Step();
    Sim->Resolve(Id, View);
    TestEqual(TEXT("Blocked line of sight does not start an attack"), Fragment.AttackRemaining, 0.f);
    TestTrue(TEXT("Blocked close-range target follows cached route around wall"),
             View.Location.Y > BeforeBlockedChase.Y + 1);
    TestEqual(TEXT("Blocked close-range navigation publishes run state"), View.State, uint8(1));
    Type->AttackRange = 20;
    FPureMassDamage Root;
    Root.bRoot = true;
    Root.StatusDuration = 1;
    Sim->ApplyDamage(Id, Root);
    const FVector RootPosition = View.Location;
    Step();
    Sim->Resolve(Id, View);
    TestTrue(TEXT("Entity root stops path movement"), View.Location.Equals(RootPosition, .01));
    auto* Surface = Scope.World->SpawnActor<ATerrainSurface>(View.Location - FVector(0, 0, 60), FRotator::ZeroRotator);
    Surface->Config = NewObject<UTerrainSurfaceConfig>(Surface);
    Surface->Config->SurfaceType = ETerrainSurfaceType::Lava;
    Surface->Config->DamagePerSecond = 20;
    Surface->bRequireGrounded = false;
    // Refresh cached terrain list on the next simulation frame without ticking unrelated gameplay.
    Scope.World->TimeSeconds += .3;
    const float Before = View.Health;
    Step();
    Sim->Resolve(Id, View);
    TestTrue(TEXT("Terrain config damages entity without enemy component"), View.Health < Before);
    return true;
}
#endif
