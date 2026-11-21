// 调用链：实际原Orbit/独立库存和投递图 -> 真ASC提交/Actor PM或Mass Sweep -> 可替换Slow与资源清理。
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilityPrototypeOrbitTestTypes.h"
#include "AbilityPrototypeNiagaraTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "EngineUtils.h"
#include "test_25d/test_25d.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeOrbitNodeTests
{
    int32 Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        return Definition.Nodes.Add(Node);
    }
    void Connect(UAbilityGraphDefinition& Definition, FGuid From, FName Port, FGuid To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = From;
        Edge.To = To;
        Edge.OutputPort = Port;
        Definition.Edges.Add(Edge);
    }

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Binding = nullptr;
        TWeakObjectPtr<UAbilityPrototypeOrbitInventoryNodeExecution> Inventory;
        UAbilityPrototypeParameterFrame* ScopeFrame = nullptr;
        AActor* RootHost = nullptr;
        FGameplayAbilitySpecHandle AttackHandle;
        bool bActor = true;
        bool bFine = false;
        bool bCompileDefault = false;
        bool bUseForceInputs = false;
        FDamageEffectParams ForceInputs;
        int32 MassId = INDEX_NONE;

        FWorld()
        {
            auto* Component = Target->GetAbilitySystemComponent();
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Target->SetActorLocation(FVector(100, 0, 0));
            for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Target))
            {
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        }
        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
            ASC->CancelAllAbilities();
        }

        int32 SpawnMass(FVector Position)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            return MassId = Sim->SpawnMonster(Type, Position);
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
        float Mana() const
        {
            return ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        }
        float MassHealth() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) ? View.Health : -1.f;
        }
        int32 MassSlowCount() const
        {
            const auto& Fragment = World->GetSubsystem<UMassEntitySubsystem>()
                                       ->GetEntityManager()
                                       .GetFragmentDataChecked<FPureMassFragment>(
                                           World->GetSubsystem<UPureMassSubsystem>()->GetEntity(MassId));
            return Fragment.SpellSlows.Num();
        }

        UAbilityPrototype* Burst(float Damage)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = EAbilityPrototypeKind::Burst;
            Result->Radius = 300.f;
            Result->Duration = .1f;
            Result->Damage = Damage;
            Result->DamagePerLevel = 0.f;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Cooldown = 0.f;
            return Result;
        }
        UAbilityPrototype* Parameters()
        {
            auto* Result = Burst(0.f);
            Result->Kind = EAbilityPrototypeKind::Orbit;
            Result->Duration = .5f;
            Result->ManaPerSecond = 2.f;
            Result->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(Result);
            auto* Settings = Result->OrbitSettings.Get();
            Settings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
            Settings->MaxFireBolts = 2;
            Settings->SpawnInterval = .25f;
            Settings->LaunchInterval = .05f;
            Settings->OrbitRadius = 10.f;
            Settings->OrbitHeight = 0.f;
            Settings->OrbitDegreesPerSecond = 0.f;
            Settings->ProjectileSpeed = 1000.f;
            Settings->MaxFlightSeconds = 2.f;
            // 不能偷偷复用Native库存整节点按Settings扣费；原prototype使用外层ManaPerSecond=2。
            Settings->ManaPerSecond = 37.f;
            return Result;
        }

        UAbilityGraphDefinition* Fine(UAbilityPrototype& InParameters, FString& Error)
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
            auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Scope->Parameters = &InParameters;
            Scope->Profile = Definition->CompatibilityProfile;
            Scope->LogicalRejectionPolicy = EAbilityPrototypeLogicalRejectionPolicy::FailExecution;
            const int32 Root = Add(*Definition, TEXT("Scope.Prototype"), Scope);
            const int32 Host = Add(*Definition, TEXT("Control.PrototypeOrbitInventory"),
                                   NewObject<UAbilityPrototypeOrbitInventoryNodeConfig>(Definition));
            const int32 Delivery = Add(*Definition, TEXT("Delivery.PrototypeOrbit"),
                                       NewObject<UAbilityPrototypeOrbitDeliveryNodeConfig>(Definition));
            auto* State = NewObject<UAbilitySlowNodeConfig>(Definition);
            State->Multiplier = .5f;
            State->Duration = 3.f;
            const int32 Slow = Add(*Definition, TEXT("Status.Slow"), State);
            Definition->EntryNodeId = Definition->Nodes[Root].NodeId;
            Connect(*Definition, Definition->Nodes[Root].NodeId, TEXT("Ready"), Definition->Nodes[Host].NodeId);
            Connect(*Definition, Definition->Nodes[Host].NodeId, TEXT("Launch"), Definition->Nodes[Delivery].NodeId);
            Connect(*Definition, Definition->Nodes[Delivery].NodeId, TEXT("Contact"), Definition->Nodes[Slow].NodeId);
            if (InParameters.OnPeriod)
            {
                auto* Child = AbilityPrototypeFineCompiler::Compile(Definition, InParameters.OnPeriod,
                                                                    Definition->CompatibilityProfile, Error);
                if (!Child)
                {
                    return nullptr;
                }
                for (auto& Node : Child->Nodes)
                {
                    if (auto* ChildScope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config))
                    {
                        ChildScope->LogicalDepth = 1;
                    }
                }
                Definition->Nodes.Append(Child->Nodes);
                Definition->Edges.Append(Child->Edges);
                Connect(*Definition, Definition->Nodes[Host].NodeId, TEXT("Period"), Child->EntryNodeId);
            }
            return Definition;
        }

        bool Start(UAbilityPrototype& InParameters, bool bFinePath, bool bActorProfile, bool bDamage, FString& Error)
        {
            bFine = bFinePath;
            bActor = bActorProfile;
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->CastId = FGuid::NewGuid();
            Binding->Level = 1;
            if (bUseForceInputs)
            {
                Binding->DamageParams = ForceInputs;
            }
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass =
                bDamage ? UArcaneShardTestDamageEffect::StaticClass() : UGameplayEffect::StaticClass();
            if (!Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
            auto* Definition = bFine && bCompileDefault
                                   ? AbilityPrototypeFineCompiler::Compile(World, &InParameters, Profile, Error)
                               : bFine ? Fine(InParameters, Error)
                                       : UAbilityLegacyGraphCompiler::Compile(World, &InParameters, Profile, Error);
            if (!Definition)
            {
                return false;
            }
            if (!bFine && !bDamage)
            {
                auto* Config = NewObject<UAbilitySlowNodeConfig>(Definition);
                Config->Multiplier = .5f;
                Config->Duration = 3.f;
                const int32 Resolve = Add(*Definition, TEXT("Target.ResolveImpact"),
                                          NewObject<UAbilityResolveImpactNodeConfig>(Definition));
                const int32 State = Add(*Definition, TEXT("Status.Slow"), Config);
                Connect(*Definition, Definition->Nodes[0].NodeId, TEXT("OnHit"), Definition->Nodes[Resolve].NodeId);
                Connect(*Definition, Definition->Nodes[Resolve].NodeId, TEXT("TargetFound"),
                        Definition->Nodes[State].NodeId);
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Binding->GetSourceLife();
            Input.Context = Binding;
            Input.Direction = FVector::ForwardVector;
            Input.DamageParams = Binding->DamageParams;
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            AbilityPrototypeOrbitNodes::Register(Registry);
            Run = NewObject<UAbilityGraphInstance>(World);
            Run->OnEvent.AddLambda(
                [this](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType == TEXT("Scope.Prototype") && Event.OutputPort == TEXT("Ready") && !ScopeFrame)
                    {
                        ScopeFrame = Cast<UAbilityPrototypeParameterFrame>(Event.Input.ParameterFrame.Get());
                    }
                });
            const bool bAccepted = Run->StartWithInput(Definition, Input, Options, Registry, Error);
            if (ScopeFrame)
            {
                Inventory = Cast<UAbilityPrototypeOrbitInventoryNodeExecution>(
                    Run->FindRuntimeResource(*ScopeFrame, TEXT("PrototypeOrbitInventory")));
            }
            for (TActorIterator<APrototypeRuntime> It(World); It; ++It)
            {
                if (!bFine && It->Prototype && It->Prototype->Kind == EAbilityPrototypeKind::Orbit)
                {
                    RootHost = *It;
                    break;
                }
            }
            for (TActorIterator<APureMassRuntime> It(World); It; ++It)
            {
                if (!bFine && It->Prototype && It->Prototype->Kind == EAbilityPrototypeKind::Orbit)
                {
                    RootHost = *It;
                    break;
                }
            }
            return bAccepted && !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }

        int32 Count() const
        {
            if (bFine)
            {
                return Inventory.IsValid() ? Inventory->GetInventoryCount() : 0;
            }
            if (const auto* ActorHost = Cast<APrototypeRuntime>(RootHost))
            {
                return ActorHost->GetOrbitCount();
            }
            const auto* MixedHost = Cast<APureMassRuntime>(RootHost);
            return MixedHost ? MixedHost->OrbitBolts.Num() : 0;
        }
        int32 Launched() const
        {
            if (bFine)
            {
                return Inventory.IsValid() ? Inventory->GetLaunchedCount() : 0;
            }
            int32 LaunchedCount = 0;
            if (bActor)
            {
                for (TActorIterator<AMyOrbitingFireBolt> It(World); It; ++It)
                {
                    LaunchedCount += It->IsLaunched();
                }
            }
            else if (const auto* Host = Cast<APureMassRuntime>(RootHost))
            {
                for (const auto& Bolt : Host->OrbitBolts)
                {
                    LaunchedCount += Bolt.bLaunched;
                }
            }
            return LaunchedCount;
        }
        void Step(float Delta, bool bPhysical = false)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
            for (TActorIterator<AMyOrbitingFireBolt> It(World); It; ++It)
            {
                if (!It->HasActorBegunPlay())
                {
                    It->DispatchBeginPlay();
                }
                if (bPhysical)
                {
                    It->Tick(Delta);
                    if (IsValid(*It) && It->ProjectileMovement->IsActive())
                    {
                        It->ProjectileMovement->TickComponent(Delta, LEVELTICK_All,
                                                              &It->ProjectileMovement->PrimaryComponentTick);
                    }
                }
            }
        }
        UPrototypeOrbitCommittedTestAbility* Attack()
        {
            AttackHandle =
                ASC->GiveAbility(FGameplayAbilitySpec(UPrototypeOrbitCommittedTestAbility::StaticClass(), 1));
            if (!ASC->TryActivateAbility(AttackHandle))
            {
                return nullptr;
            }
            return Cast<UPrototypeOrbitCommittedTestAbility>(
                ASC->FindAbilitySpecFromHandle(AttackHandle)->GetPrimaryInstance());
        }
    };
} // namespace AbilityPrototypeOrbitNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeOrbitInventoryProfiles,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeOrbit.RealInventoryCadenceManaAndFullPeriod",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitInventoryProfiles::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            if (!bActor)
            {
                F.SpawnMass(FVector(0, 100, 0));
            }
            auto* Config = F.Parameters();
            Config->OnPeriod = F.Burst(25.f);
            FString Error;
            if (!TestTrue(TEXT("Actual inventory graph starts without immediate Failed"),
                          F.Start(*Config, bFine, bActor, true, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Step(.75f);
            TestEqual(TEXT("Original inventory adds only one resource after a hitch"), F.Count(), 1);
            TestEqual(TEXT("Inventory period hook produces real independent Actor damage"), F.Health(), 90.f);
            TestEqual(TEXT("Actor whole-second/Mixed full-delta prototype Mana profiles differ"), F.Mana(),
                      bActor ? 100.f : 98.5f);
            F.Step(.25f);
            TestEqual(TEXT("Second refill reaches configured physical inventory capacity"), F.Count(), 2);
            TestEqual(TEXT("Both upkeep profiles now charge the prototype rate, not nested Settings37"), F.Mana(),
                      98.f);
            F.Step(1.f);
            TestEqual(TEXT("A full inventory does not create another resource"), F.Count(), 2);
            TestEqual(TEXT("Full inventory still emits the original period hook once per frame"), F.Health(), 70.f);
            TestEqual(TEXT("Physical inventory does not consume logical child budget"), F.Binding->Spawned, 4);
            TestEqual(TEXT("Root toggle ignores nominal finite Duration"), F.Run->IsFinished(), false);
            if (!bActor)
            {
                TestEqual(TEXT("Independent period payload also uses actual Mass damage backend"), F.MassHealth(),
                          125.f);
            }
            F.Run->Cancel();
            TestEqual(TEXT("Exact invocation cancellation clears its entire inventory"), F.Count(), 0);
            if (bFine)
            {
                TestEqual(TEXT("Inventory cancellation releases all physical leases"),
                          F.Run->GetActivePhysicalSpawnCount(), 0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeOrbitMixedCommittedState,
    "test_25d.UnifiedGA.DynamicMage.PrototypeOrbit.MixedCommittedAttackAndReplaceableState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitMixedCommittedState::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(2000, 0, 0));
        F.SpawnMass(FVector(100, 0, 0));
        auto* Config = F.Parameters();
        Config->ManaPerSecond = 0.f;
        Config->OrbitSettings->MaxFireBolts = 1;
        Config->OrbitSettings->ProjectileSpeed = 10000.f;
        FString Error;
        if (!TestTrue(TEXT("Mixed pure-payload inventory starts"), F.Start(*Config, bFine, false, false, Error)))
        {
            AddError(Error);
            continue;
        }
        F.Step(.25f);
        TestEqual(TEXT("A real orbiting slot exists before any attack"), F.Count(), 1);
        auto* Attack = F.Attack();
        if (!TestNotNull(TEXT("Real ASC owns an active offensive ability instance"), Attack))
        {
            continue;
        }
        const FPureMassCombatTarget Target(F.MassId);
        APureMassRuntime::NotifyCommittedAttack(F.Source, Attack, Target);
        F.Step(.125f);
        TestEqual(TEXT("Uncommitted ability cannot launch inventory"), F.Launched(), 0);
        TestEqual(TEXT("Uncommitted notification cannot cause replacement status"), F.MassSlowCount(), 0);
        TestTrue(
            TEXT("Actual GAS commit supplies the production committed-attack gate"),
            Attack->CommitAbility(F.AttackHandle, F.ASC->AbilityActorInfo.Get(), Attack->GetCurrentActivationInfo()));
        TestTrue(TEXT("Production damage-ability committed flag is set"), Attack->HasCommittedOffensiveAttack());
        APureMassRuntime::NotifyCommittedAttack(F.Source, Attack, Target);
        F.Step(.01f);
        TestEqual(TEXT("Actual light delivery consumes the same-frame launched slot after contact"), F.Count(), 0);
        TestEqual(TEXT("Contact payload can be replaced by independently owned Mass Slow"), F.MassSlowCount(), 1);
        TestEqual(TEXT("State-only graph contains no hidden default damage"), F.MassHealth(), 200.f);
        F.Run->Cancel();
        TestEqual(TEXT("Cancelled inventory does not retract landed Mass state"), F.MassSlowCount(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeOrbitActorPhysicalState,
    "test_25d.UnifiedGA.DynamicMage.PrototypeOrbit.ActorRealProjectileMovementStatePayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitActorPhysicalState::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(300, 0, 0));
        F.Target->AutoPossessAI = EAutoPossessAI::Disabled;
        if (!F.Target->IsActorInitialized())
        {
            F.Target->PostInitializeComponents();
        }
        F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
        F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* Config = F.Parameters();
        Config->ManaPerSecond = 0.f;
        Config->OrbitSettings->MaxFireBolts = 1;
        FString Error;
        if (!TestTrue(TEXT("Actor orbit with replaceable state starts"), F.Start(*Config, bFine, true, false, Error)))
        {
            AddError(Error);
            continue;
        }
        F.Step(.25f);
        TestEqual(TEXT("A real Actor inventory resource was created"), F.Count(), 1);
        auto* Attack = F.Attack();
        if (!TestNotNull(TEXT("Actor profile has a real active offensive source"), Attack))
        {
            continue;
        }
        Attack->SetOffensiveAttackTarget(F.Target);
        CastChecked<UMyAbilitySystemComponent>(F.ASC)->NotifyOffensiveAttack(Attack, F.Target);
        F.Step(.125f);
        TestEqual(TEXT("Before GAS commit actual Actor resources remain unlaunched"), F.Launched(), 0);
        TestTrue(
            TEXT("Real Actor offensive commit succeeds"),
            Attack->CommitAbility(F.AttackHandle, F.ASC->AbilityActorInfo.Get(), Attack->GetCurrentActivationInfo()));
        F.World->SetBegunPlay(true);
        for (int32 Step = 0; Step < 40 && UBatch6SlowComponent::GetMultiplier(F.Target) == 1.f; ++Step)
        {
            F.Step(.01f, true);
        }
        TestEqual(TEXT("Actual PM sphere contact reaches the original Actor and external Slow"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
        TestEqual(TEXT("Actor state-only contact does not execute hidden direct damage"), F.Health(), 100.f);
        TestEqual(TEXT("Actual contact consumes the offensive Actor inventory resource"), F.Count(), 0);
        F.Run->Cancel();
        TestEqual(TEXT("Cancelling owner leaves landed Actor status under its own target lifecycle"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeOrbitActualDefaultInventory,
    "test_25d.UnifiedGA.DynamicMage.PrototypeOrbit.ActualDefaultCompilerInventoryAndCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitActualDefaultInventory::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.bCompileDefault = true;
            if (!bActor)
            {
                F.SpawnMass(FVector(0, 100, 0));
            }
            auto* Config = F.Parameters();
            Config->OnPeriod = F.Burst(25.f);
            FString Error;
            if (!TestTrue(TEXT("Actual full compiler inventory starts through formal builtins"),
                          F.Start(*Config, bFine, bActor, true, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Step(.75f);
            TestEqual(TEXT("Actual default preserves one refill per frame after hitch"), F.Count(), 1);
            TestEqual(TEXT("Actual default period child applies real Actor GE"), F.Health(), 90.f);
            TestEqual(TEXT("Actual default preserves prototype Mana profile"), F.Mana(), bActor ? 100.f : 98.5f);
            F.Step(.25f);
            F.Step(1.f);
            TestEqual(TEXT("Full default inventory remains bounded and holds two resources"), F.Count(), 2);
            TestEqual(TEXT("Full default inventory keeps period damage without creating an extra resource"), F.Health(),
                      70.f);
            TestEqual(TEXT("Default physical resources consume no additional logical scopes"), F.Binding->Spawned, 4);
            TestEqual(TEXT("Default does not replace prototype upkeep by nested inventory Mana37"), F.Mana(), 96.f);
            if (!bActor)
            {
                TestEqual(TEXT("Actual default period child also damages real Mass targets"), F.MassHealth(), 125.f);
            }
            F.Run->Cancel();
            TestEqual(TEXT("Actual default cancellation removes its entire inventory"), F.Count(), 0);
            TestEqual(TEXT("Actual default cancellation ends every graph invocation"),
                      F.Run->GetActiveInvocationCount(), 0);
            if (bFine)
            {
                TestEqual(TEXT("Actual compiled inventory uses and releases two physical resources"),
                          F.Run->GetPhysicalSpawnCountTotal(), uint64(2));
                TestEqual(TEXT("Actual compiled cancellation releases each precise lease"),
                          F.Run->GetActivePhysicalSpawnCount(), 0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeOrbitActualDefaultContact,
    "test_25d.UnifiedGA.DynamicMage.PrototypeOrbit.ActualDefaultCompilerContactHPForcesAndRNG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeOrbitActualDefaultContact::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    struct FObserved
    {
        int32 Count = 0;
        FVector Death = FVector::ZeroVector;
        FVector Knockback = FVector::ZeroVector;
        float Main = 0.f;
    };
    // 三个实际后端必须分别保留：Actor原生随机force、Mixed Actor保preset、Mixed Mass不额外掷击退。
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        const bool bActor = Mode == 0;
        const bool bMass = Mode == 2;
        FObserved Original;
        int32 OriginalNextRandom = 0;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.bCompileDefault = true;
            F.bUseForceInputs = true;
            F.ForceInputs.DeathImpulse = FVector(11, 22, 33);
            F.ForceInputs.KnockbackForce = FVector(-44, 55, 66);
            F.ForceInputs.DeathImpulseMagnitude = 250.f;
            F.ForceInputs.KnockbackForceMagnitude = 350.f;
            F.ForceInputs.KnockbackChance = 100.f;
            F.Target->SetActorLocation(bActor ? FVector(300, 0, 0) : FVector(100, 0, 0));
            if (bMass)
            {
                F.Target->SetActorLocation(FVector(2000, 0, 0));
                F.SpawnMass(FVector(100, 0, 0));
            }
            if (bActor)
            {
                F.Target->AutoPossessAI = EAutoPossessAI::Disabled;
                if (!F.Target->IsActorInitialized())
                {
                    F.Target->PostInitializeComponents();
                }
                F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
                F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
            }
            auto* Config = F.Parameters();
            Config->Damage = 25.f;
            Config->ManaPerSecond = 0.f;
            Config->OrbitSettings->MaxFireBolts = 1;
            Config->OrbitSettings->ProjectileSpeed = bActor ? 1000.f : 10000.f;
            FString Error;
            if (!TestTrue(TEXT("Real default contact graph starts"), F.Start(*Config, bFine, bActor, true, Error)))
            {
                AddError(Error);
                continue;
            }
            FObserved Actual;
            const auto EffectHandle =
                F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                    [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                    {
                        if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                        {
                            ++Actual.Count;
                            Actual.Death = UMyAbilitySystemLibrary::GetDeathImpulse(Spec.GetContext());
                            Actual.Knockback = UMyAbilitySystemLibrary::GetKnockbackForce(Spec.GetContext());
                            Actual.Main = Spec.GetSetByCallerMagnitude(Config->DamageType, false);
                        }
                    });
            EAbilityContactDamageProfile ConsumedProfile = EAbilityContactDamageProfile::Unified;
            bool bContactObserved = false;
            F.Run->OnEvent.AddLambda(
                [&](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType == TEXT("Delivery.PrototypeOrbit") && Event.OutputPort == TEXT("Contact") &&
                        Event.Input.Impact.IsSet())
                    {
                        ConsumedProfile = Event.Input.Impact->ContactDamageProfile;
                        bContactObserved = true;
                    }
                });
            F.Step(.25f);
            auto* Attack = F.Attack();
            if (!TestNotNull(TEXT("Default delivery has a real offensive ability source"), Attack))
            {
                F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
                continue;
            }
            if (bActor)
            {
                Attack->SetOffensiveAttackTarget(F.Target);
            }
            if (!TestTrue(TEXT("Real offensive commit succeeds"),
                          Attack->CommitAbility(F.AttackHandle, F.ASC->AbilityActorInfo.Get(),
                                                Attack->GetCurrentActivationInfo())))
            {
                F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
                continue;
            }
            if (!bActor)
            {
                APureMassRuntime::NotifyCommittedAttack(F.Source, Attack,
                                                        bMass ? FPureMassCombatTarget(F.MassId)
                                                              : FPureMassCombatTarget::FromActor(F.Target));
            }
            if (bActor)
            {
                F.World->SetBegunPlay(true);
            }
            constexpr int32 Seed = 601127;
            FMath::RandInit(Seed);
            for (int32 Step = 0; Step < 40 && (bMass ? F.MassHealth() == 200.f : Actual.Count == 0); ++Step)
            {
                F.Step(.01f, bActor);
            }
            const int32 NextRandom = FMath::Rand();
            TestEqual(TEXT("Actual default uses exactly one main damage owner"), bMass ? F.MassHealth() : F.Health(),
                      bMass ? 175.f : 90.f);
            TestEqual(TEXT("Actual default contact consumes the prepared inventory resource"), F.Count(), 0);
            if (!bFine)
            {
                Original = Actual;
                OriginalNextRandom = NextRandom;
                if (!bMass)
                {
                    TestEqual(TEXT("Original actual body contact produces one real GE"), Original.Count, 1);
                    TestEqual(TEXT("Original actual payload takes prototype main damage"), Original.Main, 25.f);
                    if (!bActor)
                    {
                        TestEqual(TEXT("Original Mixed Actor preserves authored impulse"), Original.Death,
                                  F.ForceInputs.DeathImpulse);
                        TestEqual(TEXT("Original Mixed Actor preserves authored knockback"), Original.Knockback,
                                  F.ForceInputs.KnockbackForce);
                    }
                }
            }
            else
            {
                TestTrue(TEXT("Actual default delivery exposes the typed contact used by its real damage owner"),
                         bContactObserved);
                const auto ExpectedProfile = bActor  ? EAbilityContactDamageProfile::NativeActorProjectile
                                             : bMass ? EAbilityContactDamageProfile::NativeMassProjectile
                                                     : EAbilityContactDamageProfile::ConfiguredForces;
                TestEqual(TEXT("Default graph explicitly routes the original force/damage backend"), ConsumedProfile,
                          ExpectedProfile);
                TestEqual(TEXT("Default graph consumes exactly the original RNG sequence"), NextRandom,
                          OriginalNextRandom);
                if (!bMass)
                {
                    TestEqual(TEXT("Default real GE count matches original body contact"), Actual.Count,
                              Original.Count);
                    TestEqual(TEXT("Default actual main set-by-caller matches original prototype"), Actual.Main,
                              Original.Main);
                    TestTrue(TEXT("Default death impulse matches original actual contact direction/preset"),
                             Actual.Death.Equals(Original.Death, .01f));
                    TestTrue(TEXT("Default knockback matches original actual contact direction/preset"),
                             Actual.Knockback.Equals(Original.Knockback, .01f));
                }
            }
            F.Run->Cancel();
            TestEqual(TEXT("Default graph cancel cannot deliver a late second hit"),
                      bMass ? F.MassHealth() : F.Health(), bMass ? 175.f : 90.f);
            if (bFine)
            {
                TestEqual(TEXT("No actual contact resource survives default cancellation"),
                          F.Run->GetActivePhysicalSpawnCount(), 0);
            }
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeMixedOrbitContactNiagara,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.MixedOrbitContactActualNiagara",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FPrototypeMixedOrbitContactNiagara::RunTest(const FString&)
{
    using namespace AbilityPrototypeOrbitNodeTests;
    auto* Asset = AbilityPrototypeNiagaraTestFixture::System();
    for (const bool bMass : {false, true})
    {
        TArray<FTransform> Original;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            if (!AbilityPrototypeNiagaraTestFixture::Probe(*this, *F.World, Asset))
            {
                return false;
            }
            F.bCompileDefault = true;
            F.Target->SetActorLocation(FVector(100, 0, 0));
            if (bMass)
            {
                F.Target->SetActorLocation(FVector(2000, 0, 0));
                F.SpawnMass(FVector(100, 0, 0));
            }
            auto* Config = F.Parameters();
            Config->Damage = 25.f;
            Config->ManaPerSecond = 0.f;
            Config->LoopVFX = nullptr;
            Config->ImpactVFX = Asset;
            Config->ImpactVFXScale = FVector(3, 4, 5);
            Config->OrbitSettings->MaxFireBolts = 1;
            Config->OrbitSettings->ProjectileSpeed = 10000.f;
            FString Error;
            if (!TestTrue(TEXT("Actual default Mixed Orbit contact-presentation graph starts"),
                          F.Start(*Config, bFine, false, true, Error)))
            {
                AddError(Error);
                return false;
            }
            // 临时World不整体BeginPlay/AI，只让真实视觉宿主完成与正常游戏一致的准备。
            for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
            {
                if (!It->HasActorBegunPlay())
                {
                    It->DispatchBeginPlay();
                }
            }
            const auto EffectHandle =
                F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                    [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                    {
                        if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                        {
                            F.Target->SetActorLocation(FVector(500, 0, 0));
                        }
                    });
            F.Step(.25f);
            TestEqual(TEXT("Inventory refill creates no implicit contact particle"),
                      AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset).Num(), 0);
            auto* Attack = F.Attack();
            if (!TestNotNull(TEXT("Actual offensive source exists before committed contact"), Attack) ||
                !TestTrue(TEXT("Actual offensive GAS commit succeeds"),
                          Attack->CommitAbility(F.AttackHandle, F.ASC->AbilityActorInfo.Get(),
                                                Attack->GetCurrentActivationInfo())))
            {
                F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
                return false;
            }
            APureMassRuntime::NotifyCommittedAttack(
                F.Source, Attack, bMass ? FPureMassCombatTarget(F.MassId) : FPureMassCombatTarget::FromActor(F.Target));
            F.Step(.01f);
            TestEqual(TEXT("The actual contact consumes one inventory resource"), F.Count(), 0);
            TestEqual(TEXT("The actual contact has exactly the original damage effect"),
                      bMass ? F.MassHealth() : F.Health(), bMass ? 175.f : 90.f);
            const auto Actual = AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset);
            if (!bFine)
            {
                Original = Actual;
                TestEqual(TEXT("Original Mixed Orbit creates a real native Niagara on actual contact"), Original.Num(),
                          1);
                TestTrue(
                    TEXT("Original Mixed contact uses pre-GE target position and base unit scale"),
                    AbilityPrototypeNiagaraTestFixture::Contains(Original, FVector(100, 0, 0), FVector::OneVector));
            }
            else
            {
                TestEqual(TEXT("Fine default Orbit preserves real contact Niagara count independently of finish"),
                          Actual.Num(), Original.Num());
                for (const auto& Expected : Original)
                {
                    TestTrue(TEXT("Fine contact component preserves original target snapshot/scale"),
                             AbilityPrototypeNiagaraTestFixture::Contains(Actual, Expected.GetLocation(),
                                                                          Expected.GetScale3D()));
                }
            }
            // 不在采样前取消，避免把Mixed原来的结束粒子混成接触粒子。
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
        }
    }
    return true;
}
#endif
