/** Automation -> extracted driver -> replicated host; verifies graph ownership does not double tick behavior. */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
#include "AbilitySystem/Unified/LegacyMixedAbilityDriver.h"
#include "AbilitySystem/Unified/LegacyExtensionAbilityDriver.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "MyGameplayTags.h"
#include "EngineUtils.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "UObject/Script.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "GameplayEffect.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyDetachedDriver,
                                 "test_25d.UnifiedGA.LegacyDrivers.DetachedStopsWithoutHost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A detached graph driver cannot dereference a collected or torn-down presentation actor. */
bool FAbilityLegacyDetachedDriver::RunTest(const FString& Parameters)
{
    FActorAbilityDriver Actor(nullptr);
    FMixedAbilityDriver Mixed(nullptr);
    FExtensionAbilityDriver Extension(nullptr);
    Actor.Advance(1.f);
    Mixed.Advance(1.f);
    Extension.Advance(1.f);
    Actor.Finish(true);
    Mixed.Finish(true);
    Extension.Finish(true);
    TestTrue(TEXT("Detached Actor is terminal"), Actor.IsFinished());
    TestTrue(TEXT("Detached Mixed is terminal"), Mixed.IsFinished());
    TestTrue(TEXT("Detached Extension is terminal"), Extension.IsFinished());
    return true;
}
namespace AbilityLegacyDriverTests
{
    /** Minimal server world; no GA activation or AI BeginPlay is needed for scheduling assertions. */
    struct FFixture
    {
        UWorld* World = nullptr;
        AEnemyCharacter* Caster = nullptr;
        FEditorScriptExecutionGuard Guard;

        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Caster = Character(FVector(-500.f, 0.f, 0.f), TEXT("Player"));
        }

        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        AEnemyCharacter* Character(FVector Position, FName Team)
        {
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Actor = World->SpawnActor<AEnemyCharacter>(Position, FRotator::ZeroRotator, Parameters);
            Actor->Tags.Reset();
            Actor->Tags.Add(Team);
            auto* ASC = Actor->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Actor, Actor);
            ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
            return Actor;
        }

        UPrototypeCastContext* Context()
        {
            auto* Result = NewObject<UPrototypeCastContext>();
            Result->Caster = Caster;
            Result->DamageParams.SourceAbilitySystemComponent = Caster->GetAbilitySystemComponent();
            return Result;
        }

        UAbilityPrototype* Prototype(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UAbilityPrototype>();
            Result->Kind = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Cooldown = 0.f;
            Result->Duration = 10.f;
            Result->Period = .125f;
            Result->Damage = 0.f;
            return Result;
        }

        /** Fixture worlds do not automatically dispatch BeginPlay. */
        void Begin(AActor* Host)
        {
            if (Host && !Host->HasActorBegunPlay())
            {
                Host->DispatchBeginPlay();
            }
        }
    };
} // namespace AbilityLegacyDriverTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyActorClock,
                                 "test_25d.UnifiedGA.LegacyDrivers.ActorFieldExternalClockAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Actor Field consumes every due period, and presentation Tick cannot duplicate graph-owned advancement. */
