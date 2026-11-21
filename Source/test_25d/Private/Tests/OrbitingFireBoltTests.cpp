/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Abilities.OrbitingFireBolt.CapacityAndCleanup
 * - test_25d.Abilities.OrbitingFireBolt.SequenceDamageAndTargetDeath
 * - test_25d.Abilities.OrbitingFireBolt.LegacyPassiveSaveMigratesOff
 * - test_25d.Abilities.OrbitingFireBolt.ConfigurationAndTargetLoss
 * - test_25d.Abilities.OrbitingFireBolt.CommittedAttackSignal
 * - test_25d.Abilities.OrbitingFireBolt.ProjectileMovementHitsTarget
 * - test_25d.Abilities.OrbitingFireBolt.LearnEquipReplaceReequip
 * - test_25d.Abilities.OrbitingFireBolt.EquipmentMetadataReplicates
 * - test_25d.Abilities.OrbitingFireBolt.ToggleInputAndMana
 * - test_25d.Abilities.OrbitingFireBolt.ActiveSlotSaveLoadsOff
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "AbilitySystem/Ability/MyFireBolt.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Character/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "test_25d/test_25d.h"
#include "Game/MyGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace OrbitingFireBoltTests
{
    struct FWorld
    {
        UWorld* World;
        ABaseCharacter* Player;
        UMyAbilitySystemComponent* ASC;
        FGameplayAbilitySpecHandle Handle;
        FEditorScriptExecutionGuard Guard;
        explicit FWorld(bool bUseRealPlayerInterface = false)
        {
            World = UWorld::CreateWorld(
                EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("OrbitTest")));
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;
            URL.AddOption(TEXT("game=/Script/test_25d.MyGameModeBase"));
            World->SetGameMode(URL);
            auto* Mode = CastChecked<AMyGameModeBase>(World->GetAuthGameMode());
            Mode->CharacterClassInfo = LoadObject<UCharacterClassInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_CharacterClassInfo.DA_CharacterClassInfo"));
            Mode->AbilityInfo = LoadObject<UAbilityInfo>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"));
            if (bUseRealPlayerInterface)
            {
                // 学习/装备服务器入口现要求真实玩家接口和可用技能点，不能用 Enemy 夹具绕过权限。
                auto* State = World->SpawnActor<AMyPlayerState>();
                State->Profession = EPlayerProfession::Mage;
                State->SetSpellPoints(20);
                auto* Pawn = World->SpawnActor<ADungeonPlayerRunTestPawn>();
                Pawn->AttachState(State);
                Player = Pawn;
                Player->Tags.AddUnique(TEXT("Player"));
                auto* Component = Player->GetAbilitySystemComponent();
                Component->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
                Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
                Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            }
            else
            {
                Player = Spawn(FVector::ZeroVector, TEXT("Player"));
            }
            ASC = CastChecked<UMyAbilitySystemComponent>(Player->GetAbilitySystemComponent());
            // Supply the source pawn expected by the project's damage-number pipeline,
            // without invoking the enemy class's AI-only PossessedBy implementation.
            auto* Controller = World->SpawnActor<APlayerController>();
            Controller->SetPawn(Player);
            ASC->AbilityActorInfo->PlayerController = Controller;
        }
        ~FWorld()
        {
            ASC->CancelAllAbilities();
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        AEnemyCharacter* Spawn(FVector Location, FName Team)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Actor = World->SpawnActor<AEnemyCharacter>(Location, FRotator::ZeroRotator, Params);
            Actor->Tags.Reset();
            Actor->Tags.Add(Team);
            auto* Component = Actor->GetAbilitySystemComponent();
            Component->InitAbilityActorInfo(Actor, Actor);
            Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            return Actor;
        }
        UClass* AbilityClass()
        {
            return LoadClass<UMyOrbitingFireBoltAbility>(
                nullptr,
                TEXT(
                    "/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C"));
        }
        UMyOrbitingFireBoltAbility* Start()
        {
            UClass* Class = AbilityClass();
            if (!Class)
                return nullptr;
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(Class, 1));
            if (!ASC->TryActivateAbility(Handle))
                return nullptr;
            return Cast<UMyOrbitingFireBoltAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        }
        void Advance(float Seconds, bool bSimulateProjectiles = false)
        {
            // Deliberately skip character BeginPlay/AI; exercise the real timers in small steps.
            for (float T = 0; T < Seconds; T += .05f)
            {
                ++GFrameCounter;
                World->GetTimerManager().Tick(FMath::Min(.05f, Seconds - T));
                for (TActorIterator<AMyOrbitingFireBolt> It(World); It; ++It)
                {
                    if (!It->HasActorBegunPlay())
                        It->DispatchBeginPlay();
                    It->Tick(.05f);
                    if (bSimulateProjectiles && IsValid(*It) && It->ProjectileMovement->IsActive())
                        It->ProjectileMovement->TickComponent(.05f, LEVELTICK_All,
                                                              &It->ProjectileMovement->PrimaryComponentTick);
                }
            }
        }
        void Attack(AActor* Target)
        {
            auto* Class = LoadClass<UMyFireBolt>(
                nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"));
            // The event receiver is tested independently of input/montage in this fixture.
            ASC->OffensiveAttackTargeted.Broadcast(Class ? Class->GetDefaultObject<UGameplayAbility>() : nullptr,
                                                   Target);
        }
        TArray<AMyOrbitingFireBolt*> Balls()
        {
            TArray<AMyOrbitingFireBolt*> Result;
            for (TActorIterator<AMyOrbitingFireBolt> It(World); It; ++It)
                if (IsValid(*It))
                    Result.Add(*It);
            return Result;
        }
    };
} // namespace OrbitingFireBoltTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitCapacity, "test_25d.Abilities.OrbitingFireBolt.CapacityAndCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.CapacityAndCleanup 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitCapacity::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    auto* Ability = F.Start();
    if (!TestNotNull(TEXT("Configured passive activates"), Ability))
        return false;
    TestEqual(TEXT("No immediate spawn"), Ability->GetLiveFireBoltCount(), 0);
    TestTrue(TEXT("Timer belongs to fixture world"), Ability->GetWorld() == F.World);
    F.Advance(2.9f);
    TestEqual(TEXT("Wait full interval"), Ability->GetLiveFireBoltCount(), 0);
    F.Advance(.2f);
    TestEqual(TEXT("One after three seconds"), Ability->GetLiveFireBoltCount(), 1);
    F.Advance(12.f);
    TestEqual(TEXT("Capacity includes all live balls"), Ability->GetLiveFireBoltCount(), 3);
    F.ASC->DeactivatePassiveAbility.Broadcast(FMyGameplayTags::Get().Abilities_Passive_OrbitingFireBolt);
    TestFalse(TEXT("Replacing passive ends it"), F.ASC->FindAbilitySpecFromHandle(F.Handle)->IsActive());
    TestEqual(TEXT("All actors cleaned up"), F.Balls().Num(), 0);
    F.Advance(6.f);
    TestEqual(TEXT("Timers stay cleared"), F.Balls().Num(), 0);
    TestTrue(TEXT("Can activate again"), F.ASC->TryActivateAbility(F.Handle));
    F.Advance(3.1f);
    TestEqual(TEXT("Fresh refill timer"), Ability->GetLiveFireBoltCount(), 1);
    F.Player->GetOnDeathDelegate().Broadcast(F.Player);
    TestEqual(TEXT("Owner death cleans up"), F.Balls().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitTargeting, "test_25d.Abilities.OrbitingFireBolt.SequenceDamageAndTargetDeath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.SequenceDamageAndTargetDeath 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitTargeting::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    auto* Ability = F.Start();
    if (!TestNotNull(TEXT("Passive activates"), Ability))
        return false;
    auto* Enemy = F.Spawn(FVector(800, 0, 0), TEXT("Enemy"));
    auto* Friend = F.Spawn(FVector(500, 0, 0), TEXT("Player"));
    F.Advance(9.2f);
    F.Attack(Friend);
    TestNull(TEXT("Friendly target rejected"), Ability->GetAttackTarget());
    F.Attack(Enemy);
    F.Advance(.05f);
    TestEqual(TEXT("First ball launches alone"), Ability->GetLaunchedFireBoltCount(), 1);
    TestEqual(TEXT("Flight still counts toward cap"), Ability->GetLiveFireBoltCount(), 3);
    F.Advance(.4f);
    TestEqual(TEXT("Second ball launches later"), Ability->GetLaunchedFireBoltCount(), 2);
    AMyOrbitingFireBolt* HitBall = nullptr;
    for (auto* Ball : F.Balls())
        if (Ball->IsLaunched())
        {
            HitBall = Ball;
            break;
        }
    if (!TestNotNull(TEXT("Launched actor exists"), HitBall))
        return false;
    const float Before = Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    HitBall->OnSphereOverlap(HitBall->Sphere, Enemy, nullptr, 0, false, FHitResult());
    TestTrue(TEXT("Shared GE_Damage applies real fire damage"),
             Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) < Before);
    TestEqual(TEXT("Spent projectile disappears"), Ability->GetLiveFireBoltCount(), 2);
    FindFProperty<FBoolProperty>(ABaseCharacter::StaticClass(), TEXT("bDead"))
        ->SetPropertyValue_InContainer(Enemy, true);
    Enemy->GetOnDeathDelegate().Broadcast(Enemy);
    TestNull(TEXT("Dead target released"), Ability->GetAttackTarget());
    TestEqual(TEXT("Remaining fireballs return to orbit"), Ability->GetLaunchedFireBoltCount(), 0);
    F.Advance(3.1f);
    TestEqual(TEXT("Refills to cap after attack"), Ability->GetLiveFireBoltCount(), 3);
    F.Attack(Enemy);
    TestNull(TEXT("Dead targets cannot restart attack"), Ability->GetAttackTarget());
    auto* AnotherEnemy = F.Spawn(FVector(500, 100, 0), TEXT("Enemy"));
    F.ASC->OffensiveAttackTargeted.Broadcast(Ability, AnotherEnemy);
    TestNull(TEXT("Passive cannot trigger itself"), Ability->GetAttackTarget());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitSavedPassive, "test_25d.Abilities.OrbitingFireBolt.LegacyPassiveSaveMigratesOff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.LegacyPassiveSaveMigratesOff 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitSavedPassive::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    UClass* Class = F.AbilityClass();
    if (!TestNotNull(TEXT("Authored GA exists"), Class))
        return false;
    const auto& Tags = FMyGameplayTags::Get();
    auto* Save = NewObject<ULoadScreenSaveGame>();
    FSavedAbility Record;
    Record.GameplayAbility = Class;
    Record.AbilityTag = Tags.Abilities_Passive_OrbitingFireBolt;
    Record.AbilityStatus = Tags.Abilities_Status_Equipped;
    Record.AbilityType = Tags.Abilities_Type_Passive;
    Record.AbilitySlot = Tags.InputTag_Passive_1;
    Record.AbilityLevel = 1;
    Save->SavedAbilities.Add(Record);
    F.ASC->AddCharacterAbilitiesFromSaveData(Save);
    auto* Spec = F.ASC->GetSpecFromAbilityTag(Record.AbilityTag);
    if (!TestNotNull(TEXT("Saved spec restored"), Spec))
        return false;
    const auto Handle = Spec->Handle;
    TestFalse(TEXT("Legacy equipped passive loads switched off"), Spec->IsActive());
    TestFalse(TEXT("Legacy passive slot released"), F.ASC->AbilityHasAnySlot(*Spec));
    TestEqual(TEXT("Learning preserved"), F.ASC->GetStatusFromSpec(*Spec), Tags.Abilities_Status_Unlocked);
    TestEqual(TEXT("Load does not spend mana"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    TestNotNull(TEXT("Ending does not remove learned ability"), F.ASC->FindAbilitySpecFromHandle(Handle));
    F.ASC->ServerEquipAbility(Record.AbilityTag, Tags.InputTag_1);
    TestFalse(TEXT("New active slot equips off"), Spec->IsActive());
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestTrue(TEXT("Migrated ability can toggle on"), Spec->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitConfiguration, "test_25d.Abilities.OrbitingFireBolt.ConfigurationAndTargetLoss",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.ConfigurationAndTargetLoss 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitConfiguration::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    auto* Ability = F.Start();
    if (!TestNotNull(TEXT("Passive activates"), Ability))
        return false;
    Ability->Config = DuplicateObject<UOrbitingFireBoltConfig>(Ability->Config, Ability);
    Ability->Config->MaxFireBolts = 2;
    F.Advance(12.1f);
    TestEqual(TEXT("Active cast retains its immutable configuration"), Ability->GetLiveFireBoltCount(), 3);
    F.ASC->CancelAbilityHandle(F.Handle);
    TestTrue(TEXT("Updated configuration is accepted by the next activation"), F.ASC->TryActivateAbility(F.Handle));
    F.Advance(6.1f);
    TestEqual(TEXT("Next cast honors updated capacity"), Ability->GetLiveFireBoltCount(), 2);
    auto* Far = F.Spawn(FVector(5000, 0, 0), TEXT("Enemy"));
    F.Attack(Far);
    TestNull(TEXT("Out-of-range target rejected"), Ability->GetAttackTarget());
    auto* Enemy = F.Spawn(FVector(800, 0, 0), TEXT("Enemy"));
    F.Attack(Enemy);
    F.Advance(.1f);
    TestEqual(TEXT("Configured passive launches"), Ability->GetLaunchedFireBoltCount(), 1);
    Enemy->Destroy();
    F.Advance(.4f);
    TestNull(TEXT("Destroyed target released without death broadcast"), Ability->GetAttackTarget());
    TestEqual(TEXT("In-flight bolt returns after target removal"), Ability->GetLaunchedFireBoltCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitAttackSignal, "test_25d.Abilities.OrbitingFireBolt.CommittedAttackSignal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.CommittedAttackSignal 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitAttackSignal::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    // Native FireBolt defaults to LocalPredicted; this fixture has no network owner.
    // Temporarily run its native activation on the server, leaving the asset unchanged.
    auto* Policy = FindFProperty<FByteProperty>(UGameplayAbility::StaticClass(), TEXT("NetExecutionPolicy"));
    TGuardValue<uint8> AuthorityPolicy(*Policy->ContainerPtrToValuePtr<uint8>(GetMutableDefault<UMyFireBolt>()),
                                       static_cast<uint8>(EGameplayAbilityNetExecutionPolicy::ServerOnly));
    auto* Passive = F.Start();
    if (!TestNotNull(TEXT("Passive activates"), Passive))
        return false;
    auto* Enemy = F.Spawn(FVector(400, 0, 0), TEXT("Enemy"));
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UMyFireBolt::StaticClass(), 1));
    if (!TestTrue(TEXT("Native offensive ability starts"), F.ASC->TryActivateAbility(Handle)))
        return false;
    auto Instances = F.ASC->FindAbilitySpecFromHandle(Handle)->GetAbilityInstances();
    if (!TestTrue(TEXT("Offensive instance exists"), Instances.Num() > 0))
        return false;
    auto* Attack = CastChecked<UMyFireBolt>(Instances[0]);
    Attack->SetOffensiveAttackTarget(Enemy);
    Attack->ReportOffensiveAttack(Enemy);
    TestNull(TEXT("Target data alone does not trigger passive"), Passive->GetAttackTarget());
    TestTrue(TEXT("Offensive commit succeeds"),
             Attack->CommitAbility(Handle, F.ASC->AbilityActorInfo.Get(), Attack->GetCurrentActivationInfo()));
    TestEqual(TEXT("Successful commit reports selected target"), Passive->GetAttackTarget(),
              static_cast<AActor*>(Enemy));
    F.ASC->CancelAbilityHandle(Handle);
    Enemy->GetOnDeathDelegate().Broadcast(Enemy);
    TestNull(TEXT("Target cleared"), Passive->GetAttackTarget());
    F.ASC->NotifyOffensiveAttack(Passive, Enemy);
    TestNull(TEXT("Passive reports rejected by ASC"), Passive->GetAttackTarget());
    if (!TestTrue(TEXT("New offensive activation starts"), F.ASC->TryActivateAbility(Handle)))
        return false;
    Instances = F.ASC->FindAbilitySpecFromHandle(Handle)->GetAbilityInstances();
    Attack = CastChecked<UMyFireBolt>(Instances[0]);
    Attack->CommitAbility(Handle, F.ASC->AbilityActorInfo.Get(), Attack->GetCurrentActivationInfo());
    TestNull(TEXT("New activation has no stale target"), Passive->GetAttackTarget());
    Attack->SetOffensiveAttackTarget(Enemy);
    TestEqual(TEXT("Commit-before-target order also works"), Passive->GetAttackTarget(), static_cast<AActor*>(Enemy));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitFlight, "test_25d.Abilities.OrbitingFireBolt.ProjectileMovementHitsTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.ProjectileMovementHitsTarget 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitFlight::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    auto* Ability = F.Start();
    if (!TestNotNull(TEXT("Passive activates"), Ability))
        return false;
    auto* Enemy = F.Spawn(FVector(600, 0, 0), TEXT("Enemy"));
    // Swept movement intentionally ignores actors that have not finished initialization.
    // Initialize this target without running its AI/BeginPlay in the temporary world.
    Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
    if (!Enemy->IsActorInitialized())
        Enemy->PostInitializeComponents();
    Enemy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Enemy->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
    Enemy->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    F.Advance(3.1f);
    // PrimitiveComponent suppresses overlap delegates until the world's play flag is set.
    // All needed actors are now initialized; leave unrelated AI BeginPlay out of this test.
    F.World->SetBegunPlay(true);
    F.Attack(Enemy);
    F.Advance(1.5f, true);
    for (auto* Ball : F.Balls())
    {
        AddInfo(FString::Printf(
            TEXT(
                "Flight state: pos=%s velocity=%s launched=%d active=%d simulate=%d updated=%s collision=%d overlaps=%d targetInit=%d targetPos=%s"),
            *Ball->GetActorLocation().ToString(), *Ball->ProjectileMovement->Velocity.ToString(), Ball->IsLaunched(),
            Ball->ProjectileMovement->IsActive(), Ball->ProjectileMovement->bSimulationEnabled,
            *GetNameSafe(Ball->ProjectileMovement->UpdatedComponent),
            static_cast<int32>(Ball->Sphere->GetCollisionEnabled()), Ball->Sphere->GetGenerateOverlapEvents(),
            Enemy->IsActorInitialized(), *Enemy->GetActorLocation().ToString()));
    }
    TestTrue(TEXT("Actual projectile sweep damages target"),
             Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) < 1000.f);
    TestEqual(TEXT("Actual impact consumes bolt"), Ability->GetLiveFireBoltCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitEquipFlow, "test_25d.Abilities.OrbitingFireBolt.LearnEquipReplaceReequip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.LearnEquipReplaceReequip 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitEquipFlow::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F(true);
    const auto& Tags = FMyGameplayTags::Get();
    F.ASC->UpdateAbilityStatuses(2);
    auto* Spec = F.ASC->GetSpecFromAbilityTag(Tags.Abilities_Passive_OrbitingFireBolt);
    if (!TestNotNull(TEXT("New learnable catalog entry is discovered"), Spec))
        return false;
    TestEqual(TEXT("New skill starts eligible"), F.ASC->GetStatusFromSpec(*Spec), Tags.Abilities_Status_Eligible);
    F.ASC->ServerSpendSpellPoint(Tags.Abilities_Passive_OrbitingFireBolt);
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_Passive_1);
    TestFalse(TEXT("Toggle cannot occupy passive slot"), F.ASC->AbilityHasAnySlot(*Spec));
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_1);
    TestFalse(TEXT("Equip defaults off"), Spec->IsActive());
    F.Advance(3.1f);
    TestEqual(TEXT("Equipping alone spawns nothing"), F.Balls().Num(), 0);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestTrue(TEXT("Key enables equipped ability"), Spec->IsActive());
    F.Advance(3.1f);
    TestEqual(TEXT("Enabled toggle spawns"), F.Balls().Num(), 1);
    F.ASC->ServerSpendSpellPoint(Tags.Abilities_Fire_FireBolt);
    F.ASC->ServerEquipAbility(Tags.Abilities_Fire_FireBolt, Tags.InputTag_1);
    TestFalse(TEXT("Replacement ends previous toggle"), Spec->IsActive());
    TestEqual(TEXT("Replacement clears actors"), F.Balls().Num(), 0);
    TestEqual(TEXT("Displaced skill remains learned"), F.ASC->GetStatusFromSpec(*Spec), Tags.Abilities_Status_Unlocked);
    TestFalse(TEXT("Displaced skill loses its slot"), F.ASC->AbilityHasAnySlot(*Spec));
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_1);
    TestFalse(TEXT("Re-equip defaults off again"), Spec->IsActive());
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    F.Advance(3.1f);
    TestEqual(TEXT("Re-enabled toggle resumes refill"), F.Balls().Num(), 1);
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_2);
    TestFalse(TEXT("Moving slots switches off"), Spec->IsActive());
    TestEqual(TEXT("Moving slots cleans actors"), F.Balls().Num(), 0);
    F.ASC->ServerToggleAbility(Spec->Handle, Tags.InputTag_1);
    TestFalse(TEXT("Stale input for old slot cannot activate"), Spec->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitMetadataReplication,
                                 "test_25d.Abilities.OrbitingFireBolt.EquipmentMetadataReplicates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.EquipmentMetadataReplicates 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitMetadataReplication::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F(true);
    const auto& Tags = FMyGameplayTags::Get();
    F.ASC->UpdateAbilityStatuses(2);
    auto* Spec = F.ASC->GetSpecFromAbilityTag(Tags.Abilities_Passive_OrbitingFireBolt);
    if (!TestNotNull(TEXT("Catalog grants the toggle"), Spec))
        return false;
    TestEqual(TEXT("Exercise ServerOnly replication policy"), Spec->Ability->GetNetExecutionPolicy(),
              EGameplayAbilityNetExecutionPolicy::ServerOnly);
    int32 Before = Spec->ReplicationKey;
    F.ASC->ServerSpendSpellPoint(Tags.Abilities_Passive_OrbitingFireBolt);
    TestTrue(TEXT("Learning schedules the changed spec for replication"), Spec->ReplicationKey > Before);
    Before = Spec->ReplicationKey;
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_1);
    TestTrue(TEXT("Equipped input slot reaches the owning client"), Spec->ReplicationKey > Before);
    Before = Spec->ReplicationKey;
    F.ASC->ServerSpendSpellPoint(Tags.Abilities_Passive_OrbitingFireBolt);
    TestTrue(TEXT("Upgraded level reaches the owning client"), Spec->ReplicationKey > Before);
    Before = Spec->ReplicationKey;
    F.ASC->ServerEquipAbility(Tags.Abilities_Passive_OrbitingFireBolt, Tags.InputTag_2);
    TestTrue(TEXT("Moving an inactive toggle replicates its new slot"), Spec->ReplicationKey > Before);
    F.ASC->ServerSpendSpellPoint(Tags.Abilities_Fire_FireBolt);
    Before = Spec->ReplicationKey;
    F.ASC->ServerEquipAbility(Tags.Abilities_Fire_FireBolt, Tags.InputTag_2);
    TestTrue(TEXT("Displaced toggle's removed slot is replicated"), Spec->ReplicationKey > Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitToggleMana, "test_25d.Abilities.OrbitingFireBolt.ToggleInputAndMana",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.ToggleInputAndMana 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitToggleMana::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    const auto& Tags = FMyGameplayTags::Get();
    FGameplayAbilitySpec Grant(F.AbilityClass(), 1);
    Grant.DynamicAbilityTags.AddTag(Tags.InputTag_1);
    Grant.DynamicAbilityTags.AddTag(Tags.Abilities_Status_Equipped);
    const auto Handle = F.ASC->GiveAbility(Grant);
    auto* Spec = F.ASC->FindAbilitySpecFromHandle(Handle);
    auto Mana = [&]()
    {
        return F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
    };
    F.ASC->AbilityInputTagHeld(Tags.InputTag_1);
    TestFalse(TEXT("Held alone cannot switch on"), Spec->IsActive());
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestTrue(TEXT("Press switches on"), Spec->IsActive());
    TestEqual(TEXT("Enable pays exactly 10 MP immediately"), Mana(), 90.f);
    for (int32 I = 0; I < 20; ++I)
        F.ASC->AbilityInputTagHeld(Tags.InputTag_1);
    F.ASC->AbilityInputTagReleased(Tags.InputTag_1);
    TestTrue(TEXT("Hold and release keep toggle on"), Spec->IsActive());
    TestEqual(TEXT("Holding never repeats startup cost"), Mana(), 90.f);
    F.Advance(.9f);
    TestEqual(TEXT("No upkeep before first second"), Mana(), 90.f);
    F.Advance(.2f);
    TestEqual(TEXT("First second costs 1 MP"), Mana(), 89.f);
    F.Advance(2.f);
    TestEqual(TEXT("Three seconds cost 3 MP"), Mana(), 87.f);
    TestEqual(TEXT("Spawn while enabled"), F.Balls().Num(), 1);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestFalse(TEXT("Second press switches off"), Spec->IsActive());
    TestEqual(TEXT("Switch off clears fireballs"), F.Balls().Num(), 0);
    for (int32 I = 0; I < 20; ++I)
        F.ASC->AbilityInputTagHeld(Tags.InputTag_1);
    F.Advance(4.f);
    TestFalse(TEXT("Holding after off cannot restart"), Spec->IsActive());
    TestEqual(TEXT("No off-state cost"), Mana(), 87.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 9.f);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestFalse(TEXT("Less than 10 MP refuses enable"), Spec->IsActive());
    TestEqual(TEXT("Rejected enable costs nothing"), Mana(), 9.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 13.f);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestEqual(TEXT("Re-enable charges startup cost again"), Mana(), 3.f);
    F.Advance(3.1f);
    TestEqual(TEXT("Last full upkeep payment reaches zero"), Mana(), 0.f);
    F.Advance(1.f);
    TestFalse(TEXT("Cannot pay next second: automatically off"), Spec->IsActive());
    TestEqual(TEXT("Mana never goes negative"), Mana(), 0.f);
    TestEqual(TEXT("Resource exhaustion clears actors"), F.Balls().Num(), 0);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 20.f);
    F.ASC->AbilityInputTagHeld(Tags.InputTag_1);
    F.Advance(2.f);
    TestFalse(TEXT("Refilling mana while held does not auto-enable"), Spec->IsActive());
    TestEqual(TEXT("All upkeep timers stopped"), Mana(), 20.f);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 0.f);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_1);
    TestFalse(TEXT("Manual switch off is free even at zero mana"), Spec->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitActiveSave, "test_25d.Abilities.OrbitingFireBolt.ActiveSlotSaveLoadsOff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.OrbitingFireBolt.ActiveSlotSaveLoadsOff 回归，失败信息由下方断言交给自动化框架收集。 */
bool FOrbitActiveSave::RunTest(const FString&)
{
    using namespace OrbitingFireBoltTests;
    FWorld F;
    const auto& Tags = FMyGameplayTags::Get();
    auto* Save = NewObject<ULoadScreenSaveGame>();
    FSavedAbility Record;
    Record.GameplayAbility = F.AbilityClass();
    Record.AbilityTag = Tags.Abilities_Passive_OrbitingFireBolt;
    Record.AbilityStatus = Tags.Abilities_Status_Equipped;
    Record.AbilityType = Tags.Abilities_Type_Offensive;
    Record.AbilitySlot = Tags.InputTag_2;
    Record.AbilityLevel = 3;
    Save->SavedAbilities.Add(Record);
    F.ASC->AddCharacterAbilitiesFromSaveData(Save);
    auto* Spec = F.ASC->GetSpecFromAbilityTag(Record.AbilityTag);
    if (!TestNotNull(TEXT("Active slot saved ability restored"), Spec))
        return false;
    TestFalse(TEXT("Loaded toggle is off"), Spec->IsActive());
    TestEqual(TEXT("Level preserved"), Spec->Level, 3);
    TestEqual(TEXT("Active input slot preserved"), F.ASC->GetInputTagFromSpec(*Spec), Tags.InputTag_2);
    F.Advance(4.f);
    TestEqual(TEXT("Load is free and generates no fireballs"), F.Balls().Num(), 0);
    TestEqual(TEXT("No load mana drain"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    F.ASC->AbilityInputTagPressed(Tags.InputTag_2);
    TestTrue(TEXT("Loaded slot press activates"), Spec->IsActive());
    return true;
}
namespace OrbitingFireBoltFragmentTests
{
    /** 原GA只作真实配置/启动费对照；随后两条图以同样输入独立检查库存、维持与物理接触。 */
    UAbilityGraphDefinition* Definition(OrbitingFireBoltTests::FWorld& F, const UOrbitingFireBoltConfig& Settings,
                                        bool bFine)
    {
        if (bFine)
        {
            return AbilityOrbitFragments::Build(F.World, Settings);
        }
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
        Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Channel.OrbitingFireBolts");
        auto* Config = NewObject<UAbilityOrbitNodeConfig>(Graph);
        Config->OrbitSettings = DuplicateObject<UOrbitingFireBoltConfig>(&Settings, Config);
        Node.Config = Config;
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Add(Node);
        return Graph;
    }

    /** 真实计时器和Actor运动；每帧只排空同一图，不以手工Overlap冒充真实命中。 */
    void Step(OrbitingFireBoltTests::FWorld& F, UAbilityGraphInstance& Run, float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += .05f)
        {
            F.Advance(FMath::Min(.05f, Seconds - Elapsed), true);
            Run.Advance(0.f);
        }
    }

    bool Exercise(FAutomationTestBase& Test, bool bFine, bool bReplacePayload)
    {
        OrbitingFireBoltTests::FWorld F;
        auto* Ability = F.Start();
        if (!Test.TestNotNull(TEXT("Actual catalog Orbit frontend activates through ASC"), Ability))
        {
            return false;
        }
        auto Params = Ability->MakeDamageEffectParamsFromClassDefaults();
        auto* Settings = DuplicateObject<UOrbitingFireBoltConfig>(Ability->Config, F.World);
        Test.TestEqual(TEXT("Real frontend preserves its one startup payment"),
                       F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        F.ASC->CancelAbilityHandle(F.Handle);
        Test.TestEqual(TEXT("Cancelling configuration probe leaves no old inventory"), F.Balls().Num(), 0);
        auto* Graph = Definition(F, *Settings, bFine);
        if (!Test.TestNotNull(TEXT("Fine or original inventory definition exists"), Graph))
        {
            return false;
        }
        if (bFine)
        {
            Test.TestFalse(TEXT("Fine default contains no whole Orbit channel macro"),
                           Graph->Nodes.ContainsByPredicate(
                               [](const auto& Node)
                               {
                                   return Node.TypeId == TEXT("Channel.OrbitingFireBolts");
                               }));
            Test.TestTrue(TEXT("Inventory, actual delivery and effect are independently addressable"),
                          Graph->Nodes.Num() >= 3);
        }
        if (bReplacePayload)
        {
            FAbilityRuntimeRecipeContribution Rune;
            Rune.SourceId = TEXT("Rune.OrbitPetrifyPayload");
            FAbilityRuntimeRecipePatch Patch;
            Patch.TargetTypeId = TEXT("Effect.InputDamage");
            Patch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
            Patch.ReplacementTypeId = TEXT("Status.Petrify");
            auto* Status = NewObject<UAbilityPetrifyNodeConfig>(F.World);
            Status->DurationSeconds = 2.f;
            Patch.ReplacementConfig = Status;
            Rune.Patches.Add(Patch);
            UAbilityGraphDefinition* Compiled = nullptr;
            FString Error;
            if (!Test.TestTrue(TEXT("Runtime recipe replaces only the native contact payload: ") + Error,
                               AbilityRuntimeRecipes::Compile(F.World, Graph, {Rune}, Compiled, Error)))
            {
                Test.AddInfo(Error);
                return false;
            }
            Graph = Compiled;
        }
        auto* Binding = NewObject<UAbilityOrbitContext>(F.World);
        Binding->SourceAbility = Ability;
        Binding->AbilityTags = Ability->GetAssetTags();
        FAbilityNodeInput Input;
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(F.Player, Input.SourceToken, Error))
        {
            Test.AddError(Error);
            return false;
        }
        Input.Origin = F.Player->GetActorLocation();
        Input.Context = Binding;
        Input.DamageParams = Params;
        Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        Input.DamageParams.DebuffChance = 0.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.BudgetPolicy = Graph->BudgetPolicy;
        Options.CompatibilityProfile = Graph->CompatibilityProfile;
        if (!Test.TestTrue(TEXT("Original or fine inventory starts through registered strategies"),
                           Run->StartWithInput(Graph, Input, Options, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        auto* Enemy = F.Spawn(FVector(300, 0, 0), TEXT("Enemy"));
        Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
        if (!Enemy->IsActorInitialized())
        {
            Enemy->PostInitializeComponents();
        }
        Enemy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Enemy->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
        Enemy->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* TargetASC = Enemy->GetAbilitySystemComponent();
        Step(F, *Run, 9.2f);
        Test.TestEqual(TEXT("Real inventory replenishes to the authored capacity"), F.Balls().Num(),
                       Settings->MaxFireBolts);
        Test.TestEqual(TEXT("Upkeep is independent of the effect and preserves nine actual Mana payments"),
                       F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 81.f);
        int32 Contacts = 0;
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                Contacts += Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>() ? 1 : 0;
            });
        F.World->SetBegunPlay(true);
        ON_SCOPE_EXIT
        {
            F.World->SetBegunPlay(false);
        };
        F.Attack(Enemy);
        const float Before = TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        // 首箭发射后立即停止本次攻击信号以外的时间，观察真正物理命中而不伪造回执。
        for (int32 Frame = 0; Frame < 7 && Contacts == 0 && !Enemy->GetMesh()->bPauseAnims; ++Frame)
        {
            Step(F, *Run, .05f);
        }
        if (bReplacePayload)
        {
            Test.TestTrue(TEXT("Actual native flight applies replacement petrify on real contact"),
                          Enemy->GetMesh()->bPauseAnims);
            Test.TestEqual(TEXT("Replacing the sink removes all hidden native damage"),
                           TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), Before);
            Test.TestEqual(TEXT("Removed damage sink never invokes the original GE"), Contacts, 0);
        }
        else
        {
            Test.TestEqual(TEXT("One genuine native collision applies one actual GE"), Contacts, 1);
            Test.TestEqual(TEXT("Actual Health decreases once"),
                           TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), Before - 10.f);
        }
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
        Run->Cancel();
        Test.TestEqual(TEXT("Cancellation destroys this inventory and every in-flight original Actor"), F.Balls().Num(),
                       0);
        const float Mana = F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        Step(F, *Run, 3.2f);
        Test.TestEqual(TEXT("Cancelled inventory leaves no timer resource cost"),
                       F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), Mana);
        Test.TestEqual(TEXT("Cancelled inventory does not replenish later"), F.Balls().Num(), 0);
        return true;
    }
} // namespace OrbitingFireBoltFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeOrbitFineDefault,
                                 "test_25d.DynamicMage.NativeOrbit.FineDefaultPreservesInventoryUpkeepAndActualFlight",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeOrbitFineDefault::RunTest(const FString&)
{
    const bool bControl = OrbitingFireBoltFragmentTests::Exercise(*this, false, false);
    const bool bFine = OrbitingFireBoltFragmentTests::Exercise(*this, true, false);
    return bControl && bFine;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeOrbitAlternatePayload,
                                 "test_25d.DynamicMage.NativeOrbit.RealAttackCanReplaceDamageWithStatus",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeOrbitAlternatePayload::RunTest(const FString&)
{
    return OrbitingFireBoltFragmentTests::Exercise(*this, true, true);
}
#endif
