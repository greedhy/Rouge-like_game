// 测试链：真实Dungeon玩家/ASC -> Mass预警及接触 -> 弹反拦截 -> 真实眩晕/恢复/生命清理。
// 位移测试由UWorld计算膨胀后的Delta，再交给同一Mass处理器，避免异步调度器影响断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "PerfectDodgeTestTypes.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    struct FGuardParryMassFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        FAbilityGraphTestFixture Base;
        APerfectDodgeTestDungeonPlayer* Player = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        UPureMassProcessor* Processor = nullptr;
        UPureMassMonsterType* Type = nullptr;
        UAbilityWorldTimeSubsystem* Time = nullptr;
        FActiveGameplayEffectHandle Guard;
        FAbilitySourceToken Source;

        FGuardParryMassFixture()
        {
            Base.Source->SetActorLocation(FVector(3000, 0, 0));
            Base.Target->SetActorLocation(FVector(4000, 0, 0));
            auto* Floor = Base.World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box);
            Floor->AddInstanceComponent(Box);
            Box->SetBoxExtent(FVector(2000, 2000, 10));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Floor->SetActorLocation(FVector(0, 0, -10));
            Player = Base.World->SpawnActor<APerfectDodgeTestDungeonPlayer>();
            Player->AttachState(Base.World->SpawnActor<AMyPlayerState>());
            Player->Tags.AddUnique(TEXT("Player"));
            Player->SetActorLocation(FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
            Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            ASC = Player->GetAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 0.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetBlockChanceAttribute(), 0.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetPhysicsResistanceAttribute(), 0.f);
            auto* Controller = Base.World->SpawnActor<APlayerController>();
            Base.World->AddController(Controller);
            Controller->SetPawn(Player);
            ASC->AbilityActorInfo->PlayerController = Controller;
            Time = Base.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
            Mass = Base.World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Processor = NewObject<UPureMassProcessor>(Base.World);
            Processor->CallInitialize(Base.World, Manager().AsShared());
            Type = NewObject<UPureMassMonsterType>(Base.World);
            Type->Speed = 0.f;
            Type->AttackRange = 600.f;
            Type->AttackDamage = 20.f;
            Type->AttackWindupSeconds = .1f;
            Type->AttackPeriod = 1.5f;
            Type->XPReward = 0;
            FString Error;
            AbilityExecutionIdentity::CaptureSource(Player, Source, Error);
        }

        FMassEntityManager& Manager()
        {
            return Base.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
        }

        int32 Spawn(float X = -80.f)
        {
            // SpawnMonster接受胶囊中心；地面在0，保留生产通路的HalfHeight+1落点。
            return Mass->SpawnMonster(Type, FVector(X, 0, Type->HalfHeight + 1.f));
        }

        FPureMassFragment& Fragment(int32 Id)
        {
            return Manager().GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
        }

        void Step(float WorldDelta)
        {
            auto Context = Manager().CreateExecutionContext(WorldDelta);
            Context.SetExecutionType(EMassExecutionContextType::Processor);
            Processor->CallExecute(Manager(), Context);
        }

        void OpenGuard()
        {
            FGuardParrySettings Settings;
            Settings.bEnabled = true;
            Guard = AbilityCharacterActions::OpenGuardWindow(Source, -1.f, Settings);
        }

        float Health() const
        {
            return ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryMassContactTest,
                                 "test_25d.UnifiedGA.GuardParry.MassActualContactStunAndRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryMassContactTest::RunTest(const FString&)
{
    FGuardParryMassFixture F;
    F.Time->SampleClockForTesting(90., false);
    const int32 Id = F.Spawn();
    if (!TestTrue(TEXT("Real Mass entity spawned"), Id != INDEX_NONE))
    {
        return false;
    }
    const auto Entity = F.Mass->GetEntity(Id);
    F.Step(.01f);
    TestEqual(TEXT("Attack warns before guard opens"), F.Fragment(Id).State, uint8(4));
    F.OpenGuard();
    F.Step(.11f);
    TestEqual(TEXT("Parry suppresses production incoming damage GE"), F.Health(), 100.f);
    TestEqual(TEXT("Parry applies independent one second stun"), F.Fragment(Id).StunRemaining, 1.f);
    TestEqual(TEXT("Stun never sets petrify"), F.Fragment(Id).PetrifyRemaining, 0.f);
    TestEqual(TEXT("Contact callback cannot restore attack presentation"), F.Fragment(Id).State, uint8(0));
    TestEqual(TEXT("Current follow-through cancelled"), F.Fragment(Id).AttackVisualRemaining, 0.f);
    TestFalse(TEXT("Current warning cancelled"), F.Fragment(Id).PendingAttackTarget.IsValid());
    TestTrue(TEXT("Mass parry wins the independent world slow reward"), F.Time->HasActiveRequests());
    F.Fragment(Id).AttackRemaining = 0.f;
    F.Time->SampleClockForTesting(90.2, false);
    // BeginUpdate将单帧Delta限制到.25秒；使用实际小帧累计世界时间，不能用长帧跳过眩晕。
    for (int32 Frame = 0; Frame < 10; ++Frame)
    {
        F.Step(.05f);
    }
    TestEqual(TEXT("World delta decrements independent stun"), F.Fragment(Id).StunRemaining, .5f);
    TestEqual(TEXT("Stunned monster cannot begin another warning"), F.Fragment(Id).State, uint8(0));
    for (int32 Frame = 0; Frame < 10; ++Frame)
    {
        F.Step(.05f);
    }
    F.Step(.01f);
    TestEqual(TEXT("Expiry permits a fresh warning"), F.Fragment(Id).State, uint8(4));
    F.Step(.11f);
    TestEqual(TEXT("Outside startup window original damage resumes"), F.Health(), 80.f);
    TestFalse(TEXT("Wrong entity generation cannot receive stun"),
              F.Mass->ApplyStun(Id, FMassEntityHandle(), 1.f, F.Player));
    TestTrue(TEXT("Exact live entity can receive reusable stun"), F.Mass->ApplyStun(Id, Entity, 1.f, F.Player));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, F.Guard);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryDungeonLifeTest,
                                 "test_25d.UnifiedGA.GuardParry.SamePawnDeathAndReviveClosesWindow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryDungeonLifeTest::RunTest(const FString&)
{
    FGuardParryMassFixture F;
    F.Time->SampleClockForTesting(100., false);
    F.OpenGuard();
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Base.Target, Attack, Error);
    TestTrue(TEXT("Live initial guard has startup window"), UGuardParryComponent::CanParryProjectile(F.Player, Attack));
    UGuardParryComponent::NotifyProjectileParried(F.Player, Attack, FGuid::NewGuid());
    auto* Component = F.Player->FindComponentByClass<UGuardParryComponent>();
    const auto Reward = Component->GetRewardLease();
    TestTrue(TEXT("Initial life owns successful parry reward"), F.Time->IsActive(Reward));
    F.Time->SampleClockForTesting(100.06, false);
    F.Player->Die(FVector::ZeroVector);
    TestFalse(TEXT("Downed immediately releases reward"), F.Time->IsActive(Reward));
    TestEqual(TEXT("Downed restores global time"), F.Base.World->GetWorldSettings()->TimeDilation, 1.f);
    TestEqual(TEXT("Downed restores Pawn compensation"), F.Player->CustomTimeDilation, 1.f);
    TestTrue(TEXT("Production revive reuses same Pawn"), F.Player->ReviveForDungeon(FVector(0, 0, 100)));
    TestFalse(TEXT("No tick is needed to prevent old window surviving revive"),
              UGuardParryComponent::CanParryProjectile(F.Player, Attack));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, F.Guard);
    return true;
}
#endif