bool FAbilityLegacyActorClock::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Config = Fixture.Prototype(EAbilityPrototypeKind::Field);
    Config->OnPeriod = Fixture.Prototype(EAbilityPrototypeKind::Burst);
    auto* Presentation = Fixture.Prototype(EAbilityPrototypeKind::Field);
    Presentation->Period = 4.f;
    int32 Admissions = 0;
    auto* Host = APrototypeRuntime::SpawnDriven(
        Fixture.Context(), Config, FVector::ZeroVector, FVector::ForwardVector, nullptr, 0, false,
        [&](const FLegacyAbilityChildRequest& Request)
        {
            TestEqual(TEXT("Period port is explicit"), Request.Event, FName(TEXT("OnPeriod")));
            TestTrue(TEXT("Child comes from the execution snapshot"), Request.Config == Config->OnPeriod);
            TestEqual(TEXT("Child depth retained"), Request.Depth, 1);
            ++Admissions;
            return true;
        },
        Presentation);
    if (!TestNotNull(TEXT("Host"), Host))
    {
        return false;
    }
    Fixture.Begin(Host);
    TestTrue(TEXT("Only presentation reference is replicated"), Host->Prototype == Presentation);
    Host->Tick(2.25f);
    TestEqual(TEXT("Presentation does not advance authority clock"), Admissions, 0);
    Host->GetDriver()->Advance(2.25f);
    TestEqual(TEXT("Actor catches all 18 periods using immutable execution data"), Admissions, 18);
    Host->CancelCast();
    Host->GetDriver()->Advance(2.f);
    TestEqual(TEXT("Cancellation prevents later children"), Admissions, 18);
    TestTrue(TEXT("Driver observes terminal host"), Host->GetDriver()->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyMixedClock,
                                 "test_25d.UnifiedGA.LegacyDrivers.MixedFieldRetainsEightPulseBacklog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Mixed Field retains its unconsumed backlog and caps each advancement at eight periods. */
bool FAbilityLegacyMixedClock::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Config = Fixture.Prototype(EAbilityPrototypeKind::Field);
    Config->OnPeriod = Fixture.Prototype(EAbilityPrototypeKind::Burst);
    int32 Admissions = 0;
    auto* Host = APureMassRuntime::SpawnDriven(
        Fixture.Context(), Config, FVector::ZeroVector, FVector::ForwardVector, {}, 0, false,
        [&](const FLegacyAbilityChildRequest& Request)
        {
            TestEqual(TEXT("Period port"), Request.Event, FName(TEXT("OnPeriod")));
            ++Admissions;
            return true;
        });
    if (!TestNotNull(TEXT("Host"), Host))
    {
        return false;
    }
    Fixture.Begin(Host);
    Host->Tick(2.25f);
    TestEqual(TEXT("Presentation does not consume backlog"), Admissions, 0);
    Host->GetDriver()->Advance(2.25f);
    TestEqual(TEXT("First eight"), Admissions, 8);
    Host->GetDriver()->Advance(0.f);
    TestEqual(TEXT("Next eight without new elapsed time"), Admissions, 16);
    Host->GetDriver()->Advance(0.f);
    TestEqual(TEXT("Remaining two retained"), Admissions, 18);
    Host->CancelCast();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyPelletAdmission,
                                 "test_25d.UnifiedGA.LegacyDrivers.MultishotAdmitsIndependentPellets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A fan admits same-depth pellets through its callback instead of spawning an autonomous runtime tree. */
bool FAbilityLegacyPelletAdmission::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Config = Fixture.Prototype(EAbilityPrototypeKind::Multishot);
    Config->Count = 3;
    Config->SpreadDegrees = 60.f;
    int32 Admissions = 0;
    TArray<FVector> Directions;
    auto* Context = Fixture.Context();
    auto* Host =
        APrototypeRuntime::SpawnDriven(Context, Config, FVector::ZeroVector, FVector::ForwardVector, nullptr, 0, false,
                                       [&](const FLegacyAbilityChildRequest& Request)
                                       {
                                           TestEqual(TEXT("Pellet event"), Request.Event, FName(TEXT("Multishot")));
                                           TestEqual(TEXT("Pellet keeps parent depth"), Request.Depth, 0);
                                           TestTrue(TEXT("Pellet disables fan expansion"), Request.bSingle);
                                           Directions.Add(Request.Direction);
                                           ++Admissions;
                                           return true;
                                       });
    if (!TestNotNull(TEXT("Host"), Host))
    {
        return false;
    }
    Fixture.Begin(Host);
    TestEqual(TEXT("Three independent requests"), Admissions, 3);
    TestEqual(TEXT("Driver itself did not spawn a runtime subtree"), Context->Spawned, 1);
    TestTrue(TEXT("Container closes immediately"), Host->GetDriver()->IsFinished());
    Host->GetDriver()->Start();
    TestEqual(TEXT("Repeated Start is idempotent"), Admissions, 3);
    if (Directions.Num() == 3)
    {
        TestTrue(TEXT("Fan is symmetric"), FMath::IsNearlyEqual(Directions[0].Y, -Directions[2].Y));
        TestTrue(TEXT("Center ray preserves facing"), Directions[1].Equals(FVector::ForwardVector));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyExtensionClock,
                                 "test_25d.UnifiedGA.LegacyDrivers.ExtensionSixteenPulseClamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Extension's sixteen-period catch-up and one-period clamp remain distinct from both original backends. */
bool FAbilityLegacyExtensionClock::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Enemy = Fixture.Character(FVector::ZeroVector, TEXT("Enemy"));
    auto* Context = NewObject<UExtensionCastContext>();
    Context->Caster = Fixture.Caster;
    Context->DamageParams.SourceAbilitySystemComponent = Fixture.Caster->GetAbilitySystemComponent();
    auto* Config = NewObject<UExtensionNode>();
    Config->Behavior = EExtensionKind::Attach;
    Config->Duration = 10.f;
    Config->Period = .125f;
    Config->PeriodChild = NewObject<UExtensionNode>();
    int32 Admissions = 0;
    auto* Host = AExtensionRuntime::SpawnDriven(Context, Config, FVector::ZeroVector, FVector::ForwardVector,
                                                FPureMassCombatTarget::FromActor(Enemy), 0, FVector::ZeroVector, false,
                                                [&](const FLegacyAbilityChildRequest& Request)
                                                {
                                                    TestEqual(TEXT("Attach period port"), Request.Event,
                                                              FName(TEXT("OnPeriod")));
                                                    ++Admissions;
                                                    return true;
                                                });
    if (!TestNotNull(TEXT("Host"), Host))
    {
        return false;
    }
    Fixture.Begin(Host);
    Host->Tick(2.25f);
    TestEqual(TEXT("Host only presents"), Admissions, 0);
    Host->GetDriver()->Advance(2.25f);
    TestEqual(TEXT("At most sixteen periods"), Admissions, 16);
    Host->GetDriver()->Advance(0.f);
    TestEqual(TEXT("Only one period remains after clamp"), Admissions, 17);
    Host->GetDriver()->Advance(0.f);
    TestEqual(TEXT("Older backlog is discarded"), Admissions, 17);
    Host->Finish(true);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyOrbitLeaseGuard,
                                 "test_25d.UnifiedGA.LegacyDrivers.OrbitOverlapRejectsCancelledCast",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Projectile overlap may execute before its parent driver Tick; cancellation must guard damage at that boundary. */
bool FAbilityLegacyOrbitLeaseGuard::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Enemy = Fixture.Character(FVector(1000.f, 0.f, 0.f), TEXT("Enemy"));
    auto* Context = Fixture.Context();
    FString Error;
    TestTrue(TEXT("Capture source"), Context->CaptureSourceLife(Error));
    auto* Bolt = Fixture.World->SpawnActor<AMyOrbitingFireBolt>();
    Bolt->DamageEffectParams.SourceAbilitySystemComponent = Fixture.Caster->GetAbilitySystemComponent();
    Bolt->SetCastContext(Context);
    Bolt->InitializeOrbit(Fixture.Caster, NewObject<UOrbitingFireBoltConfig>(), 0.f);
    Bolt->LaunchAt(Enemy);
    TestTrue(TEXT("Current life can overlap"), Bolt->IsValidOverlap(Enemy));
    Context->bCancelled = true;
    TestFalse(TEXT("Independent projectile overlap stops before driver next tick"), Bolt->IsValidOverlap(Enemy));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyCompiledChildScheduling,
                                 "test_25d.UnifiedGA.LegacyDrivers.CompiledDelayAdmitsGraphChild",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Compiler snapshots feed registered drivers, and a real delayed completion schedules a separate graph invocation. */
bool FAbilityLegacyCompiledChildScheduling::RunTest(const FString& Parameters)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    auto* Config = Fixture.Prototype(EAbilityPrototypeKind::Delay);
    Config->ArmDelay = .125f;
    Config->OnExpire = Fixture.Prototype(EAbilityPrototypeKind::Burst);
    FString Error;
    auto* Graph = UAbilityLegacyGraphCompiler::Compile(GetTransientPackage(), Config, TEXT("LegacyActor.v1"), Error);
    if (!TestNotNull(TEXT("Compiled finite graph"), Graph))
    {
        AddError(Error);
        return false;
    }
    const auto* RootNode = Graph->Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Graph->EntryNodeId;
        });
    const auto* FrozenRoot = RootNode ? Cast<UAbilityLegacyNodeConfig>(RootNode->Config) : nullptr;
    if (!TestNotNull(TEXT("Compiled root configuration"), FrozenRoot))
    {
        return false;
    }
    Config->OnExpire->Damage = 321.f;
    Config->OnExpire->Period = .01f;
    TestTrue(TEXT("Child parameter reference is frozen"), FrozenRoot->Parameters->OnExpire != Config->OnExpire);
    TestEqual(TEXT("Source child damage edits cannot reach frozen validation references"),
              FrozenRoot->Parameters->OnExpire->Damage, 0.f);
    TestEqual(TEXT("Source child invalid timing cannot invalidate the snapshot"),
              FrozenRoot->Parameters->OnExpire->Period, .125f);
    auto* Context = Fixture.Context();
    TestTrue(TEXT("Source life"), Context->CaptureSourceLife(Error));
    FAbilityNodeInput Input;
    Input.SourceToken = Context->GetSourceLife();
    Input.Context = Context;
    auto* Run = NewObject<UAbilityGraphInstance>();
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = TEXT("LegacyActor.v1");
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    int32 Expirations = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("OnExpire"))
            {
                ++Expirations;
            }
        });
    if (!TestTrue(TEXT("Registered strategy starts"), Run->StartWithInput(Graph, Input, Options, Error)))
    {
        AddError(Error);
        return false;
    }
    // Test worlds defer engine BeginPlay; production worlds dispatch it during SpawnDriven.
    for (TActorIterator<APrototypeRuntime> It(Fixture.World); It; ++It)
    {
        Fixture.Begin(*It);
    }
    Config->ArmDelay = 5.f;
    Run->Advance(.125f);
    TestEqual(TEXT("Frozen delay expires at its compiled time"), Expirations, 1);
    TestEqual(TEXT("Child admitted through graph shares one cast budget"), Context->Spawned, 2);
    TestEqual(TEXT("Only child invocation remains active"), Run->GetActiveInvocationCount(), 1);
    Run->Cancel();
    TestTrue(TEXT("Graph cancellation reaches the shared cast"), Context->bCancelled);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyEmptyPortComposition,
                                 "test_25d.UnifiedGA.LegacyDrivers.EmptyLegacyExpiryPortConnectsToQuery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Authored graph edges can attach shared query behavior even when the original DA had no child reference. */
