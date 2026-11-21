// 自动化入口 → 血液组件生命去重/世界几何 → 断言；NullRHI 不替代 Niagara 视觉验收。
#include "Misc/AutomationTest.h"
#include "Combat/BloodFXComponent.h"
#include "Combat/BloodFXGameplay.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "TimerManager.h"
#include "PureMass/PureMassCrowd.h"
#include "Character/PlayerCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodGroundTest, "test_25d.BloodFX.GroundOrientation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodGroundTest::RunTest(const FString&)
{
    for (const FVector Normal : {FVector::UpVector, FVector(0.f, .5f, 1.f).GetSafeNormal()})
    {
        const FTransform Placement = UBloodFXSubsystem::MakeGroundTransform(FVector(0, 0, 100), Normal, 73.f, 1.2f);
        TestTrue(TEXT("The authored Y-facing blood plane follows ground normal"),
                 Placement.GetUnitAxis(EAxis::Y).Equals(Normal, .001));
        TestTrue(TEXT("Surface offset follows the normal"),
                 Placement.GetLocation().Equals(FVector(0, 0, 100) + Normal * 1.5, .001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodLifeTest, "test_25d.BloodFX.LifeIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodLifeTest::RunTest(const FString&)
{
    UBloodFXComponent* Blood = NewObject<UBloodFXComponent>();
    Blood->ResetForLife(1);
    const FGuid First = FGuid::NewGuid();
    TestTrue(TEXT("First hit is visible"), Blood->AcceptHit(First, 1));
    TestFalse(TEXT("Repeated hit receipt is ignored"), Blood->AcceptHit(First, 1));
    TestTrue(TEXT("A second valid combo hit remains visible"), Blood->AcceptHit(FGuid::NewGuid(), 1));
    TestTrue(TEXT("First death is visible"), Blood->AcceptDeath(1));
    TestFalse(TEXT("Duplicated death is ignored"), Blood->AcceptDeath(1));
    Blood->ResetForLife(2);
    TestFalse(TEXT("Old hit cannot affect a reused actor"), Blood->AcceptHit(FGuid::NewGuid(), 1));
    TestFalse(TEXT("Old death cannot affect a reused actor"), Blood->AcceptDeath(1));
    TestTrue(TEXT("New life can show its hit"), Blood->AcceptHit(First, 2));
    TestTrue(TEXT("New life can die once"), Blood->AcceptDeath(2));
    Blood->ResetForLife(2);
    TestFalse(TEXT("Repeated alive notification cannot replay death"), Blood->AcceptDeath(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodStrikeTest, "test_25d.BloodFX.ActualMeleeAndPoolReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodStrikeTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture Fixture;
    auto* Blood = Fixture.Target->BloodFX.Get();
    auto* ASC = Fixture.Target->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    Fixture.Target->EnableDungeonPooling(0.f);
    FString Error;
    TestTrue(TEXT("Capture first pooled life"),
             AbilityExecutionIdentity::CaptureTarget(
                 Fixture.SourceToken, FPureMassCombatTarget::FromActor(Fixture.Target), Fixture.TargetToken, Error));
    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>();
    Settings->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
    Settings->Strike.Radius = 300.f;
    Settings->Strike.Angle = 360.f;
    Settings->Strike.Damage = 10.f;
    FDamageEffectParams Damage;
    Damage.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
    Damage.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
    const auto Strike = [&]()
    {
        AbilityCharacterActions::ExecuteStrike(Fixture.SourceToken, Damage, *Settings, FVector::ZeroVector,
                                               FVector::ForwardVector, {});
    };
    Strike();
    TestEqual(TEXT("Real shared strike removes health"),
              ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestEqual(TEXT("Real shared strike emits one blood hit"), Blood->AcceptedHits, 1);
    Settings->Strike.Damage = 0.f;
    Strike();
    TestEqual(TEXT("Zero damage adds no hit blood"), Blood->AcceptedHits, 1);
    const FBloodHitSnapshot Old =
        BloodFXGameplay::Capture(Fixture.TargetToken, FVector::ZeroVector, Fixture.Target->GetActorLocation());
    Blood->SetBleeding(true);
    TestTrue(TEXT("Visual bleeding is explicitly enabled"), Blood->IsBleeding());
    Settings->Strike.Damage = 200.f;
    Strike();
    TestEqual(TEXT("Lethal melee still emits its hit"), Blood->AcceptedHits, 2);
    TestEqual(TEXT("Death emits once"), Blood->AcceptedDeaths, 1);
    TestFalse(TEXT("Death stops bleeding"), Blood->IsBleeding());
    Fixture.Target->Die(FVector::ZeroVector);
    TestEqual(TEXT("Repeated death cannot emit again"), Blood->AcceptedDeaths, 1);
    Fixture.Target->SleepForDungeonPool();
    TestTrue(TEXT("Same actor wakes into another life"),
             Fixture.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), NAME_None));
    TestFalse(TEXT("New life starts without bleeding"), Blood->IsBleeding());
    FAbilityDamageResult OldResult;
    OldResult.ActualDamage = 10.f;
    BloodFXGameplay::Confirm(Old, OldResult, FGuid::NewGuid());
    TestEqual(TEXT("Delayed old blood cannot target reused body"), Blood->AcceptedHits, 2);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    Settings->Strike.Damage = 10.f;
    Strike();
    TestEqual(TEXT("Second life receives blood normally"), Blood->AcceptedHits, 3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodWakeOrderTest, "test_25d.BloodFX.BleedBeforeWake",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodWakeOrderTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture Fixture;
    Fixture.Target->EnableDungeonPooling(0.f);
    Fixture.Target->Die(FVector::ZeroVector);
    Fixture.Target->SleepForDungeonPool();
    auto* Blood = Fixture.Target->BloodFX.Get();
    TestTrue(TEXT("New-life event can precede the replicated owner wake"), Blood->AcceptHit(FGuid::NewGuid(), 2));
    Blood->Bleeding.Life = 2;
    Blood->Bleeding.bEnabled = true;
    Blood->OnRep_Bleeding();
    TestFalse(TEXT("New bleed waits until the owner wakes"), Blood->IsBleeding());
    TestTrue(TEXT("Owner wakes into the event's life"),
             Fixture.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), NAME_None));
    TestTrue(TEXT("Equal-life wake reconciles the pending bleeding state"), Blood->IsBleeding());
    TestTrue(TEXT("Equal-life wake starts the previously deferred timer"),
             Fixture.World->GetTimerManager().IsTimerActive(Blood->DripTimer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodMassBatchTest, "test_25d.BloodFX.MassBatchAndDedupe",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodMassBatchTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* Crowd = Fixture.World->SpawnActor<APureMassCrowd>();
    auto* Type = NewObject<UPureMassMonsterType>();
    const FGuid Attack = FGuid::NewGuid();
    for (int32 Id = 1; Id <= 12; ++Id)
    {
        Crowd->NotifyBloodHit(Id, Attack, Type, FVector::ZeroVector, FVector::ForwardVector);
        Crowd->NotifyBloodHit(Id, Attack, Type, FVector::ZeroVector, FVector::ForwardVector);
    }
    TestEqual(TEXT("A sweep beyond the project's RPC limit queues one batch of unique victims"),
              Crowd->PendingBloodHits.Num(), 12);
    Crowd->Tick(.016f);
    TestEqual(TEXT("The batch is flushed during normal world ticking"), Crowd->PendingBloodHits.Num(), 0);
    Crowd->NotifyBloodHit(1, Attack, Type, FVector::ZeroVector, FVector::ForwardVector);
    TestEqual(TEXT("Legacy extra/base damage remains deduplicated after flush"), Crowd->PendingBloodHits.Num(), 0);
    Crowd->NotifyBloodHit(1, FGuid::NewGuid(), Type, FVector::ZeroVector, FVector::ForwardVector);
    TestEqual(TEXT("A new combo stage can hit the same entity"), Crowd->PendingBloodHits.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBloodBaseDeathTest, "test_25d.BloodFX.PlayerDeathStopsReplicatedBleeding",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBloodBaseDeathTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture Fixture;
    auto* Player = Fixture.World->SpawnActor<APlayerCharacter>();
    Player->BloodFX->SetBleeding(true);
    TestTrue(TEXT("Player visual switch starts enabled"), Player->BloodFX->Bleeding.bEnabled);
    Player->ABaseCharacter::Die(FVector::ZeroVector);
    TestFalse(TEXT("Base death clears the authoritative replicated switch"), Player->BloodFX->Bleeding.bEnabled);
    return true;
}
#endif
