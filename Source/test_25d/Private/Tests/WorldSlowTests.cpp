// 测试链：真实World/ASC -> 时间租约/注册图 -> WorldSettings与Actor倍率、恢复和来源失效。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorldTimeNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "GameFramework/WorldSettings.h"
#include "AbilitySystem/Ability/WorldSlowAbility.h"
#include "Actor/MyProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Actor/StanderWeaponProjectile.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowRemovalRestartTest, "test_25d.UnifiedGA.WorldSlow.CooldownRemovalRestart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowRemovalRestartTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UWorldSlowAbility::StaticClass(), 1));
    const auto CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Time.WorldSlow"));
    bool bCancelled = false;
    bool bRestarted = false;
    const auto Removed =
        F.ASC->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved)
            .AddLambda(
                [&](const FGameplayTag, int32 Count)
                {
                    if (Count == 0 && !bRestarted)
                    {
                        bRestarted = true;
                        F.ASC->CancelAbilityHandle(Handle);
                        TestTrue(TEXT("Cooldown removal callback restarts"), F.ASC->TryActivateAbility(Handle));
                    }
                });
    const auto Committed = F.ASC->AbilityCommittedCallbacks.AddLambda(
        [&](UGameplayAbility* Ability)
        {
            if (!bCancelled && Ability->IsA<UWorldSlowAbility>())
            {
                bCancelled = true;
                F.ASC->CancelAbilityHandle(Handle);
            }
        });
    F.ASC->TryActivateAbility(Handle);
    F.ASC->AbilityCommittedCallbacks.Remove(Committed);
    F.ASC->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved).Remove(Removed);
    TestTrue(TEXT("New activation survives outer EndAbility"), F.ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    TestTrue(TEXT("New request survives outer cleanup"), Time->HasActiveRequests());
    TestTrue(TEXT("New cooldown survives outer cleanup"), F.ASC->HasMatchingGameplayTag(CooldownTag));
    F.ASC->CancelAbilityHandle(Handle);
    TestFalse(TEXT("Final cancel releases every request"), Time->HasActiveRequests());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowRestartTest, "test_25d.UnifiedGA.WorldSlow.CommitCancelAndRestart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowRestartTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UWorldSlowAbility::StaticClass(), 1));
    bool bRestarted = false;
    const auto Callback = F.ASC->AbilityCommittedCallbacks.AddLambda(
        [&](UGameplayAbility* Ability)
        {
            if (!bRestarted && Ability->IsA<UWorldSlowAbility>())
            {
                bRestarted = true;
                F.ASC->CancelAbilityHandle(Handle);
                TestTrue(TEXT("Reentrant new activation accepted"), F.ASC->TryActivateAbility(Handle));
            }
        });
    F.ASC->TryActivateAbility(Handle);
    F.ASC->AbilityCommittedCallbacks.Remove(Callback);
    TestTrue(TEXT("New activation survives old activation unwind"),
             F.ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    TestTrue(TEXT("New activation retains its own cooldown"),
             F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Time.WorldSlow"))));
    TestTrue(TEXT("New activation owns a live request"), Time->HasActiveRequests());
    F.ASC->CancelAbilityHandle(Handle);
    TestFalse(TEXT("Cancel leaves no orphaned old graph request"), Time->HasActiveRequests());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowIdentityTest, "test_25d.UnifiedGA.WorldSlow.RequestIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowIdentityTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FAbilityWorldSlowSettings Settings;
    FString Error;
    const FGuid First = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    TestEqual(TEXT("Identical request is idempotent"), Time->Acquire(F.SourceToken, Settings, F.Source, Error), First);
    auto* Director = F.World->SpawnActor<ADungeonRunDirector>();
    Director->RunId = FGuid::NewGuid();
    FAbilitySourceToken Current;
    TestTrue(TEXT("Captures new run for same avatar"),
             AbilityExecutionIdentity::CaptureSource(F.Source, Current, Error));
    const FGuid NewLife = Time->Acquire(Current, Settings, F.Source, Error);
    TestTrue(TEXT("New run never reuses old lease"), NewLife.IsValid() && NewLife != First);
    TestFalse(TEXT("Old request pruned before reuse"), Time->IsActive(First));
    auto DifferentProfile = Settings;
    DifferentProfile.bCompensateSource = false;
    const FGuid ProfileLease = Time->Acquire(Current, DifferentProfile, F.Source, Error);
    TestTrue(TEXT("Compensation profile has distinct identity"), ProfileLease.IsValid() && ProfileLease != NewLife);
    DifferentProfile = Settings;
    Swap(DifferentProfile.BlendIn, DifferentProfile.BlendOut);
    const FGuid EnvelopeLease = Time->Acquire(Current, DifferentProfile, F.Source, Error);
    TestTrue(TEXT("Equal total duration does not merge different envelopes"),
             EnvelopeLease.IsValid() && EnvelopeLease != NewLife);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowSharedParticipantTest, "test_25d.UnifiedGA.WorldSlow.SharedParticipant",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowSharedParticipantTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FAbilitySourceToken OtherSource;
    FString Error;
    TestTrue(TEXT("Second source captured"), AbilityExecutionIdentity::CaptureSource(F.Target, OtherSource, Error));
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = 0.f;
    auto* Shared = F.World->SpawnActor<AActor>();
    Shared->CustomTimeDilation = .8f;
    const FGuid First = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    const FGuid Second = Time->Acquire(OtherSource, Settings, F.Target, Error);
    Time->RegisterCompensatedActor(Shared, F.Source);
    Time->RegisterCompensatedActor(Shared, F.Target);
    Time->SampleClockForTesting(.01, false);
    TestEqual(TEXT("Shared participant compensated once"), Shared->CustomTimeDilation, 4.f);
    Time->Release(First);
    TestEqual(TEXT("Second source retains shared compensation"), Shared->CustomTimeDilation, 4.f);
    Time->Release(Second);
    TestEqual(TEXT("Last source restores original baseline"), Shared->CustomTimeDilation, .8f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowRejectedStartupTest,
                                 "test_25d.UnifiedGA.WorldSlow.RejectedStartupHasNoCooldown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowRejectedStartupTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = 0.f;
    FString Error;
    Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    F.World->GetWorldSettings()->SetTimeDilation(.7f);
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UWorldSlowAbility::StaticClass(), 1));
    TestFalse(TEXT("External ownership conflict rejects before commit"), F.ASC->TryActivateAbility(Handle));
    TestFalse(TEXT("Rejected service request consumes no cooldown"),
              F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Time.WorldSlow"))));
    TestEqual(TEXT("External world speed retained"), F.World->GetWorldSettings()->TimeDilation, .7f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowReentrantStartupTest,
                                 "test_25d.UnifiedGA.WorldSlow.ReentrantStartupRollback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowReentrantStartupTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    const auto Callback = F.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
        {
            // 模拟提交冷却时其他游戏逻辑同步更换Avatar，图启动必须失败并精确退回冷却。
            F.ASC->InitAbilityActorInfo(F.Source, F.Target);
        });
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UWorldSlowAbility::StaticClass(), 1));
    F.ASC->TryActivateAbility(Handle);
    F.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Callback);
    TestFalse(TEXT("Invalidated startup leaves no time request"), Time->HasActiveRequests());
    TestFalse(TEXT("Failed graph startup rolls back only its cooldown"),
              F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Time.WorldSlow"))));
    TestFalse(TEXT("Failed graph startup ends ability"), F.ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowLeaseTest, "test_25d.UnifiedGA.WorldSlow.LeasesAndRestoration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowLeaseTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    if (!TestNotNull(TEXT("Per-world service exists"), Time))
    {
        return false;
    }
    F.World->GetWorldSettings()->SetTimeDilation(.5f);
    F.Source->CustomTimeDilation = .8f;
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = Settings.BlendOut = 0.f;
    Settings.Hold = 1.f;
    FString Error;
    const FGuid First = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    TestTrue(TEXT("Valid request acquires"), First.IsValid());
    TestEqual(TEXT("Preserves prior global baseline"), F.World->GetWorldSettings()->TimeDilation, .1f);
    TestEqual(TEXT("Compensates only this service factor"), F.Source->CustomTimeDilation, 4.f);
    Settings.Scale = .5f;
    const FGuid Second = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    TestEqual(TEXT("Overlaps take min, not product"), F.World->GetWorldSettings()->TimeDilation, .1f);
    Time->Release(First);
    TestEqual(TEXT("Releasing strongest retains other"), F.World->GetWorldSettings()->TimeDilation, .25f);
    Time->Release(First);
    TestTrue(TEXT("Duplicate release leaves second"), Time->IsActive(Second));
    Time->Release(Second);
    TestEqual(TEXT("Original world restored"), F.World->GetWorldSettings()->TimeDilation, .5f);
    TestEqual(TEXT("Original actor restored"), F.Source->CustomTimeDilation, .8f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowClockTest, "test_25d.UnifiedGA.WorldSlow.ClockAndSourceInvalidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowClockTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = .05f;
    Settings.Hold = 1.2f;
    Settings.BlendOut = .15f;
    FString Error;
    Time->SampleClockForTesting(10., false);
    const FGuid Lease = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    Time->SampleClockForTesting(10.05, false);
    TestTrue(*FString::Printf(TEXT("Reached hold scale within float interpolation tolerance (actual %.9f)"),
                              F.World->GetWorldSettings()->TimeDilation),
             FMath::IsNearlyEqual(F.World->GetWorldSettings()->TimeDilation, .2f, 1.e-5f));
    const double BeforePause = Time->GetGameplayRealTime();
    Time->SampleClockForTesting(100., true);
    Time->SampleClockForTesting(100.01, false);
    TestTrue(TEXT("Pause is not accumulated"), Time->GetGameplayRealTime() - BeforePause < .02);
    Time->SampleClockForTesting(101.36, false);
    TestFalse(TEXT("Expires after real gameplay duration"), Time->IsActive(Lease));
    TestEqual(TEXT("Timeout restores world"), F.World->GetWorldSettings()->TimeDilation, 1.f);
    const FGuid Rebound = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    F.ASC->InitAbilityActorInfo(F.Source, F.Target);
    Time->SampleClockForTesting(101.37, false);
    TestFalse(TEXT("Old avatar cannot retain request"), Time->IsActive(Rebound));
    Settings.Scale = 0.f;
    TestFalse(TEXT("Zero scale rejected"), Time->Acquire(F.SourceToken, Settings, F.Source, Error).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowGraphTest, "test_25d.UnifiedGA.WorldSlow.RegisteredNodeAndExternalConflict",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowGraphTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Time.WorldSlow");
    auto* Config = NewObject<UAbilityWorldSlowNodeConfig>(Graph);
    Config->Settings.BlendIn = 0;
    Node.Config = Config;
    Graph->Nodes.Add(Node);
    Graph->EntryNodeId = Node.NodeId;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Actual registered strategy starts"), Run->StartWithInput(Graph, Input, {}, Error));
    TestEqual(TEXT("Graph actually slows world"), F.World->GetWorldSettings()->TimeDilation, .2f);
    Run->Cancel();
    TestEqual(TEXT("Cancel actually restores world"), F.World->GetWorldSettings()->TimeDilation, 1.f);
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = 0;
    const FGuid Lease = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    F.World->GetWorldSettings()->SetTimeDilation(.7f);
    Time->Release(Lease);
    TestEqual(TEXT("External writer is not overwritten"), F.World->GetWorldSettings()->TimeDilation, .7f);
    TestEqual(TEXT("Owned actor compensation released on conflict"), F.Source->CustomTimeDilation, 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowAbilityTest, "test_25d.UnifiedGA.WorldSlow.StandaloneAbility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowAbilityTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(10., false);
    const auto Handle = F.ASC->GiveAbility(FGameplayAbilitySpec(UWorldSlowAbility::StaticClass(), 1));
    TestTrue(TEXT("Independent GA activates without dodge"), F.ASC->TryActivateAbility(Handle));
    Time->SampleClockForTesting(10.06, false);
    TestTrue(TEXT("Standalone GA really changes world"),
             FMath::IsNearlyEqual(F.World->GetWorldSettings()->TimeDilation, .2f));
    F.ASC->CancelAbilityHandle(Handle);
    TestFalse(TEXT("GA cancellation releases own request"), Time->HasActiveRequests());
    TestEqual(TEXT("Cancellation restores world immediately"), F.World->GetWorldSettings()->TimeDilation, 1.f);
    TestTrue(TEXT("Standalone GA committed its separate cooldown"),
             F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Time.WorldSlow"))));
    TestFalse(TEXT("Immediate repeated activation is rejected"), F.ASC->TryActivateAbility(Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSlowParticipantsTest, "test_25d.UnifiedGA.WorldSlow.ProjectilesAndDeath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldSlowParticipantsTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    FActorSpawnParameters Spawn;
    Spawn.Owner = F.Source;
    auto* Existing = F.World->SpawnActor<AMyProjectile>(FVector(900, 0, 100), FRotator::ZeroRotator, Spawn);
    auto* ExistingWeaponShot =
        F.World->SpawnActor<AStanderWeaponProjectile>(FVector(1100, 0, 100), FRotator::ZeroRotator, Spawn);
    Existing->CustomTimeDilation = .6f;
    auto* EnemyShot = F.World->SpawnActor<AMyProjectile>(FVector(-900, 0, 100), FRotator::ZeroRotator);
    EnemyShot->SetOwner(F.Target);
    FAbilityWorldSlowSettings Settings;
    Settings.BlendIn = Settings.BlendOut = 0.f;
    FString Error;
    Time->SampleClockForTesting(10., false);
    const FGuid Lease = Time->Acquire(F.SourceToken, Settings, F.Source, Error);
    TestEqual(TEXT("Existing owned projectile compensated"), Existing->CustomTimeDilation, 3.f);
    TestEqual(TEXT("Existing manually moved weapon shot compensated"), ExistingWeaponShot->CustomTimeDilation, 5.f);
    TestEqual(TEXT("Enemy projectile not compensated"), EnemyShot->CustomTimeDilation, 1.f);
    auto* NewShot = F.World->SpawnActor<AMyProjectile>(FVector(900, 50, 100), FRotator::ZeroRotator, Spawn);
    auto* NewWeaponShot =
        F.World->SpawnActor<AStanderWeaponProjectile>(FVector(1100, 50, 100), FRotator::ZeroRotator, Spawn);
    Time->SampleClockForTesting(10.01, false);
    TestEqual(TEXT("New owned projectile joins automatically"), NewShot->CustomTimeDilation, 5.f);
    TestEqual(TEXT("New manually moved weapon shot joins"), NewWeaponShot->CustomTimeDilation, 5.f);
    F.Source->GetOnDeathDelegate().Broadcast(F.Source);
    TestFalse(TEXT("Death synchronously removes request before revival"), Time->IsActive(Lease));
    TestEqual(TEXT("Original projectile speed factor restored"), Existing->CustomTimeDilation, .6f);
    TestEqual(TEXT("New projectile factor restored"), NewShot->CustomTimeDilation, 1.f);
    TestEqual(TEXT("Existing weapon projectile factor restored"), ExistingWeaponShot->CustomTimeDilation, 1.f);
    TestEqual(TEXT("New weapon projectile factor restored"), NewWeaponShot->CustomTimeDilation, 1.f);
    return true;
}
#endif
