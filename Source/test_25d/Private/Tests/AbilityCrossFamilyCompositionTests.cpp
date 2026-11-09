// Automation -> one Schema2 graph -> Mixed/Extension drivers -> shared query/damage and persistent-world cleanup.
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace AbilityCrossFamilyTests
{
    /** Uses the existing PlayerState ASC test pawn; no duplicate AttributeSet or gameplay BeginPlay is introduced. */
    struct FFixture
    {
        FEditorScriptExecutionGuard Guard;
        UWorld* World = nullptr;
        ADungeonPlayerRunTestPawn* Source = nullptr;
        UPureMassSubsystem* Mass = nullptr;
        UExtensionWorldSubsystem* Persistent = nullptr;
        UAbilityGraphDefinition* Graph = nullptr;
        UPrototypeCastContext* Context = nullptr;
        UAbilityGraphInstance* Run = nullptr;

        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector(-500, 0, 0), FRotator::ZeroRotator, Spawn);
            Source->AttachState(World->SpawnActor<AMyPlayerState>());
            Source->Tags = {TEXT("Player")};
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Persistent = World->GetSubsystem<UExtensionWorldSubsystem>();
            Context = NewContext();
            Graph = NewObject<UAbilityGraphDefinition>(World);
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("LegacyMixed.v1");
            Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
            Graph->Budget.MaxActiveNodes = 64;
        }

        ~FFixture()
        {
            if (Run && !Run->IsFinished())
            {
                Run->Cancel();
            }
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        UPrototypeCastContext* NewContext()
        {
            auto* Result = NewObject<UPrototypeCastContext>(World);
            Result->Caster = Source;
            Result->DamageParams.SourceAbilitySystemComponent = Source->GetAbilitySystemComponent();
            Result->DamageParams.WorldContextObject = Source;
            Result->DamageParams.DebuffChance = 0.f;
            return Result;
        }

        int32 SpawnMass(FVector Position = FVector(200, 0, 0))
        {
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 1000.f;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            return Mass->SpawnMonster(Type, Position);
        }

        AEnemyCharacter* SpawnActor()
        {
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Result = World->SpawnActor<AEnemyCharacter>(FVector(200, 0, 0), FRotator::ZeroRotator, Spawn);
            Result->Tags = {TEXT("Enemy")};
            Result->GetAbilitySystemComponent()->InitAbilityActorInfo(Result, Result);
            Result->GetCapsuleComponent()->SetCapsuleSize(20.f, 40.f);
            Result->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Result->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            return Result;
        }

        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }

        UAbilityPrototype* Mixed(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = Result->DamagePerLevel = 0.f;
            Result->Cooldown = 0.f;
            Result->Duration = 2.f;
            Result->ArmDelay = Result->Period = .125f;
            return Result;
        }

        UExtensionNode* Extension(EExtensionKind Kind)
        {
            auto* Result = NewObject<UExtensionNode>(World);
            Result->Behavior = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = Result->DamagePerLevel = 0.f;
            Result->Cooldown = 0.f;
            Result->Duration = 2.f;
            Result->ArmDelay = Result->Period = .125f;
            return Result;
        }

        /** Compile each family using its real validator; merge only the selected entry into one authored graph. */
        FGuid AddFamily(UAbilityPrototype* Parameters, FName Profile, int32 Depth, FAutomationTestBase& Test)
        {
            FString Error;
            auto* Compiled = UAbilityLegacyGraphCompiler::Compile(World, Parameters, Profile, Error);
            if (!Test.TestNotNull(TEXT("Family compiler"), Compiled))
            {
                Test.AddError(Error);
                return {};
            }
            const auto* Entry = Compiled->Nodes.FindByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    return Node.NodeId == Compiled->EntryNodeId;
                });
            FAbilityGraphNode Node = *Entry;
            Node.NodeId = FGuid::NewGuid();
            Node.Config = DuplicateObject<UAbilityNodeConfig>(Entry->Config, Graph);
            CastChecked<UAbilityLegacyNodeConfig>(Node.Config)->LegacyDepth = Depth;
            Graph->Nodes.Add(Node);
            return Node.NodeId;
        }

        void Connect(FGuid From, FName Port, FGuid To)
        {
            FAbilityGraphEdge Edge;
            Edge.From = From;
            Edge.To = To;
            Edge.OutputPort = Port;
            Graph->Edges.Add(Edge);
        }

        /** Generic Query -> Damage stays independent of either legacy backend and damages the real Mass target. */
        void AddQueryDamage(FGuid From, FName Port)
        {
            FAbilityGraphNode Query;
            Query.NodeId = FGuid::NewGuid();
            Query.TypeId = TEXT("Query.Shape");
            auto* QueryConfig = NewObject<UAbilityQueryNodeConfig>(Graph);
            QueryConfig->Query.bIncludeActors = false;
            QueryConfig->Query.bRequireVisibility = false;
            Query.Config = QueryConfig;
            Graph->Nodes.Add(Query);
            FAbilityGraphNode Damage;
            Damage.NodeId = FGuid::NewGuid();
            Damage.TypeId = TEXT("Effect.Damage");
            auto* DamageConfig = NewObject<UAbilityDamageNodeConfig>(Graph);
            DamageConfig->Damage = 25.f;
            DamageConfig->DamagePerLevel = 0.f;
            DamageConfig->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Damage.Config = DamageConfig;
            Graph->Nodes.Add(Damage);
            Connect(From, Port, Query.NodeId);
            Connect(Query.NodeId, TEXT("TargetFound"), Damage.NodeId);
        }

        bool Start(int32 Id, FAutomationTestBase& Test)
        {
            FString Error;
            if (!Test.TestTrue(TEXT("Source captured"), Context->CaptureSourceLife(Error)))
            {
                Test.AddError(Error);
                return false;
            }
            FVector Position;
            if (!Test.TestTrue(TEXT("Target captured"), Context->ResolveTargetLife(Id, Position)))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.LiveTargetToken = *Context->FindTargetLife(Id);
            Input.Origin = Position;
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Graph->CompatibilityProfile;
            Options.BudgetPolicy = Graph->BudgetPolicy;
            Run = NewObject<UAbilityGraphInstance>(World);
            if (!Test.TestTrue(TEXT("One cross-family graph starts"),
                               Run->StartWithInput(Graph, Input, Options, Error)))
            {
                Test.AddError(Error);
                return false;
            }
            BeginHosts();
            return true;
        }

        /** Engine test worlds defer BeginPlay; only behavior hosts are dispatched, never player/AI actors. */
        void BeginHosts()
        {
            TArray<AActor*> Pending;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!It->HasActorBegunPlay() &&
                    (It->IsA<APrototypeRuntime>() || It->IsA<APureMassRuntime>() || It->IsA<AExtensionRuntime>()))
                {
                    Pending.Add(*It);
                }
            }
            for (AActor* Host : Pending)
            {
                Host->DispatchBeginPlay();
            }
        }

        void Floor()
        {
            auto* Actor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(1000, 1000, 10));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Actor->SetActorLocation(FVector(0, 0, -51));
        }
    };
} // namespace AbilityCrossFamilyTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityCrossMixedStack,
                                 "test_25d.UnifiedGA.CrossFamily.MixedDelayToExtensionStackToDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A base cast context crosses into persistent Extension state and resumes shared graph damage after detonation. */
