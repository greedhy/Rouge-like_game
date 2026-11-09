/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Extensions.MixedBurst
 * - test_25d.Extensions.StackIsolationAndDetonation
 * - test_25d.Extensions.GraphValidation
 * - test_25d.Extensions.AttachMovesAndTransfers
 * - test_25d.Extensions.StackExpiryOwnerAndDeath
 * - test_25d.Extensions.DisplacementWallAndReplacement
 * - test_25d.Extensions.HitchExpiryAndCancellation
 * - test_25d.Extensions.LethalThresholdHit
 * - test_25d.Extensions.ExplicitPersistentCancellation
 * - test_25d.Extensions.StartingPointsRespawn
 * - test_25d.Extensions.GroundContactDoesNotBlockEffects
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassCombatTarget.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
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
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "PureMass/Extensions/MassStartingPoints.h"
#include "Player/MyPlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ExtensionTests
{
    using K = EExtensionKind;
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
        UExtensionNode* Config(K Kind)
        {
            auto* P = NewObject<UExtensionNode>(World);
            P->Behavior = Kind;
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
        UExtensionCastContext* Context()
        {
            auto* C = NewObject<UExtensionCastContext>(Caster);
            C->Caster = Caster;
            C->CastId = FGuid::NewGuid();
            C->DamageParams.SourceAbilitySystemComponent = ASC;
            C->DamageParams.DamageGameplayEffectClass = DamageEffect;
            C->DamageParams.WorldContextObject = Caster;
            C->DamageParams.DebuffChance = 0.f;
            return C;
        }
        AExtensionRuntime* Start(UExtensionNode* P, FTarget Target = {}, UExtensionCastContext* C = nullptr)
        {
            auto* R =
                AExtensionRuntime::Spawn(C ? C : Context(), P, FVector::ZeroVector, FVector::ForwardVector, Target);
            if (R && !R->HasActorBegunPlay())
                R->DispatchBeginPlay();
            return R;
        }
        void BeginChildren()
        {
            for (int32 Pass = 0; Pass < 4; ++Pass)
            {
                TArray<AExtensionRuntime*> Pending;
                for (TActorIterator<AExtensionRuntime> It(World); It; ++It)
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
} // namespace ExtensionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionBurstTest, "test_25d.Extensions.MixedBurst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.MixedBurst 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionBurstTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* Actor = F.Character(FVector(0, 100, 0));
    auto* R = F.Start(F.Config(K::Burst));
    TestNotNull(TEXT("Runtime created"), R);
    TestEqual(TEXT("Mass damaged once"), F.Health(Id), 975.f);
    TestEqual(TEXT("Actor damaged once"), F.Health(Actor), 975.f);
    if (R)
        R->Tick(1.f);
    TestEqual(TEXT("Finished burst cannot repeat"), F.Health(Id), 975.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionStackTest, "test_25d.Extensions.StackIsolationAndDetonation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.StackIsolationAndDetonation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionStackTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* S = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* Node = F.Config(K::Stack);
    Node->EffectId = TEXT("Ember");
    Node->CompleteChild = F.Config(K::Burst);
    auto Add = [&]()
    {
        return S->AddStack(F.Context(), Node, Id, FVector(100, 0, 0), FVector::ForwardVector, 1);
    };
    TestTrue(TEXT("First stack accepted"), Add());
    Add();
    TestEqual(TEXT("Two layers persist between casts"), S->LayersFor(F.Caster, TEXT("Ember"), Id), 2);
    Add();
    Add();
    TestEqual(TEXT("Pending explosion does not accumulate more"), S->LayersFor(F.Caster, TEXT("Ember"), Id), 3);
    S->Tick(.41f);
    F.BeginChildren();
    TestEqual(TEXT("Explosion damage"), F.Health(Id), 975.f);
    S->Tick(1.f);
    F.BeginChildren();
    TestEqual(TEXT("Explosion once"), F.Health(Id), 975.f);
    TestEqual(TEXT("Consumed record removed"), S->StackCount(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionGraphTest, "test_25d.Extensions.GraphValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.GraphValidation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionGraphTest::RunTest(const FString&)
{
    auto* Node = NewObject<UExtensionNode>();
    TestTrue(TEXT("Default burst is valid"), Node->ValidateGraph());
    Node->CompleteChild = Node;
    TestFalse(TEXT("Cycles rejected"), Node->ValidateGraph());
    Node->CompleteChild = nullptr;
    Node->Damage = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN rejected"), Node->ValidateGraph());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionAttachTest, "test_25d.Extensions.AttachMovesAndTransfers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.AttachMovesAndTransfers 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionAttachTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* A = F.Character(FVector(100, 0, 0));
    const int32 Id = F.Enemy(FVector(200, 0, 0));
    auto* N = F.Config(K::Attach);
    N->Duration = .5f;
    N->Period = .1f;
    N->PeriodChild = F.Config(K::Burst);
    N->PeriodChild->Radius = 35;
    auto* R = F.Start(N, FTarget::FromActor(A));
    if (!TestNotNull(TEXT("Attach runtime"), R))
        return false;
    A->SetActorLocation(FVector(130, 0, 0));
    R->Tick(.11f);
    F.BeginChildren();
    TestEqual(TEXT("Follows moving Actor"), R->GetActorLocation().X, 130.0);
    TestEqual(TEXT("Periodic payload hits Actor"), F.Health(A), 975.f);
    A->Destroy();
    R->Tick(.11f);
    F.BeginChildren();
    TestEqual(TEXT("Transfers to Mass"), R->GetTarget().EntityId, Id);
    TestEqual(TEXT("Transferred payload damages Mass"), F.Health(Id), 975.f);
    R->Tick(.5f);
    F.BeginChildren();
    TestTrue(TEXT("Hard lifetime ends"), R->bFinished);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionStackEdgeTest, "test_25d.Extensions.StackExpiryOwnerAndDeath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.StackExpiryOwnerAndDeath 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionStackEdgeTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* S = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* N = F.Config(K::Stack);
    N->EffectId = TEXT("Edge");
    N->CompleteChild = F.Config(K::Burst);
    N->StackLifetime = .2f;
    auto Add = [&](UExtensionCastContext* C)
    {
        S->AddStack(C, N, Id, FVector(100, 0, 0), FVector::ForwardVector, 1);
    };
    Add(F.Context());
    S->Tick(.21f);
    TestEqual(TEXT("Unfilled stack expires"), S->StackCount(), 0);
    auto* Other = F.Character(FVector(-400, 200, 0), TEXT("Player"));
    auto* OtherContext = F.Context();
    OtherContext->Caster = Other;
    OtherContext->DamageParams.SourceAbilitySystemComponent = Other->GetAbilitySystemComponent();
    Add(F.Context());
    Add(OtherContext);
    Add(F.Context());
    TestEqual(TEXT("Caster one layers isolated"), S->LayersFor(F.Caster, TEXT("Edge"), Id), 2);
    TestEqual(TEXT("Caster two layer isolated"), S->LayersFor(Other, TEXT("Edge"), Id), 1);
    Add(F.Context());
    const int32 Neighbor = F.Enemy(FVector(150, 0, 0));
    FPureMassDamage D;
    D.Source = F.Caster;
    D.Amount = 2000;
    F.Mass->ApplyDamage(Id, D);
    S->Tick(.01f);
    F.BeginChildren();
    TestEqual(TEXT("Dead marked enemy explodes at last position"), F.Health(Neighbor), 975.f);
    S->Tick(1);
    F.BeginChildren();
    TestEqual(TEXT("Death cannot detonate twice"), F.Health(Neighbor), 975.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionMoveTest, "test_25d.Extensions.DisplacementWallAndReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.DisplacementWallAndReplacement 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionMoveTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Floor = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(1000, 1000, 10));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Floor->SetActorLocation(FVector(0, 0, -51));
    const int32 Id = F.Enemy(FVector(200, 0, 0));
    auto* N = F.Config(K::Displace);
    N->Duration = .5;
    N->MoveDistance = 180;
    N->SafeDistance = 70;
    auto* S = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    auto* C = F.Context();
    FGuid First = S->RequestDisplacement(C, N, Id, FVector::ZeroVector, FVector::ForwardVector);
    TestTrue(TEXT("Move accepted on floor"), First.IsValid());
    FGuid Second = S->RequestDisplacement(C, N, Id, FVector::ZeroVector, FVector::ForwardVector);
    TestTrue(TEXT("Previous command replaced"), S->WasMoveReplaced(First));
    auto& Fragment = F.World->GetSubsystem<UMassEntitySubsystem>()
                         ->GetMutableEntityManager()
                         .GetFragmentDataChecked<FPureMassFragment>(F.Mass->GetEntity(Id));
    S->MoveMass(Fragment, .5f);
    TestTrue(TEXT("Pull stops at safe centre distance"), FMath::IsNearlyEqual(Fragment.Location.X, 70.0, 1.));
    TestTrue(TEXT("Navigation suppressed for displacement frame"), Fragment.RootRemaining > .5f);
    F.Wall(FVector(0, 0, 0));
    FVector End;
    TestTrue(TEXT("Wall sweep returns legal partial move"),
             UExtensionWorldSubsystem::ConstrainMove(F.World, nullptr, FVector(100, 0, 0), FVector(-100, 0, 0), 20, 40,
                                                     End));
    TestTrue(TEXT("Wall not crossed"), End.X >= 29);
    TestFalse(TEXT("No floor outside room rejects movement"),
              UExtensionWorldSubsystem::ConstrainMove(F.World, nullptr, FVector(900, 800, 0), FVector(1500, 800, 0), 20,
                                                      40, End));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionHitchTest, "test_25d.Extensions.HitchExpiryAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.HitchExpiryAndCancellation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionHitchTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    const int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* N = F.Config(K::Attach);
    N->Duration = .4f;
    N->Period = .1f;
    N->PeriodChild = F.Config(K::Burst);
    N->PeriodChild->Radius = 35;
    auto* R = F.Start(N, Id);
    if (!TestNotNull(TEXT("Runtime"), R))
        return false;
    R->Tick(.45f);
    F.BeginChildren();
    TestEqual(TEXT("Hitch catches up three pulses before expiry, excludes boundary"), F.Health(Id), 925.f);
    TestTrue(TEXT("Expires after catch-up"), R->bFinished);
    auto* C = F.Context();
    R = F.Start(N, Id, C);
    C->bCancelled = true;
    R->Tick(.2f);
    F.BeginChildren();
    TestEqual(TEXT("Cancellation emits no pulses"), F.Health(Id), 925.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionLethalStackTest, "test_25d.Extensions.LethalThresholdHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.LethalThresholdHit 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionLethalStackTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    for (bool ActorTarget : {false, true})
    {
        ExtensionTests::FFixture F;
        if (!F.Ready(*this))
            return false;
        FTarget Target;
        if (ActorTarget)
        {
            auto* Enemy = F.Character(FVector(100, 0, 0));
            Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 60.f);
            Target = FTarget::FromActor(Enemy);
        }
        else
        {
            F.Type->Health = 60.f;
            Target = FTarget(F.Enemy(FVector(100, 0, 0)));
            F.Type->Health = 1000.f;
        }
        int32 Bystander = F.Enemy(FVector(130, 0, 0));
        auto* Stack = F.Config(K::Stack);
        Stack->EffectId = TEXT("LethalTest");
        Stack->CompleteChild = F.Config(K::Burst);
        Stack->CompleteChild->Radius = 60;
        auto* Hit = F.Config(K::Projectile);
        Hit->HitChild = Stack;
        for (int32 I = 0; I < 3; ++I)
        {
            auto* R = F.Start(Hit);
            if (!TestNotNull(TEXT("Valid hit graph"), R))
                return false;
            R->Hit(Target);
            F.BeginChildren();
        }
        auto* S = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        S->Tick(.1f);
        F.BeginChildren();
        TestEqual(ActorTarget ? TEXT("Actor lethal third hit explodes") : TEXT("Mass lethal third hit explodes"),
                  F.Health(Bystander), 975.f);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionPublicCancelTest, "test_25d.Extensions.ExplicitPersistentCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.ExplicitPersistentCancellation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionPublicCancelTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    int32 Id = F.Enemy(FVector(100, 0, 0));
    auto* N = F.Config(K::Attach);
    N->PeriodChild = F.Config(K::Burst);
    auto* R = F.Start(N, FTarget(Id));
    F.World->GetSubsystem<UExtensionWorldSubsystem>()->CancelCasterEffects(F.Caster);
    R->Tick(.2f);
    F.BeginChildren();
    TestEqual(TEXT("Explicit cancellation emits no more damage"), F.Health(Id), 1000.f);
    TestTrue(TEXT("Runtime cancelled"), R->bFinished);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionPointsRespawnTest, "test_25d.Extensions.StartingPointsRespawn",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.StartingPointsRespawn 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionPointsRespawnTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* S = NewObject<UMassStartingPointsSession>(F.World->GetGameInstance());
    auto* State = F.World->SpawnActor<AMyPlayerState>();
    State->SetTravelPlayerSlot(0);
    F.Caster->SetPlayerState(State);
    S->ObservePlayer(State);
    State->SetSpellPoints(990);
    auto* Replacement = F.Character(FVector(-600, 0, 0), TEXT("Player"));
    Replacement->SetPlayerState(State);
    State->SetSpellPoints(999); // LoadProgress restores a checkpoint before the next world tick.
    S->ObservePlayer(State);
    TestEqual(TEXT("Respawn retains balance spent since checkpoint"), State->GetSpellPoints(), 990);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionGroundContactTest, "test_25d.Extensions.GroundContactDoesNotBlockEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.GroundContactDoesNotBlockEffects 回归，失败信息由下方断言交给自动化框架收集。 */
bool FExtensionGroundContactTest::RunTest(const FString&)
{
    using namespace ExtensionTests;
    ExtensionTests::FFixture F;
    if (!F.Ready(*this))
        return false;
    auto* Floor = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(1000, 1000, 10));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Floor->SetActorLocation(FVector(0, 0, -9.999));
    int32 Id = F.Enemy(FVector(100, 0, 40));
    F.Start(F.Config(K::Burst));
    TestEqual(TEXT("Ground-origin burst is not occluded by its supporting floor"), F.Health(Id), 975.f);
    FVector End;
    TestTrue(TEXT("Floor contact permits horizontal displacement"),
             UExtensionWorldSubsystem::ConstrainMove(F.World, nullptr, FVector(100, 0, 40), FVector(150, 0, 40), 20, 40,
                                                     End));
    TestTrue(TEXT("Horizontal displacement reaches destination"), End.X > 149);
    return true;
}
#endif
