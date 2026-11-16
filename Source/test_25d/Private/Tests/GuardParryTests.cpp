// 测试链：真实GuardWindow节点/接触服务 -> 伤害前弹反 -> 眩晕GE、移动封锁与生命清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "PerfectDodgeTestTypes.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityMeleeNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    FGuardParrySettings ParrySettings()
    {
        FGuardParrySettings Settings;
        Settings.bEnabled = true;
        Settings.bRewardWorldSlow = false; // 单独验证格挡/眩晕时不改变测试世界时钟。
        return Settings;
    }

    UAbilityGraphDefinition* ParryGraph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryWindowTest, "test_25d.UnifiedGA.GuardParry.FixedWindowAndOwnedCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryWindowTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(10., false);
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    auto Legacy = AbilityCharacterActions::OpenGuardWindow(F.SourceToken);
    TestFalse(TEXT("Legacy guard opts out"), UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    auto Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, ParrySettings());
    TestTrue(TEXT("Startup begins at actual guard open"), UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    auto* Component = F.Source->FindComponentByClass<UGuardParryComponent>();
    int32 Successes = 0;
    Component->OnParried.AddLambda(
        [&](FActiveGameplayEffectHandle)
        {
            ++Successes;
        });
    const FGuid First = FGuid::NewGuid();
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, First);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, First);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    TestEqual(TEXT("Every distinct attack confirms once"), Successes, 2);
    Time->SampleClockForTesting(10.1, false);
    Component->Open(F.SourceToken, Guard, ParrySettings());
    Time->SampleClockForTesting(10.151, false);
    TestFalse(TEXT("Duplicate open and hits cannot refresh fixed window"),
              UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    TestTrue(TEXT("Ordinary guard remains after parry expiry"),
             F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, ParrySettings());
    TestTrue(TEXT("New guard owns a fresh startup"), UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    TestFalse(TEXT("Exact GE removal closes parry synchronously"),
              UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    TestTrue(TEXT("Other guard GE remains"), F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Legacy);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryGraphTest,
                                 "test_25d.UnifiedGA.GuardParry.RegisteredNodeTypedEventAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryGraphTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(20., false);
    auto* Config = NewObject<UAbilityGuardWindowNodeConfig>(F.World);
    Config->Parry = ParrySettings();
    FString Error;
    Config->Parry.Window = -1.f;
    TestFalse(TEXT("Malformed parry fails authoring validation"), Config->Validate(Error));
    Config->Parry.Window = .15f;
    FAbilitySourceToken Attack;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Parried = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Parried += Event.OutputPort == TEXT("Parried") ? 1 : 0;
        });
    TestTrue(TEXT("Registered GuardWindow starts with typed parry configuration"),
             Run->StartWithInput(ParryGraph(F.World, TEXT("Status.GuardWindow"), Config), Input, {}, Error));
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    TestEqual(TEXT("Callback defers graph publication"), Parried, 0);
    Run->Advance(.01f);
    TestEqual(TEXT("Distinct successes each publish during graph advance"), Parried, 2);
    Run->Cancel();
    TestFalse(TEXT("Graph cancellation closes exact window immediately"),
              UGuardParryComponent::CanParryProjectile(F.Source, Attack));
    Config->bCloseOnBlock = true;
    auto* CloseRun = NewObject<UAbilityGraphInstance>(F.World);
    int32 CloseEvents = 0;
    CloseRun->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            CloseEvents += Event.OutputPort == TEXT("Parried") ? 1 : 0;
        });
    TestTrue(TEXT("Close-on-block guard starts"),
             CloseRun->StartWithInput(ParryGraph(F.World, TEXT("Status.GuardWindow"), Config), Input, {}, Error));
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    CloseRun->Advance(.01f);
    TestEqual(TEXT("Close-on-block terminates after first success"), CloseEvents, 1);
    TestTrue(TEXT("Close-on-block releases window"), CloseRun->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryActorStunTest,
                                 "test_25d.UnifiedGA.GuardParry.RealContactHealthStunExpiryAndPool",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryActorStunTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(30., false);
    auto* EnemyASC = F.Target->GetAbilitySystemComponent();
    const auto Stun = FMyGameplayTags::Get().Debuff_Stun;
    EnemyASC->RegisterGameplayTagEvent(Stun, EGameplayTagEventType::NewOrRemoved)
        .AddUObject(F.Target, &AEnemyCharacter::StunTagChanged);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    auto Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, ParrySettings());
    FDamageEffectParams Damage;
    Damage.SourceAbilitySystemComponent = EnemyASC;
    Damage.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
    Damage.DamageType = FMyGameplayTags::Get().Damage_Physics;
    Damage.BaseDamage = 10.f;
    AbilityCharacterActions::ApplyTelegraphedTargets(Attack, Damage, {}, {F.Source}, {}, FGuid::NewGuid());
    TestEqual(TEXT("Actual direct adapter preserves real Health"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestFalse(TEXT("Suppressed damage never applies its Debuff"),
              F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Test.PerfectDodge.HitDebuff"))));
    TestTrue(TEXT("Real native stun tag is active"), EnemyASC->HasMatchingGameplayTag(Stun));
    TestEqual(TEXT("Production stun callback stops movement speed"), F.Target->GetCharacterMovement()->MaxWalkSpeed,
              0.f);
    TestTrue(TEXT("Stun blocks new attacks"), EnemyASC->AreAbilityTagsBlocked(FGameplayTagContainer(
                                                  FGameplayTag::RequestGameplayTag(TEXT("Abilities")))));
    TestFalse(TEXT("Stun never pauses animations as petrify"), F.Target->GetMesh()->bPauseAnims);
    for (int32 Index = 0; Index < 24; ++Index)
    {
        ++GFrameCounter;
        F.World->Tick(LEVELTICK_All, .05f);
    }
    TestFalse(TEXT("Stun expires through world GE timer"), EnemyASC->HasMatchingGameplayTag(Stun));
    TestTrue(TEXT("Expiry restores movement"), F.Target->GetCharacterMovement()->MaxWalkSpeed > 0.f);
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    AbilityCharacterActions::ApplyTelegraphedTargets(Attack, Damage, {}, {F.Source}, {}, FGuid::NewGuid());
    TestEqual(TEXT("Original damage resumes after guard closes"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    F.Target->EnableDungeonPooling(0.f);
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, ParrySettings());
    TestTrue(TEXT("Another live attack can be parried"),
             UGuardParryComponent::TryParryMelee(F.Source, Attack, FGuid::NewGuid()));
    F.Target->SleepForDungeonPool();
    TestFalse(TEXT("Pool sleep removes the stun GE"), EnemyASC->HasMatchingGameplayTag(Stun));
    TestTrue(TEXT("Same Actor wakes into next life"),
             F.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), NAME_None));
    TestFalse(TEXT("Old source cannot stun the reused enemy"),
              UGuardParryComponent::TryParryMelee(F.Source, Attack, FGuid::NewGuid()));
    TestFalse(TEXT("New life has no old stun"), EnemyASC->HasMatchingGameplayTag(Stun));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryMeleeNodeTest,
                                 "test_25d.UnifiedGA.GuardParry.MeleeNodeSuppressesBothLegacyDamagePaths",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryMeleeNodeTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(40., false);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    auto Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, ParrySettings());
    auto* Config = NewObject<UAbilityMeleeNodeConfig>(F.World);
    Config->Radius = 300.f;
    Config->bLegacyExtraDamageOnKnockback = true;
    FAbilityNodeInput Input;
    Input.SourceToken = Attack;
    Input.Origin = F.Target->GetActorLocation();
    Input.DamageParams.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Physics;
    Input.DamageParams.BaseDamage = 10.f;
    Input.DamageParams.KnockbackChance = 100.f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Real legacy-compatible contact node executes"),
             Run->StartWithInput(ParryGraph(F.World, TEXT("Melee.Contact"), Config), Input, {}, Error));
    TestEqual(TEXT("Both extra and base damage paths suppressed before GE"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestTrue(TEXT("Actual attacking source receives stun"),
             Attack.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryWorldSlowTest,
                                 "test_25d.UnifiedGA.GuardParry.WorldSlowRewardLifetimeAndLock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryWorldSlowTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(50., false);
    auto Settings = ParrySettings();
    Settings.bRewardWorldSlow = true;
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    auto Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, Settings);
    TestTrue(TEXT("Successful melee parry remains successful with reward"),
             UGuardParryComponent::TryParryMelee(F.Source, Attack, FGuid::NewGuid()));
    auto* Component = F.Source->FindComponentByClass<UGuardParryComponent>();
    const FGuid Reward = Component->GetRewardLease();
    TestTrue(TEXT("Parry owns a real independent time lease"), Time->IsActive(Reward));
    Time->SampleClockForTesting(50.06, false);
    TestEqual(TEXT("Default parry reward slows world to one fifth"), F.World->GetWorldSettings()->TimeDilation, .2f);
    TestEqual(TEXT("Defender receives inverse compensation"), F.Source->CustomTimeDilation, 5.f);
    TestEqual(TEXT("Enemy remains subject to world time"), F.Target->CustomTimeDilation, 1.f);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    TestEqual(TEXT("Later distinct success does not replace or refresh reward"), Component->GetRewardLease(), Reward);
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    TestTrue(TEXT("Guard completion/cancellation leaves earned independent reward"), Time->IsActive(Reward));
    Time->SampleClockForTesting(51.41, false);
    TestFalse(TEXT("Reward expires at original envelope end"), Time->IsActive(Reward));
    TestEqual(TEXT("Natural expiry restores world"), F.World->GetWorldSettings()->TimeDilation, 1.f);
    TestEqual(TEXT("Natural expiry restores defender baseline"), F.Source->CustomTimeDilation, 1.f);
    Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, Settings);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    TestFalse(TEXT("Another guard cannot bypass three action second reward lock"), Time->HasActiveRequests());
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    Time->SampleClockForTesting(53.01, false);
    Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, Settings);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    const auto NextReward = Component->GetRewardLease();
    TestTrue(TEXT("Fresh guard can win again after lock"), Time->IsActive(NextReward) && NextReward != Reward);
    Time->SampleClockForTesting(53.08, false);
    F.Source->GetOnDeathDelegate().Broadcast(F.Source);
    TestFalse(TEXT("Death immediately releases earned lease"), Time->IsActive(NextReward));
    TestEqual(TEXT("Death restores world without waiting for guard tick"), F.World->GetWorldSettings()->TimeDilation,
              1.f);
    TestEqual(TEXT("Death restores defender compensation"), F.Source->CustomTimeDilation, 1.f);
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryWorldSlowIsolationTest,
                                 "test_25d.UnifiedGA.GuardParry.WorldSlowNoStackAndOwnedCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryWorldSlowIsolationTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(60., false);
    FAbilitySourceToken Attack;
    FString Error;
    AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error);
    FAbilityWorldSlowSettings ExternalSettings;
    ExternalSettings.BlendIn = ExternalSettings.BlendOut = 0.f;
    ExternalSettings.Hold = 10.f;
    ExternalSettings.Scale = .4f;
    const auto External = Time->Acquire(Attack, ExternalSettings, F.Target, Error);
    auto Settings = ParrySettings();
    Settings.bRewardWorldSlow = true;
    Settings.Window = .5f;
    Settings.Reward.BlendIn = 0.f;
    auto Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, Settings);
    auto* Component = F.Source->FindComponentByClass<UGuardParryComponent>();
    int32 Successes = 0;
    Component->OnParried.AddLambda(
        [&](FActiveGameplayEffectHandle)
        {
            ++Successes;
        });
    TestTrue(TEXT("Parry still suppresses attack while another slow is active"),
             UGuardParryComponent::TryParryMelee(F.Source, Attack, FGuid::NewGuid()));
    TestFalse(TEXT("Existing world slow prevents stacking another reward"), Component->GetRewardLease().IsValid());
    Time->Release(External);
    Time->SampleClockForTesting(60.05, false);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    TestEqual(TEXT("Reward failure does not suppress distinct success events"), Successes, 2);
    TestFalse(TEXT("Same guard never retries queued reward after external lease ends"), Time->HasActiveRequests());
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    Guard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken, -1.f, Settings);
    UGuardParryComponent::NotifyProjectileParried(F.Source, Attack, FGuid::NewGuid());
    const auto Earned = Component->GetRewardLease();
    TestTrue(TEXT("Fresh guard can earn independent reward"), Time->IsActive(Earned));
    const auto Other = Time->Acquire(Attack, ExternalSettings, F.Target, Error);
    Component->Invalidate();
    TestFalse(TEXT("Life cleanup releases exactly owned reward"), Time->IsActive(Earned));
    TestTrue(TEXT("Life cleanup preserves another source lease"), Time->IsActive(Other));
    TestEqual(TEXT("Remaining external factor survives cleanup"), F.World->GetWorldSettings()->TimeDilation, .4f);
    Time->Release(Other);
    TestEqual(TEXT("External owner can restore its own world baseline"), F.World->GetWorldSettings()->TimeDilation,
              1.f);
    AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
    return true;
}
#endif