bool FAbilityLegacyEmptyPortComposition::RunTest(const FString& Parameters)
{
    for (const FName Profile :
         {FName(TEXT("LegacyActor.v1")), FName(TEXT("LegacyMixed.v1")), FName(TEXT("LegacyExtension.v1"))})
    {
        AbilityLegacyDriverTests::FFixture Fixture;
        UAbilityPrototype* Config = nullptr;
        UPrototypeCastContext* Context = nullptr;
        if (Profile == TEXT("LegacyExtension.v1"))
        {
            auto* Extension = NewObject<UExtensionNode>();
            Extension->Behavior = EExtensionKind::Burst;
            Extension->Damage = 0.f;
            Extension->Duration = .125f;
            Config = Extension;
            Context = NewObject<UExtensionCastContext>();
            Context->Caster = Fixture.Caster;
            Context->DamageParams.SourceAbilitySystemComponent = Fixture.Caster->GetAbilitySystemComponent();
            TestNull(TEXT("Extension complete child starts empty"), Extension->CompleteChild.Get());
        }
        else
        {
            Config = Fixture.Prototype(EAbilityPrototypeKind::Burst);
            Config->Duration = .125f;
            Context = Fixture.Context();
            TestNull(TEXT("Legacy expiry child starts empty"), Config->OnExpire.Get());
        }
        FString Error;
        auto* Graph = UAbilityLegacyGraphCompiler::Compile(GetTransientPackage(), Config, Profile, Error);
        if (!TestNotNull(*FString::Printf(TEXT("Compile %s"), *Profile.ToString()), Graph))
        {
            AddError(Error);
            return false;
        }
        FAbilityGraphNode Query;
        Query.NodeId = FGuid::NewGuid();
        Query.TypeId = TEXT("Query.Shape");
        Query.Config = NewObject<UAbilityQueryNodeConfig>(Graph);
        Graph->Nodes.Add(Query);
        FAbilityGraphEdge Edge;
        Edge.From = Graph->EntryNodeId;
        Edge.To = Query.NodeId;
        Edge.OutputPort = TEXT("OnExpire");
        Graph->Edges.Add(Edge);
        TestTrue(TEXT("Source captured"), Context->CaptureSourceLife(Error));
        FAbilityNodeInput Input;
        Input.SourceToken = Context->GetSourceLife();
        Input.Context = Context;
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Profile;
        Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        auto* Run = NewObject<UAbilityGraphInstance>();
        int32 EmptyQueryResults = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("NoTarget"))
                {
                    ++EmptyQueryResults;
                }
            });
        if (!TestTrue(TEXT("Graph validates and starts"), Run->StartWithInput(Graph, Input, Options, Error)))
        {
            AddError(Error);
            return false;
        }
        for (TActorIterator<APrototypeRuntime> It(Fixture.World); It; ++It)
        {
            Fixture.Begin(*It);
        }
        for (TActorIterator<APureMassRuntime> It(Fixture.World); It; ++It)
        {
            Fixture.Begin(*It);
        }
        for (TActorIterator<AExtensionRuntime> It(Fixture.World); It; ++It)
        {
            Fixture.Begin(*It);
        }
        Run->Advance(.125f);
        TestEqual(*FString::Printf(TEXT("Empty %s port actually invokes the shared query"), *Profile.ToString()),
                  EmptyQueryResults, 1);
        TestTrue(TEXT("Composed graph reaches completion"), Run->IsFinished());
        TestEqual(TEXT("Shared query does not spawn another legacy runtime"), Context->Spawned, 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityMixedSlowLifeBoundary,
                                 "test_25d.UnifiedGA.LegacyDrivers.SlowRechecksLifeAfterDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A real GE observer changes the lease or cancels the source before the Field's secondary slow. */
bool FAbilityMixedSlowLifeBoundary::RunTest(const FString&)
{
    for (const bool bCancelSource : {false, true})
    {
        AbilityLegacyDriverTests::FFixture Fixture;
        auto* Enemy = Fixture.Character(FVector(100, 0, 0), TEXT("Enemy"));
        auto* Config = NewObject<UBatch6FieldPrototype>();
        Config->Kind = EAbilityPrototypeKind::Field;
        Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Config->Cooldown = 0.f;
        Config->SlowMultiplier = .5f;
        auto* Context = Fixture.Context();
        Context->DamageParams.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
        auto* Host =
            APureMassRuntime::SpawnDriven(Context, Config, FVector::ZeroVector, FVector::ForwardVector, {}, 0, false,
                                          [](const FLegacyAbilityChildRequest&)
                                          {
                                              return false;
                                          });
        if (!TestNotNull(TEXT("Field host allocated"), Host))
        {
            return false;
        }
        int32 Effects = 0;
        auto* ASC = Enemy->GetAbilitySystemComponent();
        const auto Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                ++Effects;
                if (bCancelSource)
                {
                    Context->bCancelled = true;
                }
                else
                {
                    Enemy->EnableDungeonPooling();
                }
            });
        TestTrue(TEXT("Original hit reaches the real GAS call"),
                 Host->GetDriver()->Hit(FPureMassCombatTarget::FromActor(Enemy)));
        TestEqual(TEXT("Exactly one damage GE was observed"), Effects, 1);
        TestEqual(TEXT("Secondary slow cannot cross cancellation or target-life boundary"),
                  UBatch6SlowComponent::GetMultiplier(Enemy), 1.f);
        TestNull(TEXT("No slow component was created on the replacement life"),
                 Enemy->FindComponentByClass<UBatch6SlowComponent>());
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
        Host->GetDriver()->Finish(true);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyBudgetRefusal,
                                 "test_25d.UnifiedGA.LegacyDrivers.ChildBudgetRefusalKeepsEmitterAlive",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Streaming events do not remove the original total-spawn cap; rejection leaves an already admitted emitter alive. */
bool FAbilityLegacyBudgetRefusal::RunTest(const FString&)
{
    AbilityLegacyDriverTests::FFixture Fixture;
    Fixture.Character(FVector(100, 0, 0), TEXT("Enemy"));
    auto* Config = Fixture.Prototype(EAbilityPrototypeKind::Emitter);
    Config->Duration = 1.f;
    Config->Count = 8;
    Config->OnPeriod = Fixture.Prototype(EAbilityPrototypeKind::Burst);
    Config->OnPeriod->Duration = .1f;
    FString Error;
    auto* Graph = UAbilityLegacyGraphCompiler::Compile(Fixture.World, Config, TEXT("LegacyActor.v1"), Error);
    if (!TestNotNull(TEXT("Emitter compiles"), Graph))
    {
        return false;
    }
    Graph->Budget.MaxInvocations = 2;
    auto* Context = Fixture.Context();
    Context->CaptureSourceLife(Error);
    FAbilityNodeInput Input;
    Input.Context = Context;
    Input.SourceToken = Context->GetSourceLife();
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    TestTrue(TEXT("Bounded emitter starts"), Run->StartWithInput(Graph, Input, Options, Error));
    Run->Advance(.125f);
    TestEqual(TEXT("One child is admitted"), Context->Spawned, 2);
    Run->Advance(.5f);
    TestEqual(TEXT("Further child requests are refused without spawning"), Context->Spawned, 2);
    TestFalse(TEXT("Existing emitter continues until its duration"), Run->IsFinished());
    TestFalse(TEXT("Refusal does not cancel the cast"), Context->bCancelled);
    Run->Advance(.5f);
    TestTrue(TEXT("Emitter expires normally"), Run->IsFinished());
    TestTrue(TEXT("No budget failure replaces normal completion"),
             Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    return true;
}
#endif
