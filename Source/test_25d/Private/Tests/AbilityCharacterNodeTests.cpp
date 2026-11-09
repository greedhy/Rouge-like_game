// 调用链：Automation真实World/ASC/Mass -> 注册角色节点和共享动作 -> 实际血量/格挡句柄/地面筛选。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityTimelineTestAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Actor/StanderWeaponProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    UAbilityGraphDefinition* CharacterGraph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Add(Node);
        return Graph;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterStrikeProfiles,
                                 "test_25d.UnifiedGA.CharacterActions.StrikeProfilesAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterStrikeProfiles::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Front = Mass->SpawnMonster(Type, FVector(120, 0, 0));
    const int32 Rear = Mass->SpawnMonster(Type, FVector(-120, 0, 0));
    auto* Config = NewObject<UAbilityCharacterStrikeNodeConfig>(F.World);
    Config->Strike.Radius = 180.f;
    Config->Strike.Angle = 90.f;
    Config->Strike.Damage = 25.f;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Direction = FVector::ForwardVector;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
    FString Error;
    Config->Profile = EAbilityCharacterStrikeProfile::SwordShieldActor;
    auto* ActorOnly = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(
        TEXT("SwordShield profile starts"),
        ActorOnly->StartWithInput(CharacterGraph(F.World, TEXT("Combat.CharacterStrike"), Config), Input, {}, Error));
    FPureMassView View;
    TestTrue(TEXT("Front exists"), Mass->Resolve(Front, View));
    TestEqual(TEXT("SwordShield preserves Actor-only scope"), View.Health, 100.f);
    Config->Profile = EAbilityCharacterStrikeProfile::StanderMixed;
    auto* Mixed = NewObject<UAbilityGraphInstance>(F.World);
    int32 Hits = 0;
    Mixed->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("DamageResolved"))
            {
                ++Hits;
                TestTrue(TEXT("Graph publishes real Mass damage"), Event.Input.ActualDamage.IsSet());
            }
        });
    TestTrue(TEXT("Stander mixed profile starts"),
             Mixed->StartWithInput(CharacterGraph(F.World, TEXT("Combat.CharacterStrike"), Config), Input, {}, Error));
    Mass->Resolve(Front, View);
    TestEqual(TEXT("Front receives one original strike"), View.Health, 75.f);
    Mass->Resolve(Rear, View);
    TestEqual(TEXT("Rear excluded by locked facing"), View.Health, 100.f);
    TestEqual(TEXT("Exactly one confirmed strike result"), Hits, 1);
    Config->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
    Config->Strike.Angle = 360.f;
    Config->bRadialImpulse = true;
    bool bCurrent = true;
    int32 Applied = 0;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [&]()
    {
        return bCurrent;
    };
    Callbacks.AfterDamage = [&](const FAbilityTargetToken&, const FVector&, const FAbilityDamageResult&)
    {
        ++Applied;
        bCurrent = false;
        return true;
    };
    AbilityCharacterActions::ExecuteStrike(F.SourceToken, Input.DamageParams, *Config, FVector::ZeroVector,
                                           FVector::ForwardVector, Callbacks);
    TestEqual(TEXT("Native host cancellation closes shared damage loop immediately"), Applied, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterGuardWindow, "test_25d.UnifiedGA.CharacterActions.GuardOwnsOnlyItsEffect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterGuardWindow::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto OtherGuard = AbilityCharacterActions::OpenGuardWindow(F.SourceToken);
    TestTrue(TEXT("Existing native guard opens real tag"),
             F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    auto* Config = NewObject<UAbilityGuardWindowNodeConfig>(F.World);
    Config->Duration = .5f;
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Composable guard opens"),
             Run->StartWithInput(CharacterGraph(F.World, TEXT("Status.GuardWindow"), Config), Input, {}, Error));
    AbilityCharacterActions::CloseGuardWindow(F.ASC, OtherGuard);
    TestTrue(TEXT("Graph owns separate guard handle"), F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    Run->Advance(.25f);
    TestTrue(TEXT("Guard remains until configured time"), F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    Run->Advance(.25f);
    TestTrue(TEXT("Guard graph completes"), Run->IsFinished());
    TestFalse(TEXT("Timed graph releases own effect"), F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    auto* Cancel = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Second guard starts"),
             Cancel->StartWithInput(CharacterGraph(F.World, TEXT("Status.GuardWindow"), Config), Input, {}, Error));
    Cancel->Cancel();
    TestFalse(TEXT("Cancel releases guard immediately"), F.ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTelegraphedContactAction,
                                 "test_25d.UnifiedGA.CharacterActions.TelegraphedUsesLockedSurface",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTelegraphedContactAction::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* FloorActor = F.World->SpawnActor<AActor>();
    auto* Floor = NewObject<UBoxComponent>(FloorActor);
    FloorActor->SetRootComponent(Floor);
    FloorActor->AddInstanceComponent(Floor);
    Floor->SetBoxExtent(FVector(600, 600, 10));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Floor->SetCollisionObjectType(ECC_WorldStatic);
    Floor->SetCollisionResponseToAllChannels(ECR_Block);
    Floor->RegisterComponent();
    FloorActor->SetActorLocation(FVector(0, 0, -10));
    F.Target->SetActorLocation(FVector(150, 0, F.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    auto* Config = NewObject<UAbilityTelegraphedContactNodeConfig>(F.World);
    Config->Shape.Radius = 250.f;
    Config->LockedSurface = {FVector(0, -250, 0), FVector(250, -250, 0), FVector(250, 250, 0), FVector(0, 250, 0)};
    Config->LockedTriangles = {0, 1, 2, 0, 2, 3};
    TArray<AActor*> Targets;
    AbilityCharacterActions::GatherTelegraphedTargets(F.SourceToken, Config->Shape, FVector::ZeroVector, 0.f,
                                                      Config->LockedSurface, Config->LockedTriangles, nullptr, Targets);
    TestTrue(TEXT("Visible target on actual warning surface accepted"), Targets.Contains(F.Target));
    Config->LockedSurface = {FVector(-250, -250, 0), FVector(0, -250, 0), FVector(0, 250, 0), FVector(-250, 250, 0)};
    AbilityCharacterActions::GatherTelegraphedTargets(F.SourceToken, Config->Shape, FVector::ZeroVector, 0.f,
                                                      Config->LockedSurface, Config->LockedTriangles, nullptr, Targets);
    TestFalse(TEXT("Inside radius but outside locked warning rejected"), Targets.Contains(F.Target));
    Config->LockedTriangles.Add(50);
    FString Error;
    TestFalse(TEXT("Malformed triangle snapshot rejected"), Config->Validate(Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTelegraphedReportLifeIsolation,
                                 "test_25d.UnifiedGA.CharacterActions.ReportCannotRetargetNewLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTelegraphedReportLifeIsolation::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Target->Tags.AddUnique(TEXT("DungeonEncounter"));
    F.Target->EnableDungeonPooling(0.f);
    const int32 OriginalLease = F.Target->GetDungeonPoolLease();
    FDamageEffectParams Damage;
    Damage.BaseDamage = 25.f;
    Damage.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
    FStoneGolemAttackTuning Shape;
    int32 Reports = 0;
    int32 DamageResults = 0;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.BeforeActorDamage = [&](AActor* Target)
    {
        ++Reports;
        F.Target->SleepForDungeonPool(false);
        TestTrue(TEXT("Report observer can recycle target synchronously"),
                 F.Target->WakeFromDungeonPool(FTransform(FVector(150, 0, 0)), NAME_None));
    };
    Callbacks.AfterDamage = [&](const FAbilityTargetToken&, const FVector&, const FAbilityDamageResult&)
    {
        ++DamageResults;
        return true;
    };
    AbilityCharacterActions::ApplyTelegraphedTargets(F.SourceToken, Damage, Shape, {F.Target}, Callbacks);
    TestEqual(TEXT("Native offensive report remains once per batch"), Reports, 1);
    TestTrue(TEXT("Observer produced a distinct target life"), F.Target->GetDungeonPoolLease() != OriginalLease);
    TestEqual(TEXT("Captured old hit cannot affect recycled target"), DamageResults, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterWeaponProjectileAction,
                                 "test_25d.UnifiedGA.CharacterActions.WeaponProjectileDamageAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCharacterWeaponProjectileAction::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    F.Source->SetActorLocation(FVector(-2000, 0, 0));
    auto* Player = F.World->SpawnActor<AAbilityTimelineTestStander>();
    Player->AttachState(F.World->SpawnActor<AMyPlayerState>());
    Player->Tags.AddUnique(TEXT("Player"));
    Player->SetActorLocation(FVector::ZeroVector);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Target = Mass->SpawnMonster(Type, FVector(250, 0, 0));
    auto* Config = NewObject<UAbilityWeaponProjectileNodeConfig>(F.World);
    Config->Event.Kind = EStanderWeaponActionKind::Projectile;
    Config->Event.Radius = 20.f;
    Config->Event.ProjectileSpeed = 1000.f;
    Config->Event.ProjectileLifetime = 2.f;
    Config->Event.Damage = 25.f;
    FAbilityNodeInput Input;
    Input.Origin = Player->GetActorLocation();
    Input.Direction = FVector::ForwardVector;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
    FString Error;
    if (!TestTrue(TEXT("Weapon source captures real PlayerState ASC"),
                  AbilityExecutionIdentity::CaptureSource(Player, Input.SourceToken, Error)))
    {
        return false;
    }
    const auto SpawnNative = [&]()
    {
        return AbilityCharacterActions::SpawnWeaponProjectile(Input.SourceToken, Input.DamageParams, Config->Event,
                                                              Input.Origin, Input.Direction, INDEX_NONE);
    };
    const auto FindGraphShot = [&](AStanderWeaponProjectile* Excluded = nullptr)
    {
        for (TActorIterator<AStanderWeaponProjectile> It(F.World); It; ++It)
        {
            if (*It != Excluded && !It->IsActorBeingDestroyed() && It->GetOwner() == Player)
            {
                return *It;
            }
        }
        return static_cast<AStanderWeaponProjectile*>(nullptr);
    };
    auto* Native = SpawnNative();
    if (!TestNotNull(TEXT("Shared native action spawns original weapon projectile"), Native))
    {
        return false;
    }
    Native->Tick(.4f);
    FPureMassView View;
    TestTrue(TEXT("Native projectile target remains alive"), Mass->Resolve(Target, View));
    TestEqual(TEXT("Native projectile applies configured damage through real sweep"), View.Health, 75.f);
    TestTrue(TEXT("Native projectile is consumed by impact"), Native->IsActorBeingDestroyed());
    auto* Definition = CharacterGraph(F.World, TEXT("Delivery.WeaponProjectile"), Config);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Spawned = 0;
    int32 Completed = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("Spawned"))
            {
                ++Spawned;
            }
            if (Event.OutputPort == TEXT("Completed"))
            {
                ++Completed;
            }
        });
    TestTrue(TEXT("Registered weapon delivery starts"), Run->StartWithInput(Definition, Input, {}, Error));
    auto* GraphShot = FindGraphShot();
    if (!TestNotNull(TEXT("Graph creates original weapon projectile actor"), GraphShot))
    {
        return false;
    }
    TestTrue(TEXT("Projectile retains actor replication"), GraphShot->GetIsReplicated());
    TestTrue(TEXT("Projectile retains movement replication"), GraphShot->IsReplicatingMovement());
    TestEqual(TEXT("Original configured flight lifetime retained"), GraphShot->GetLifeSpan(), 2.f);
    TestEqual(TEXT("Registered node publishes one spawn"), Spawned, 1);
    GraphShot->Tick(.4f);
    Run->Advance(0.f);
    TestTrue(TEXT("Graph projectile target remains alive"), Mass->Resolve(Target, View));
    TestEqual(TEXT("Graph applies the same single damage as native action"), View.Health, 50.f);
    TestEqual(TEXT("Impact publishes one completion"), Completed, 1);
    TestTrue(TEXT("Impact completes graph lifetime"), Run->IsFinished());
    TestEqual(TEXT("Impact completes successfully"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);

    auto* Unrelated = SpawnNative();
    auto* Cancelled = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Cancellable weapon delivery starts"), Cancelled->StartWithInput(Definition, Input, {}, Error));
    auto* CancelShot = FindGraphShot(Unrelated);
    if (!TestNotNull(TEXT("Cancellation fixture has its own projectile"), CancelShot) ||
        !TestNotNull(TEXT("Unrelated native projectile remains distinct"), Unrelated))
    {
        return false;
    }
    Cancelled->Cancel();
    TestTrue(TEXT("Cancellation destroys exactly the graph projectile"), CancelShot->IsActorBeingDestroyed());
    TestFalse(TEXT("Other native flight survives graph cancellation"), Unrelated->IsActorBeingDestroyed());
    TestTrue(TEXT("Cancelled graph has closed its execution"), Cancelled->IsFinished());
    TestEqual(TEXT("Cancellation remains cancellation"), Cancelled->GetEndReason(), EAbilityGraphEndReason::Cancelled);
    Mass->Resolve(Target, View);
    TestEqual(TEXT("Cancellation adds no impact damage"), View.Health, 50.f);
    Unrelated->Tick(.4f);
    Mass->Resolve(Target, View);
    TestEqual(TEXT("Unrelated flight still applies exactly one damage"), View.Health, 25.f);

    auto* StaleRun = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Source lifecycle delivery starts"), StaleRun->StartWithInput(Definition, Input, {}, Error));
    auto* StaleShot = FindGraphShot();
    if (!TestNotNull(TEXT("Lifecycle fixture owns a projectile"), StaleShot))
    {
        return false;
    }
    Player->GetAbilitySystemComponent()->InitAbilityActorInfo(Player->GetPlayerState(), F.Source);
    StaleShot->Tick(.4f);
    StaleRun->Advance(0.f);
    TestTrue(TEXT("Rebound source retires old projectile before sweep"), StaleShot->IsActorBeingDestroyed());
    Mass->Resolve(Target, View);
    TestEqual(TEXT("Retired source cannot add damage"), View.Health, 25.f);
    TestTrue(TEXT("Source invalidation closes graph lifetime"), StaleRun->IsFinished());
    return true;
}
#endif
