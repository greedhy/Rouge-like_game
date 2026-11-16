// 测试链：真实Dungeon玩家/PlayerState ASC -> Mass处理器预警与AttackPlayer -> IncomingDamage GE/防御组件。
// 位移测试由UWorld计算膨胀后的Delta，再交给同一Mass处理器，避免异步调度器影响断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "PerfectDodgeTestTypes.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
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
    struct FPerfectDodgeMassFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        FAbilityGraphTestFixture Base;
        APerfectDodgeTestDungeonPlayer* Player = nullptr;
        UAbilitySystemComponent* ASC = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        UPureMassProcessor* Processor = nullptr;
        UPureMassMonsterType* Type = nullptr;
        UAbilityWorldTimeSubsystem* Time = nullptr;
        UPerfectDodgeComponent* Defense = nullptr;
        FAbilitySourceToken Source;

        FPerfectDodgeMassFixture()
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
            Defense = UPerfectDodgeComponent::FindOrCreate(Player);
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

        FGuid Roll(FString& Error)
        {
            FPerfectDodgeSettings Settings;
            Settings.Reward.BlendIn = 0.f;
            const FGuid Id = Defense->Reserve(Source, Player, Settings, FVector::RightVector, Error);
            return Defense->Begin(Id) ? Id : FGuid();
        }

        float Health() const
        {
            return ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeMassContactTest,
                                 "test_25d.UnifiedGA.PerfectDodge.MassActualAttackAndHealth",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeMassContactTest::RunTest(const FString&)
{
    FPerfectDodgeMassFixture F;
    F.Time->SampleClockForTesting(90., false);
    const int32 First = F.Spawn();
    if (!TestTrue(TEXT("Real Mass entity spawned"), First != INDEX_NONE))
    {
        return false;
    }
    F.Step(.01f);
    TestEqual(TEXT("Original processor starts warning before damage"), F.Fragment(First).State, uint8(4));
    TestEqual(TEXT("Warning leaves real player Health intact"), F.Health(), 100.f);
    F.Time->SampleClockForTesting(90.06, false);
    F.Step(.05f);
    FString Error;
    const FGuid Roll = F.Roll(Error);
    TestTrue(TEXT("Grounded dungeon pawn begins dodge after warning"), Roll.IsValid());
    F.Time->SampleClockForTesting(90.12, false);
    F.Defense->TickComponent(.06f, LEVELTICK_All, nullptr);
    F.Step(.06f);
    TestEqual(TEXT("Real Mass strike reached contact/follow-through"), F.Fragment(First).State, uint8(2));
    TestEqual(TEXT("Actual AttackPlayer interception preserves Health"), F.Health(), 100.f);
    TestTrue(TEXT("Actual grounded Mass contact awards perfect dodge"), F.Defense->WasPerfect(Roll));
    TestTrue(TEXT("Mass contact owns a real world-slow lease"), F.Time->IsActive(F.Defense->GetRewardLease()));

    // A second real entity winds up while the first is recovering. Its contact occurs after .24 action seconds.
    F.Time->SampleClockForTesting(90.21, false);
    const int32 Second = F.Spawn(-160.f);
    F.Step(.01f);
    TestEqual(TEXT("Second independent Mass strike warns"), F.Fragment(Second).State, uint8(4));
    F.Time->SampleClockForTesting(90.32, false);
    F.Defense->TickComponent(.20f, LEVELTICK_All, nullptr);
    F.Step(.11f);
    TestEqual(TEXT("Second real strike reaches contact after invulnerability"), F.Fragment(Second).State, uint8(2));
    TestEqual(TEXT("Original IncomingDamage GE reduces actual Health after window"), F.Health(), 80.f);
    TestEqual(TEXT("Original AttributeSet consumes IncomingDamage normally"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetIncomingDamageAttribute()), 0.f);
    F.Defense->Finish(Roll, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeMassCancelledWindupTest,
                                 "test_25d.UnifiedGA.PerfectDodge.MassCancelledWindupHasNoReward",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeMassCancelledWindupTest::RunTest(const FString&)
{
    FPerfectDodgeMassFixture F;
    F.Type->AttackRange = 120.f;
    F.Time->SampleClockForTesting(100., false);
    const int32 Id = F.Spawn();
    F.Step(.01f);
    TestEqual(TEXT("Real Mass attack enters warning"), F.Fragment(Id).State, uint8(4));
    FString Error;
    const FGuid Roll = F.Roll(Error);
    TestTrue(TEXT("Dodge begins during windup"), Roll.IsValid());
    F.Time->SampleClockForTesting(100.08, false);
    F.Defense->TickComponent(.08f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Actual dash exits original attack range"),
             FVector::Dist(F.Player->GetActorLocation(), F.Fragment(Id).Location) > F.Type->AttackRange);
    F.Step(.08f);
    TestFalse(TEXT("Original Mass AI clears cancelled target"), F.Fragment(Id).PendingAttackTarget.IsValid());
    TestEqual(TEXT("Original Mass AI clears cancelled windup"), F.Fragment(Id).AttackWindupRemaining, 0.f);
    F.Time->SampleClockForTesting(100.20, false);
    F.Step(.12f);
    TestEqual(TEXT("Cancelled attack never consumes player Health"), F.Health(), 100.f);
    TestFalse(TEXT("Cancelled attack cannot confirm the vacated witness"), F.Defense->WasPerfect(Roll));
    TestFalse(TEXT("Cancelled attack creates no time request"), F.Time->HasActiveRequests());
    F.Defense->Finish(Roll, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfectDodgeMassTimeTest, "test_25d.UnifiedGA.PerfectDodge.MassMotionUsesWorldDelta",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPerfectDodgeMassTimeTest::RunTest(const FString&)
{
    FPerfectDodgeMassFixture F;
    F.Player->SetActorLocation(FVector(1000, 0, F.Player->GetActorLocation().Z));
    F.Type->Speed = 100.f;
    F.Type->AttackRange = 20.f;
    const int32 Id = F.Spawn(0.f);
    auto& Fragment = F.Fragment(Id);
    Fragment.Path = {F.Player->GetActorLocation()};
    Fragment.PathGoal = F.Player->GetActorLocation();
    Fragment.NextPathTime = MAX_flt;
    const auto StepFromWorld = [&]()
    {
        F.Base.World->Tick(LEVELTICK_TimeOnly, .1f);
        F.Step(F.Base.World->GetDeltaSeconds());
    };
    const double BeforeNormal = Fragment.Location.X;
    StepFromWorld();
    const double NormalDistance = Fragment.Location.X - BeforeNormal;
    TestTrue(TEXT("Production Mass processor moves real entity normally"), NormalDistance > 9.9);
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = Settings.BlendOut = 0.f;
    Settings.Hold = 10.f;
    FString Error;
    const FGuid Slow = F.Time->Acquire(F.Source, Settings, F.Player, Error);
    TestTrue(TEXT("Real time service acquired slow request"), Slow.IsValid());
    const double BeforeSlow = Fragment.Location.X;
    StepFromWorld();
    const double SlowDistance = Fragment.Location.X - BeforeSlow;
    TestTrue(TEXT("Engine computes .02 world delta from .1 real delta at .2 scale"),
             FMath::IsNearlyEqual(F.Base.World->GetDeltaSeconds(), .02f, .0001f));
    TestTrue(TEXT("Actual Mass displacement is one fifth without an Actor compensation path"),
             FMath::IsNearlyEqual(SlowDistance, NormalDistance * .2, .01));
    F.Time->Release(Slow);
    const double BeforeRestore = Fragment.Location.X;
    StepFromWorld();
    TestTrue(TEXT("Releasing slow restores actual Mass travel"),
             FMath::IsNearlyEqual(Fragment.Location.X - BeforeRestore, NormalDistance, .01));
    return true;
}
#endif
