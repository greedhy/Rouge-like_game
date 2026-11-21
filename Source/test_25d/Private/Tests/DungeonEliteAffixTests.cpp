// 调用链：Automation RunTests → Profile 抽样、导演生成队列与 Actor 强化生命周期。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonRunProfile.h"
#include "TimerManager.h"
#include "UObject/Script.h"

namespace
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FAffixTestWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

        FAffixTestWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        }

        ~FAffixTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        // Advance the timer manager once per simulated frame, including across its frame guard.
        void AdvanceTimer(float Seconds) const
        {
            ++GFrameCounter;
            World->GetTimerManager().Tick(Seconds);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEliteAffixProfileTest, "test_25d.Roguelike.EliteAffix.Profile", TestFlags)

// 使用真实数据资产检查默认值，另用内存配置覆盖概率、权重和房间边界。
bool FDungeonEliteAffixProfileTest::RunTest(const FString&)
{
    const auto* Live = LoadObject<UDungeonRunProfile>(
        nullptr, TEXT("/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay.DA_DungeonGameplay"));
    if (!TestNotNull(TEXT("Run profile used by the dungeon"), Live))
    {
        return false;
    }
    FString Error;
    TestTrue(TEXT("Live profile is valid"), Live->ValidateEliteAffixes(Error));
    TestTrue(TEXT("Live profile contains the protective affix"), Live->EliteAffixWeights.ContainsByPredicate(
        [](const FDungeonEliteAffixWeight& Entry)
        {
            return Entry.Affix == EDungeonEliteAffix::Protective;
        }));
    TestNotEqual(TEXT("Live combat room has a first-wave elite"),
                 Live->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 1), EDungeonEliteAffix::None);

    auto* Profile = NewObject<UDungeonRunProfile>();
    TSet<EDungeonEliteAffix> Seen;
    int32 SecondWaveCount = 0;
    for (int32 Seed = 0; Seed < 256; ++Seed)
    {
        const auto First = Profile->RollEliteAffix(Seed, 2, EDungeonRoomRole::Combat, 1);
        Seen.Add(First);
        TestEqual(TEXT("Same seed, room and wave produce the same affix"),
                  Profile->RollEliteAffix(Seed, 2, EDungeonRoomRole::Combat, 1), First);
        if (Profile->RollEliteAffix(Seed, 2, EDungeonRoomRole::Combat, 2) != EDungeonEliteAffix::None)
        {
            ++SecondWaveCount;
        }
    }
    TestEqual(TEXT("Four affixes are reachable"), Seen.Num(), 4);
    TestFalse(TEXT("First wave always has an elite"), Seen.Contains(EDungeonEliteAffix::None));
    TestTrue(TEXT("Default second wave visits both outcomes"), SecondWaveCount > 0 && SecondWaveCount < 256);

    Profile->SecondWaveEliteChance = 0.f;
    TestEqual(TEXT("Zero disables second-wave elites"),
              Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 2), EDungeonEliteAffix::None);
    Profile->SecondWaveEliteChance = 1.f;
    TestNotEqual(TEXT("One guarantees the second elite"),
                 Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 2), EDungeonEliteAffix::None);
    TestEqual(TEXT("No third wave"), Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 3),
              EDungeonEliteAffix::None);
    TestEqual(TEXT("No boss affix"), Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Boss, 1),
              EDungeonEliteAffix::None);
    TestNotEqual(TEXT("Dedicated elite room rolls an affix"),
                 Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Elite, 1), EDungeonEliteAffix::None);
    Profile->EliteAffixWeights = {{EDungeonEliteAffix::Frenzied, 1}};
    TestEqual(TEXT("Single configured weight selects its affix"),
              Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 1), EDungeonEliteAffix::Frenzied);
    Profile->EliteAffixWeights.Add({EDungeonEliteAffix::Frenzied, 1});
    TestFalse(TEXT("Duplicate affix rejected"), Profile->ValidateEliteAffixes(Error));
    Profile->bEnableEliteAffixes = false;
    TestEqual(TEXT("Disabled feature does not roll"), Profile->RollEliteAffix(1, 2, EDungeonRoomRole::Combat, 1),
              EDungeonEliteAffix::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEliteAffixWaveTest, "test_25d.Roguelike.EliteAffix.WaveQueue", TestFlags)