bool FAbilityCrossMixedStack::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    const int32 Id = F.SpawnMass();
    auto* Delay = F.Mixed(EAbilityPrototypeKind::Delay);
    Delay->OnExpire = F.Mixed(EAbilityPrototypeKind::Burst);
    auto* Stack = F.Extension(EExtensionKind::Stack);
    Stack->EffectId = TEXT("CrossFamilyStack");
    Stack->StackThreshold = 1;
    Stack->DetonationDelay = .125f;
    Stack->CompleteChild = F.Extension(EExtensionKind::Burst);
    const FGuid Entry = F.AddFamily(Delay, TEXT("LegacyMixed.v1"), 0, *this);
    const FGuid Persistent = F.AddFamily(Stack, TEXT("LegacyExtension.v1"), 1, *this);
    F.Graph->EntryNodeId = Entry;
    F.Connect(Entry, TEXT("OnExpire"), Persistent);
    F.AddQueryDamage(Persistent, TEXT("OnExpire"));
    if (!F.Start(Id, *this))
    {
        return false;
    }
    F.Run->Advance(.125f);
    F.BeginHosts();
    TestEqual(TEXT("Stack is owned by the persistent subsystem"), F.Persistent->StackCount(), 1);
    TestEqual(TEXT("No premature damage before detonation"), F.Health(Id), 1000.f);
    TestFalse(TEXT("Graph waits for persistent continuation"), F.Run->IsFinished());
    F.Persistent->Tick(.125f);
    F.Run->Advance(0.f);
    TestEqual(TEXT("Shared damage node ran after stack release"), F.Health(Id), 975.f);
    TestTrue(TEXT("Base context records detonation"), F.Context->DetonatedEffects.Contains(Stack->EffectId));
    TestEqual(TEXT("Only two legacy hosts were admitted"), F.Context->Spawned, 2);
    TestTrue(TEXT("Whole graph finishes after its continuation"), F.Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityCrossExtensionMixed,
                                 "test_25d.UnifiedGA.CrossFamily.ExtensionDelayToMixedBurstToDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** The inverse family order uses one graph, one base cast context and the same generic damage strategy. */
bool FAbilityCrossExtensionMixed::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    const int32 Id = F.SpawnMass();
    auto* Delay = F.Extension(EExtensionKind::Delay);
    auto* Burst = F.Mixed(EAbilityPrototypeKind::Burst);
    Burst->Duration = .125f;
    const FGuid Entry = F.AddFamily(Delay, TEXT("LegacyExtension.v1"), 0, *this);
    const FGuid Mixed = F.AddFamily(Burst, TEXT("LegacyMixed.v1"), 1, *this);
    F.Graph->EntryNodeId = Entry;
    F.Connect(Entry, TEXT("OnExpire"), Mixed);
    F.AddQueryDamage(Mixed, TEXT("OnExpire"));
    if (!F.Start(Id, *this))
    {
        return false;
    }
    F.Run->Advance(.125f);
    F.BeginHosts();
    TestEqual(TEXT("Mixed child begins without duplicated root cost or damage"), F.Health(Id), 1000.f);
    F.Run->Advance(.125f);
    TestEqual(TEXT("Generic damage follows Mixed expiry"), F.Health(Id), 975.f);
    TestEqual(TEXT("Both families share the original spawn budget"), F.Context->Spawned, 2);
    TestTrue(TEXT("Inverse composition completes"), F.Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityCrossAttachCancellation,
                                 "test_25d.UnifiedGA.CrossFamily.MixedDelayToAttachPeriodCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A periodic Extension branch created by Mixed execution routes shared damage and closes on graph cancellation. */
bool FAbilityCrossAttachCancellation::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    const int32 Id = F.SpawnMass();
    auto* Delay = F.Mixed(EAbilityPrototypeKind::Delay);
    Delay->OnExpire = F.Mixed(EAbilityPrototypeKind::Burst);
    auto* Attach = F.Extension(EExtensionKind::Attach);
    Attach->PeriodChild = F.Extension(EExtensionKind::Burst);
    const FGuid Entry = F.AddFamily(Delay, TEXT("LegacyMixed.v1"), 0, *this);
    const FGuid Attached = F.AddFamily(Attach, TEXT("LegacyExtension.v1"), 1, *this);
    F.Graph->EntryNodeId = Entry;
    F.Connect(Entry, TEXT("OnExpire"), Attached);
    F.AddQueryDamage(Attached, TEXT("OnPeriod"));
    if (!F.Start(Id, *this))
    {
        return false;
    }
    F.Run->Advance(.125f);
    F.BeginHosts();
    F.Run->Advance(.125f);
    TestEqual(TEXT("Attached period reaches shared damage"), F.Health(Id), 975.f);
    F.Run->Cancel();
    F.Run->Advance(1.f);
    F.Persistent->Tick(1.f);
    TestEqual(TEXT("Cancelled graph cannot emit another damage period"), F.Health(Id), 975.f);
    TestTrue(TEXT("Cross-family context is cancelled"), F.Context->bCancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionRootMotionOwnership,
                                 "test_25d.UnifiedGA.CrossFamily.DisplacementCleanupOwnsExactRootMotion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Simulate an engine uint16 source ID being reused after pool wake; cleanup may retire only its original source. */
bool FExtensionRootMotionOwnership::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    F.Floor();
    auto* Target = F.SpawnActor();
    Target->EnableDungeonPooling(0.f);
    auto* Node = F.Extension(EExtensionKind::Displace);
    Node->Duration = 1.f;
    Node->MoveDistance = 100.f;
    const FGuid Token = F.Persistent->RequestDisplacement(F.Context, Node, FPureMassCombatTarget::FromActor(Target),
                                                          FVector::ZeroVector, FVector::ForwardVector);
    if (!TestTrue(TEXT("Original displacement accepted"), Token.IsValid()))
    {
        return false;
    }
    auto* Movement = Target->GetCharacterMovement();
    const auto OldSource = Movement->GetRootMotionSource(TEXT("ExtensionDisplace"));
    if (!TestTrue(TEXT("Original force exists"), OldSource.IsValid()))
    {
        return false;
    }
    const uint16 OldId = OldSource->LocalID;
    Target->SleepForDungeonPool();
    TestTrue(TEXT("Target wakes into next life"),
             Target->WakeFromDungeonPool(FTransform(FVector(200, 0, 0)), NAME_None));
    Movement->CurrentRootMotion.Clear();
    auto NewSource = MakeShared<FRootMotionSource_MoveToForce>();
    NewSource->InstanceName = TEXT("NewLifeForce");
    NewSource->Duration = 1.f;
    Movement->ApplyRootMotionSource(NewSource);
    NewSource->LocalID = OldId;
    F.Persistent->Tick(0.f);
    TestTrue(TEXT("Old movement token is retired"), F.Persistent->WasMoveReplaced(Token));
    TestTrue(TEXT("Owned old source is marked for removal"),
             OldSource->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    TestFalse(TEXT("Reused ID does not remove the new life force"),
              NewSource->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionStackNewLifeIsolation,
                                 "test_25d.UnifiedGA.CrossFamily.StackLayersDoNotCarryIntoNewLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** A pooled actor's new lease cannot inherit nonexpired layers owned by its previous target token. */
bool FExtensionStackNewLifeIsolation::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    auto* Target = F.SpawnActor();
    Target->EnableDungeonPooling(0.f);
    auto* Node = F.Extension(EExtensionKind::Stack);
    Node->EffectId = TEXT("LeaseIsolatedStack");
    Node->StackThreshold = 3;
    Node->CompleteChild = F.Extension(EExtensionKind::Burst);
    const auto Identity = FPureMassCombatTarget::FromActor(Target);
    TestTrue(TEXT("Old life receives first layer"),
             F.Persistent->AddStack(F.Context, Node, Identity, Target->GetActorLocation(), FVector::ForwardVector, 0));
    Target->SleepForDungeonPool();
    TestTrue(TEXT("Target wakes"), Target->WakeFromDungeonPool(FTransform(FVector(200, 0, 0)), NAME_None));
    auto* NextContext = F.NewContext();
    TestTrue(
        TEXT("New life receives independent layer"),
        F.Persistent->AddStack(NextContext, Node, Identity, Target->GetActorLocation(), FVector::ForwardVector, 0));
    F.Persistent->Tick(0.f);
    TestEqual(TEXT("Only one fresh layer survives"), F.Persistent->LayersFor(F.Source, Node->EffectId, Identity), 1);
    TestEqual(TEXT("Retired old life record is removed"), F.Persistent->StackCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExtensionMassMoveLifeIsolation,
                                 "test_25d.UnifiedGA.CrossFamily.MassDisplacementRejectsRetiredHandle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** The processor boundary must resolve the captured entity before touching a fragment, even before subsystem Tick cleanup. */
bool FExtensionMassMoveLifeIsolation::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    F.Floor();
    const int32 Id = F.SpawnMass();
    auto* Node = F.Extension(EExtensionKind::Displace);
    Node->MoveDistance = 100.f;
    const FGuid Token =
        F.Persistent->RequestDisplacement(F.Context, Node, Id, FVector::ZeroVector, FVector::ForwardVector);
    if (!TestTrue(TEXT("Mass displacement accepted"), Token.IsValid()))
    {
        return false;
    }
    auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    const auto Handle = F.Mass->GetEntity(Id);
    FPureMassFragment RetiredFragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Handle);
    const FVector Before = RetiredFragment.Location;
    const float RootBefore = RetiredFragment.RootRemaining;
    Manager.DestroyEntity(Handle);
    F.Persistent->MoveMass(RetiredFragment, .125f);
    TestTrue(TEXT("Retired fragment position is untouched"), RetiredFragment.Location.Equals(Before));
    TestEqual(TEXT("Retired fragment receives no new root"), RetiredFragment.RootRemaining, RootBefore);
    TestTrue(TEXT("Invalid command token is retired immediately"), F.Persistent->WasMoveReplaced(Token));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyImpactPetrifyBridge,
                                 "test_25d.UnifiedGA.CrossFamily.LegacyHitComposesLiveStatus",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 旧 Burst 的真实命中快照经过原生命复核后，能直接接入新状态节点，不嵌套激活另一个 GA。 */
bool FLegacyImpactPetrifyBridge::RunTest(const FString& Parameters)
{
    AbilityCrossFamilyTests::FFixture F;
    const int32 Target = F.SpawnMass();
    auto* Burst = F.Mixed(EAbilityPrototypeKind::Burst);
    Burst->Damage = 25.f;
    F.Graph->EntryNodeId = F.AddFamily(Burst, TEXT("LegacyMixed.v1"), 0, *this);
    FAbilityGraphNode Resolve, Petrify;
    Resolve.NodeId = FGuid::NewGuid();
    Resolve.TypeId = TEXT("Target.ResolveImpact");
    Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(F.Graph);
    Petrify.NodeId = FGuid::NewGuid();
    Petrify.TypeId = TEXT("Status.Petrify");
    Petrify.Config = NewObject<UAbilityPetrifyNodeConfig>(F.Graph);
    F.Graph->Nodes.Add(Resolve);
    F.Graph->Nodes.Add(Petrify);
    F.Connect(F.Graph->EntryNodeId, TEXT("OnHit"), Resolve.NodeId);
    F.Connect(Resolve.NodeId, TEXT("TargetFound"), Petrify.NodeId);
    if (!F.Start(Target, *this))
    {
        return false;
    }
    F.Run->Advance(.01f);
    FPureMassView View;
    TestTrue(TEXT("Target survives actual old damage"), F.Mass->Resolve(Target, View));
    TestEqual(TEXT("Legacy hit occurs once"), View.Health, 975.f);
    TestTrue(TEXT("New Petrify receives exact old hit target"), View.bPetrified);
    return true;
}
#endif
