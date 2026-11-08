/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Pool.DeathDoesNotDestroy
 * - test_25d.Roguelike.Pool.GasResetAndReuse
 * - test_25d.Roguelike.Pool.CapacityAndQuarantine
 * - test_25d.Roguelike.Pool.PerformanceReport
 * - test_25d.Roguelike.Pool.BlackboardLeaseIsolation
 * - test_25d.Roguelike.Pool.RealBlueprintRagdollRestore
 * - test_25d.Lifecycle.PoolOldCallback
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "TimerManager.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"
#include "Roguelike/DungeonEnemyPool.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonRunProfile.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "MyGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "AI/MyAIController.h"
#include "Components/CapsuleComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPooledDeathTest, "test_25d.Roguelike.Pool.DeathDoesNotDestroy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.DeathDoesNotDestroy 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPooledDeathTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    Enemy->Tags.Add(TEXT("DungeonEncounter"));
    Enemy->Tags.Add(TEXT("DungeonPoolManaged"));
    Enemy->Die(FVector::ZeroVector);
    TestTrue(TEXT("Dead actors remain excluded from combat"), Enemy->IsDead_Implementation());
    TestFalse(TEXT("Corpse remains visible while ragdoll settles"), Enemy->IsHidden());
    TestTrue(TEXT("Weapon stays attached"), Enemy->GetWeapon_Implementation()->GetAttachParent() == Enemy->GetMesh());
    TestFalse(TEXT("Death stops actor tick"), Enemy->IsActorTickEnabled());
    TestEqual(TEXT("Pooled death does not schedule destruction"), Enemy->GetLifeSpan(), 0.f);
    Enemy->Die(FVector::ZeroVector);
    TestTrue(TEXT("Repeated death stays terminal"), Enemy->IsDead_Implementation());
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPoolReuseTest, "test_25d.Roguelike.Pool.GasResetAndReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.GasResetAndReuse 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPoolReuseTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    auto* ASC = Enemy->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Enemy, Enemy);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Enemy));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    Enemy->SetActorTickEnabled(true);
    Enemy->GetMesh()->SetComponentTickEnabled(true);
    Enemy->EnableDungeonPooling();
    for (int32 Lease = 0; Lease < 3; ++Lease)
    {
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1.f);
        ASC->SetLooseGameplayTagCount(FMyGameplayTags::Get().Debuff_Stun, 1);
        auto* GE = NewObject<UGameplayEffect>(World);
        GE->DurationPolicy = EGameplayEffectDurationType::Infinite;
        auto& Mod = GE->Modifiers.AddDefaulted_GetRef();
        Mod.Attribute = UMyAttributeSet::GetArmorAttribute();
        Mod.ModifierOp = EGameplayModOp::Additive;
        Mod.ModifierMagnitude = FScalableFloat(7.f);
        FGameplayEffectSpec Spec(GE, ASC->MakeEffectContext(), 1.f);
        ASC->ApplyGameplayEffectSpecToSelf(Spec);
        Enemy->Die(FVector::ZeroVector);
        TestFalse(TEXT("Wake rejected until damage callback cleanup has completed"),
                  Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("Early")));
        Enemy->SleepForDungeonPool();
        Enemy->SleepForDungeonPool();
        TestTrue(TEXT("Hidden and dead while idle"), Enemy->IsHidden() && Enemy->IsDead_Implementation());
        TestFalse(TEXT("Idle actor tick disabled"), Enemy->IsActorTickEnabled());
        TestFalse(TEXT("Idle skeletal tick disabled"), Enemy->GetMesh()->IsComponentTickEnabled());
        TestFalse(TEXT("Idle collision disabled"), Enemy->GetActorEnableCollision());
        TestEqual(TEXT("All active effects removed"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
        TestFalse(TEXT("Loose stun removed"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
        TestTrue(TEXT("Same Actor can be reactivated"),
                 Enemy->WakeFromDungeonPool(FTransform(FVector(300, 400, 100)), TEXT("NextRoom")));
        TestTrue(TEXT("ASC identity retained"), ASC == Enemy->GetAbilitySystemComponent());
        TestEqual(TEXT("Health base restored"), ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
        TestEqual(TEXT("Room lease changed"), Enemy->MonsterRoomId, FName(TEXT("NextRoom")));
        TestFalse(TEXT("Death reset"), Enemy->IsDead_Implementation());
        TestFalse(TEXT("Visible again"), Enemy->IsHidden());
        TestTrue(TEXT("Tick restored"), Enemy->IsActorTickEnabled());
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPoolCapacityTest, "test_25d.Roguelike.Pool.CapacityAndQuarantine",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.CapacityAndQuarantine 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPoolCapacityTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* A = World->SpawnActor<AEnemyCharacter>();
    auto* B = World->SpawnActor<AEnemyCharacter>();
    FDungeonEnemyPool Pool;
    TestFalse(TEXT("Zero capacity disables pool"), Pool.Adopt(A, TEXT("R1"), 0));
    TestTrue(TEXT("Adopt one"), Pool.Adopt(A, TEXT("R1"), 1));
    TestFalse(TEXT("Active actor consumes capacity"), Pool.Adopt(B, TEXT("R1"), 1));
    TestFalse(TEXT("Cannot acquire a live actor"), Pool.Acquire(AEnemyCharacter::StaticClass(), TEXT("R2")) != nullptr);
    TestTrue(TEXT("Return once"), Pool.Retire(A));
    TestFalse(TEXT("Duplicate return ignored"), Pool.Retire(A));
    TestFalse(TEXT("Idle actor still consumes capacity"), Pool.Adopt(B, TEXT("R1"), 1));
    TestTrue(TEXT("No reuse in same encounter"), Pool.Acquire(AEnemyCharacter::StaticClass(), TEXT("R1")) == nullptr);
    TestTrue(TEXT("No cross-class reuse"), Pool.Acquire(AActor::StaticClass(), TEXT("R2")) == nullptr);
    TestTrue(TEXT("Next room reuses exact actor"), Pool.Acquire(AEnemyCharacter::StaticClass(), TEXT("R2")) == A);
    A->Destroy();
    Pool.Prune();
    TestTrue(TEXT("Destroyed entries free capacity"), Pool.Adopt(B, TEXT("R2"), 1));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPerformanceReportTest, "test_25d.Roguelike.Pool.PerformanceReport",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.PerformanceReport 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPerformanceReportTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Director = World->SpawnActor<ADungeonRunDirector>();
    Director->Profile = NewObject<UDungeonRunProfile>(World);
    Director->Tick(.016f);
    TSharedPtr<FJsonObject> Report;
    TestTrue(TEXT("Report parses"),
             FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Director->GetRunReport()), Report));
    if (Report)
    {
        const auto Perf = Report->GetObjectField(TEXT("performance"));
        TestEqual(TEXT("Default bounded capacity"), Perf->GetIntegerField(TEXT("pool_capacity")), 12);
        TestEqual(TEXT("One periodic sample"), Perf->GetArrayField(TEXT("samples")).Num(), 1);
        TestTrue(TEXT("Reports whole-process scope explicitly"),
                 Perf->GetStringField(TEXT("memory_scope")).Contains(TEXT("whole process")));
        TestTrue(TEXT("Physical memory measured"),
                 Perf->GetNumberField(TEXT("peak_observed_process_physical_bytes")) > 0);
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPoolBlackboardTest, "test_25d.Roguelike.Pool.BlackboardLeaseIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.BlackboardLeaseIsolation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPoolBlackboardTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    auto* AI = World->SpawnActor<AMyAIController>();
    Enemy->MyAIController = AI;
    Enemy->BehaviorTree = NewObject<UBehaviorTree>(World);
    auto* Data = NewObject<UBlackboardData>(World);
    FBlackboardEntry Target;
    Target.EntryName = TEXT("TargetToFollow");
    Target.KeyType = NewObject<UBlackboardKeyType_Object>(Data);
    Data->Keys.Add(Target);
    FBlackboardEntry Distance;
    Distance.EntryName = TEXT("DistanceToTarget");
    Distance.KeyType = NewObject<UBlackboardKeyType_Float>(Data);
    Data->Keys.Add(Distance);
    Enemy->BehaviorTree->BlackboardAsset = Data;
    World->CreateAISystem();
    auto* BB = AI->GetBlackboardComponent();
    TestTrue(TEXT("Blackboard actually initializes"), BB->InitializeBlackboard(*Data));
    BB->SetValueAsObject(TEXT("TargetToFollow"), AI);
    BB->SetValueAsFloat(TEXT("DistanceToTarget"), 777.f);
    TestTrue(TEXT("Fixture retains target"), BB->GetValueAsObject(TEXT("TargetToFollow")) == AI);
    TestEqual(TEXT("Fixture retains distance"), BB->GetValueAsFloat(TEXT("DistanceToTarget")), 777.f);
    Enemy->EnableDungeonPooling();
    Enemy->SleepForDungeonPool();
    TestTrue(TEXT("Wake succeeds"), Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("NextRoom")));
    TestNull(TEXT("Previous target cleared"), BB->GetValueAsObject(TEXT("TargetToFollow")));
    TestEqual(TEXT("Previous distance cleared"), BB->GetValueAsFloat(TEXT("DistanceToTarget")), 0.f);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPoolRagdollTest, "test_25d.Roguelike.Pool.RealBlueprintRagdollRestore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Pool.RealBlueprintRagdollRestore 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPoolRagdollTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    auto* Class = LoadClass<AEnemyCharacter>(
        nullptr, TEXT("/Game/RoguelikeGeneration/Gameplay/BP_DungeonElite.BP_DungeonElite_C"));
    if (!TestNotNull(TEXT("Real common monster BP"), Class))
        return false;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<AEnemyCharacter>(Class);
    const FTransform MeshTransform = Enemy->GetMesh()->GetRelativeTransform();
    const auto CapsuleCollision = Enemy->GetCapsuleComponent()->GetCollisionEnabled();
    Enemy->EnableDungeonPooling(0);
    for (int32 Lease = 0; Lease < 2; ++Lease)
    {
        Enemy->Die(FVector(100, 0, 0));
        TestTrue(TEXT("Real mesh enters ragdoll"), Enemy->GetMesh()->IsSimulatingPhysics());
        TestFalse(TEXT("Corpse visible"), Enemy->IsHidden());
        TestTrue(TEXT("Weapon follows hand instead of detaching"),
                 Enemy->GetWeapon_Implementation()->GetAttachParent() == Enemy->GetMesh());
        TestFalse(TEXT("Weapon has no independent simulation"),
                  Enemy->GetWeapon_Implementation()->IsSimulatingPhysics());
        Enemy->SleepForDungeonPool(true);
        TestTrue(TEXT("GAS reset does not cancel settling ragdoll"), Enemy->GetMesh()->IsSimulatingPhysics());
        TestFalse(TEXT("Visible corpse cannot be leased"),
                  Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("Next")));
        TestTrue(TEXT("Zero configured settle time allows return"), Enemy->CanFinishPoolReturn());
        Enemy->SleepForDungeonPool();
        TestFalse(TEXT("Idle physics disabled"), Enemy->GetMesh()->IsSimulatingPhysics());
        TestTrue(TEXT("Wake"), Enemy->WakeFromDungeonPool(FTransform(FVector(400, 0, 100)), TEXT("Next")));
        TestTrue(TEXT("Mesh reattached to capsule"),
                 Enemy->GetMesh()->GetAttachParent() == Enemy->GetCapsuleComponent());
        TestTrue(TEXT("Mesh relative transform restored"),
                 Enemy->GetMesh()->GetRelativeTransform().Equals(MeshTransform));
        TestEqual(TEXT("Capsule collision restored"), Enemy->GetCapsuleComponent()->GetCollisionEnabled(),
                  CapsuleCollision);
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLifecyclePoolCallbackTest, "test_25d.Lifecycle.PoolOldCallback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Lifecycle.PoolOldCallback 回归，失败信息由下方断言交给自动化框架收集。 */
bool FLifecyclePoolCallbackTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<ATelegraphedEnemyCharacter>();
    auto* ASC = Enemy->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Enemy, Enemy);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Enemy));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100);
    Enemy->EnableDungeonPooling(0);
    const int32 Lease = Enemy->GetDungeonPoolLease();
    FStoneGolemAttackTuning Tuning;
    const int32 Serial = Enemy->BeginAttack(Tuning, FVector::ZeroVector, 0);
    TestTrue(TEXT("First-life attack valid"), Enemy->IsAttackCurrent(Serial, Lease));
    bool Executed = false, Accepted = false;
    FTimerHandle Handle;
    TWeakObjectPtr<ATelegraphedEnemyCharacter> WeakEnemy = Enemy;
    // External delayed work is not owned by the Actor's timer binding and survives its reset.
    World->GetTimerManager().SetTimer(Handle,
                                      FTimerDelegate::CreateLambda(
                                          [&Executed, &Accepted, WeakEnemy, Serial, Lease]()
                                          {
                                              Executed = true;
                                              if (auto* E = WeakEnemy.Get())
                                              {
                                                  Accepted = E->IsAttackCurrent(Serial, Lease);
                                                  E->FinishAttack(Serial);
                                              }
                                          }),
                                      .01f, false);
    Enemy->Die(FVector::ZeroVector);
    Enemy->SleepForDungeonPool();
    TestTrue(TEXT("Second life wakes"), Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("Second")));
    const int32 NewSerial = Enemy->BeginAttack(Tuning, FVector::ZeroVector, 0);
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        ++GFrameCounter;
        World->GetTimerManager().Tick(.1f);
    }
    TestTrue(TEXT("Old delayed callback actually executed"), Executed);
    TestFalse(TEXT("Old life rejected despite valid Actor"), Accepted);
    TestTrue(TEXT("Old completion cannot cancel new attack"),
             Enemy->IsAttackCurrent(NewSerial, Enemy->GetDungeonPoolLease()));
    World->GetTimerManager().ClearTimer(Handle);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