// 直接调用导演的生产队列，确认两波精英占位、账本和 Mass 数量。
bool FDungeonEliteAffixWaveTest::RunTest(const FString&)
{
    FAffixTestWorld Fixture;
    auto* Director = Fixture.World->SpawnActor<ADungeonRunDirector>();
    Director->Generation = Fixture.World->SpawnActor<ADungeonGenerationRuntime>();
    Director->Generation->Seed = 32;
    Director->Mass = Fixture.World->GetSubsystem<UPureMassSubsystem>();
    if (!TestNotNull(TEXT("Mass subsystem used by the encounter"), Director->Mass.Get()))
    {
        return false;
    }
    auto* Profile = NewObject<UDungeonRunProfile>(Director);
    Director->Profile = Profile;
    Profile->EliteClass = AEnemyCharacter::StaticClass();
    Profile->NormalClass = AEnemyCharacter::StaticClass();
    Profile->SecondWaveEliteChance = 1.f;
    Profile->bUseMassMelee = true;
    const auto* Live = LoadObject<UDungeonRunProfile>(
        nullptr, TEXT("/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay.DA_DungeonGameplay"));
    if (!TestNotNull(TEXT("Live Mass monster type"), Live ? Live->MonsterType.Get() : nullptr))
    {
        return false;
    }
    Profile->MonsterType = Live->MonsterType;
    Director->Model.Rooms.AddDefaulted_GetRef().Role = EDungeonRoomRole::Combat;
    Director->Model.ActiveRoom = 0;
    Director->TotalNormalCount = 13;
    auto& Ledger = Director->Model.Encounter;
    if (!TestTrue(TEXT("Encounter starts"), Ledger.Start(2)))
    {
        return false;
    }
    for (int32 Wave = 1; Wave <= 2; ++Wave)
    {
        if (!TestTrue(TEXT("Queue wave using production director"), Director->SpawnWave()))
        {
            return false;
        }
        const int32 ActorCount = Wave == 1 ? 6 : 7;
        TestEqual(TEXT("Elite uses an existing Actor slot"), Director->SpawnQueue.Num(), ActorCount + 25);
        const auto* Elite = Director->SpawnQueue.FindByPredicate([](const auto& Job)
        {
            return Job.EliteAffix != EDungeonEliteAffix::None;
        });
        if (!TestNotNull(TEXT("Wave contains an elite request"), Elite))
        {
            return false;
        }
        TestTrue(TEXT("Elite is a required non-pooled Actor"), Elite->Required && !Elite->PoolEligible && !Elite->IsMass);
        TestEqual(TEXT("Combat elite has separate base health"), Elite->Health, Profile->CombatEliteHealth);
        for (const auto& Job : Director->SpawnQueue)
        {
            if (Job.Id != Elite->Id)
            {
                Ledger.ResolveTarget(Job.Id, EDungeonTargetResolution::Killed);
            }
        }
        TestFalse(TEXT("Remaining elite blocks next wave"), Ledger.CanSpawnNextWave());
        TestFalse(TEXT("Remaining elite blocks room clear"), Ledger.IsClear());
        Ledger.ResolveTarget(Elite->Id, EDungeonTargetResolution::Killed);
        Director->SpawnQueue.Reset();
    }
    TestTrue(TEXT("Room clears after both waves"), Ledger.IsClear());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEliteAffixActorTest, "test_25d.Roguelike.EliteAffix.ActorLife", TestFlags)

// 通过原生 ASC 变更生命，验证属性倍率、阈值、计时与死亡清理。
bool FDungeonEliteAffixActorTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    FAffixTestWorld Fixture;
    const auto* Live = LoadObject<UDungeonRunProfile>(
        nullptr, TEXT("/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay.DA_DungeonGameplay"));
    const auto EliteClass = Live ? Live->EliteClass.LoadSynchronous() : nullptr;
    if (!TestNotNull(TEXT("Dungeon elite Blueprint"), EliteClass))
    {
        return false;
    }
    const auto Spawn = [&Fixture, EliteClass]()
    {
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Enemy = Fixture.World->SpawnActor<AEnemyCharacter>(EliteClass, Parameters);
        auto* ASC = Enemy->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(Enemy, Enemy);
        ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(Enemy->GetAttributeSet()));
        return Enemy;
    };
    const auto ReadHealth = [](AEnemyCharacter* Enemy)
    {
        return Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    };
    const auto SetHealth = [](AEnemyCharacter* Enemy, float Value)
    {
        Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), Value);
    };

    auto* Fortified = Spawn();
    TestTrue(TEXT("Apply fortified"), Fortified->ApplyDungeonEliteAffix(EDungeonEliteAffix::Fortified, 180.f));
    TestTrue(TEXT("Fortified label is Chinese"),
             Fortified->GetDungeonEliteAffixDescription().ToString().Contains(TEXT("坚韧")));
    TestEqual(TEXT("Fortified current health"), ReadHealth(Fortified), 270.f);
    TestEqual(TEXT("Fortified maximum health"),
              Fortified->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()),
              270.f);
    Fortified->EnableDungeonPooling();
    TestEqual(TEXT("Affix life cannot enter the pool"), Fortified->GetDungeonPoolLease(), 0);
    TestFalse(TEXT("Cannot reapply to the same life"),
              Fortified->ApplyDungeonEliteAffix(EDungeonEliteAffix::Fortified, 180.f));

    auto* Frenzied = Spawn();
    TestTrue(TEXT("Apply frenzy"), Frenzied->ApplyDungeonEliteAffix(EDungeonEliteAffix::Frenzied, 100.f));
    const float BaseSpeed = Frenzied->GetCharacterMovement()->MaxWalkSpeed;
    SetHealth(Frenzied, 51.f);
    TestEqual(TEXT("Frenzy does not activate above half health"), Frenzied->GetCharacterMovement()->MaxWalkSpeed,
              BaseSpeed);
    SetHealth(Frenzied, 50.f);
    TestTrue(TEXT("Frenzy raises movement speed at half health"),
             FMath::IsNearlyEqual(Frenzied->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed * 1.35f));
    TestTrue(TEXT("Frenzy label switches to the active state"),
             Frenzied->GetDungeonEliteAffixDescription().ToString().Contains(TEXT("狂暴中")));
    Frenzied->StunTagChanged(FGameplayTag(), 1);
    TestEqual(TEXT("Stun still stops movement"), Frenzied->GetCharacterMovement()->MaxWalkSpeed, 0.f);
    Frenzied->StunTagChanged(FGameplayTag(), 0);
    TestTrue(TEXT("Frenzy remains after stun clears"),
             FMath::IsNearlyEqual(Frenzied->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed * 1.35f));

    auto* Regen = Spawn();
    TestTrue(TEXT("Apply regeneration"), Regen->ApplyDungeonEliteAffix(EDungeonEliteAffix::Regenerating, 100.f));
    Fixture.AdvanceTimer(0.f);
    SetHealth(Regen, 80.f);
    Fixture.AdvanceTimer(2.f);
    TestEqual(TEXT("No early healing"), ReadHealth(Regen), 80.f);
    SetHealth(Regen, 70.f);
    Fixture.AdvanceTimer(2.f);
    TestEqual(TEXT("Taking damage restarts the delay"), ReadHealth(Regen), 70.f);
    Fixture.AdvanceTimer(1.1f);
    TestEqual(TEXT("First healing interval restores 3% of max health"), ReadHealth(Regen), 73.f);
    Fixture.AdvanceTimer(1.f);
    TestEqual(TEXT("Restores further health on the next interval"), ReadHealth(Regen), 76.f);
    Regen->Die(FVector::ZeroVector);
    TestEqual(TEXT("Death clears the affix"), Regen->GetDungeonEliteAffix(), EDungeonEliteAffix::None);
    Fixture.AdvanceTimer(5.f);
    TestEqual(TEXT("Dead enemy does not regenerate"), ReadHealth(Regen), 76.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonEliteProtectionTest, "test_25d.Roguelike.EliteAffix.Protection", TestFlags)

// 验证当前波的守护来源和目标筛选，以及来源死亡后即刻撤销。
bool FDungeonEliteProtectionTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    FAffixTestWorld Fixture;
    const auto* Live = LoadObject<UDungeonRunProfile>(
        nullptr, TEXT("/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay.DA_DungeonGameplay"));
    const auto EliteClass = Live ? Live->EliteClass.LoadSynchronous() : nullptr;
    if (!TestNotNull(TEXT("Dungeon elite Blueprint"), EliteClass))
    {
        return false;
    }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Director = Fixture.World->SpawnActor<ADungeonRunDirector>();
    auto* Ward = Fixture.World->SpawnActor<AEnemyCharacter>(EliteClass, Parameters);
    auto* Ally = Fixture.World->SpawnActor<AEnemyCharacter>(
        EliteClass, FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator, Parameters);
    auto* ASC = Ward->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Ward, Ward);
    ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(Ward->GetAttributeSet()));
    TestTrue(TEXT("Apply protective affix"), Ward->ApplyDungeonEliteAffix(EDungeonEliteAffix::Protective, 180.f));
    TestTrue(TEXT("Protective label is Chinese"),
             Ward->GetDungeonEliteAffixDescription().ToString().Contains(TEXT("守护光环")));
    Ward->MonsterRoomId = Ally->MonsterRoomId = TEXT("R01");
    Director->Phase = EDungeonRunPhase::Encounter;
    Director->ProtectiveElite = Ward;
    TestEqual(TEXT("Unregistered Actor is not protected"), Director->GetEliteProtectionMultiplier(Ally), 1.f);
    Director->ActorTargets.Add(Ally, TEXT("Ally"));
    TestEqual(TEXT("Nearby member takes 75% damage"), Director->GetEliteProtectionMultiplier(Ally), 0.75f);
    TestEqual(TEXT("Aura does not protect its source"), Director->GetEliteProtectionMultiplier(Ward), 1.f);
    Ally->MonsterRoomId = TEXT("R02");
    TestEqual(TEXT("Protective aura cannot cross room boundaries"), Director->GetEliteProtectionMultiplier(Ally), 1.f);
    Ally->MonsterRoomId = TEXT("R01");
    Ally->SetActorLocation(FVector(601.f, 0.f, 0.f));
    TestEqual(TEXT("Outside radius the full damage applies"), Director->GetEliteProtectionMultiplier(Ally), 1.f);
    Ally->SetActorLocation(FVector(500.f, 0.f, 0.f));
    Ward->Die(FVector::ZeroVector);
    TestEqual(TEXT("Death removes protection immediately"), Director->GetEliteProtectionMultiplier(Ally), 1.f);
    TestTrue(TEXT("Death clears label"), Ward->GetDungeonEliteAffixDescription().IsEmpty());
    return true;
}
#endif
