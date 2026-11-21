// 调用链：Automation -> 原 LegacyGraph / 独立 Scope+Flight+Effect 细图 -> 原碰撞/真实 Actor GE 和 Mass 属性。
// 旧路径作为独立世界基线；新路径没有完整 Driver 宏。测试不改资产，不以原生 FireBolt 几何替代原型合同。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "UObject/GarbageCollection.h"

namespace AbilityPrototypeFlightNodeTests
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
        Edge.OutputPort = Port;
        Edge.To = To;
        Definition.Edges.Add(Edge);
    }

    /** 合并现有已验证的 Burst 细片段；不重复实现其伤害、寿命或 Scope 准入。 */
    FGuid AppendChild(UAbilityGraphDefinition& Definition, const UAbilityPrototype* Child, FName Profile,
                      FString& Error)
    {
        if (!Child)
        {
            return {};
        }
        auto* Fragment = AbilityPrototypeFineCompiler::Compile(&Definition, Child, Profile, Error);
        if (!Fragment)
        {
            return {};
        }
        Definition.Nodes.Append(Fragment->Nodes);
        Definition.Edges.Append(Fragment->Edges);
        return Fragment->EntryNodeId;
    }

    /** 新主片段手动连接，避免“compiler尚未接新kind”掩盖运动节点真实行为；子Burst仍走现有compiler。 */
    UAbilityGraphDefinition* Fine(UWorld* World, UAbilityPrototype& Parameters, bool bActor, FString& Error,
                                  bool bSingleProjectile = false)
    {
        auto* Definition = NewObject<UAbilityGraphDefinition>(World);
        Definition->SchemaVersion = 2;
        Definition->CompatibilityProfile = bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
        Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
        auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
        Scope->Parameters = &Parameters;
        Scope->Profile = Definition->CompatibilityProfile;
        Scope->bSingleProjectile = bSingleProjectile;
        const int32 Entry = Add(*Definition, TEXT("Scope.Prototype"), Scope);
        const bool bBeam = Parameters.Kind == EAbilityPrototypeKind::Beam;
        const int32 Motion =
            Add(*Definition, bBeam ? TEXT("Control.TargetChannel") : TEXT("Delivery.PrototypeFlight"),
                bBeam ? static_cast<UAbilityNodeConfig*>(NewObject<UAbilityTargetChannelNodeConfig>(Definition))
                      : static_cast<UAbilityNodeConfig*>(NewObject<UAbilityPrototypeFlightNodeConfig>(Definition)));
        auto* Effect = NewObject<UAbilityPrototypeDamageNodeConfig>(Definition);
        Effect->bConsumeInputScales = true;
        const int32 Damage = Add(*Definition, TEXT("Effect.PrototypeDamageBatch"), Effect);
        Connect(*Definition, Definition->Nodes[Entry].NodeId, TEXT("Ready"), Definition->Nodes[Motion].NodeId);
        Connect(*Definition, Definition->Nodes[Motion].NodeId, bBeam ? TEXT("Action") : TEXT("Contact"),
                Definition->Nodes[Damage].NodeId);
        Definition->EntryNodeId = Definition->Nodes[Entry].NodeId;
        if (Parameters.OnPeriod)
        {
            const auto Child = AppendChild(*Definition, Parameters.OnPeriod, Definition->CompatibilityProfile, Error);
            if (!Child.IsValid())
            {
                return nullptr;
            }
            Connect(*Definition, Definition->Nodes[Motion].NodeId, TEXT("Period"), Child);
        }
        if (Parameters.OnHit)
        {
            const auto Child = AppendChild(*Definition, Parameters.OnHit, Definition->CompatibilityProfile, Error);
            if (!Child.IsValid())
            {
                return nullptr;
            }
            Connect(*Definition, Definition->Nodes[Damage].NodeId, TEXT("DamageResolved"), Child);
        }
        if (Parameters.OnExpire)
        {
            const auto Child = AppendChild(*Definition, Parameters.OnExpire, Definition->CompatibilityProfile, Error);
            if (!Child.IsValid())
            {
                return nullptr;
            }
            Connect(*Definition, Definition->Nodes[Motion].NodeId, TEXT("Expired"), Child);
        }
        return Definition;
    }

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Binding = nullptr;
        // 旧用例保留固定-10对照；需要观察倍率的个别用例在Start冻结前显式选择真实SBC GE。
        TSubclassOf<UGameplayEffect> ActualDamageEffect = UArcaneShardTestDamageEffect::StaticClass();

        FWorld()
        {
            Prepare(*Target);
            Source->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            Source->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        static void Prepare(AEnemyCharacter& Actor)
        {
            auto* Component = Actor.GetAbilitySystemComponent();
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Actor.GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Actor.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Actor.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        }

        AEnemyCharacter* Other(FVector Position)
        {
            auto* Actor = World->SpawnActor<AEnemyCharacter>();
            Actor->Tags = {TEXT("Enemy")};
            Actor->SetActorLocation(Position);
            auto* Component = Actor->GetAbilitySystemComponent();
            Component->InitAbilityActorInfo(Actor, Actor);
            Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Prepare(*Actor);
            return Actor;
        }

        AActor* Box(FVector Position, bool bWall)
        {
            auto* Actor = World->SpawnActor<AActor>();
            auto* Component = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Component);
            Component->SetBoxExtent(FVector(8, 100, 100));
            Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Component->SetCollisionObjectType(bWall ? ECC_WorldStatic : ECC_WorldDynamic);
            Component->SetCollisionResponseToAllChannels(ECR_Overlap);
            Component->SetCollisionResponseToChannel(ECC_Visibility, bWall ? ECR_Block : ECR_Overlap);
            Component->RegisterComponent();
            Actor->SetActorLocation(Position);
            return Actor;
        }

        UAbilityPrototype* Config(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = 40.f;
            Result->DamagePerLevel = 0.f;
            Result->Cooldown = 0.f;
            Result->Duration = 2.f;
            Result->Period = .1f;
            Result->Speed = 1000.f;
            Result->Range = 400.f;
            Result->CollisionRadius = 5.f;
            Result->Radius = 10.f;
            Result->Count = 4;
            return Result;
        }

        UAbilityPrototype* Child(float Damage)
        {
            auto* Result = Config(EAbilityPrototypeKind::Burst);
            Result->Damage = Damage;
            Result->Duration = .1f;
            Result->Radius = 10.f;
            return Result;
        }

        int32 Mass(FVector Position, float InitialHealth = 200.f)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = InitialHealth;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            return Sim->SpawnMonster(Type, Position);
        }

        float MassHealth(int32 Id) const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) ? View.Health : -1.f;
        }

        float Health(AEnemyCharacter* Actor) const
        {
            return Actor->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }

        bool Start(UAbilityPrototype& Parameters, bool bFine, bool bActor, FString& Error,
                   FVector Origin = FVector::ZeroVector, bool bSingleProjectile = false)
        {
            auto* Definition =
                bFine ? Fine(World, Parameters, bActor, Error, bSingleProjectile)
                      : UAbilityLegacyGraphCompiler::Compile(
                            World, &Parameters, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!Definition)
            {
                return false;
            }
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->CastId = FGuid::NewGuid();
            Binding->Level = 1;
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass = ActualDamageEffect;
            // LegacyNode validates the already captured GA context before creating its runtime.
            if (!Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput InInput;
            InInput.SourceToken = SourceToken;
            InInput.LiveTargetToken = TargetToken;
            InInput.Context = Binding;
            InInput.DamageParams = Binding->DamageParams;
            InInput.Direction = FVector::ForwardVector;
            InInput.Origin = InInput.EventCenter = Origin;
            FAbilityNodeRegistry Nodes;
            Nodes.RegisterBuiltins();
            AbilityPrototypeFlightNodes::Register(Nodes);
            Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            const bool bStarted = Run->StartWithInput(Definition, InInput, Options, Nodes, Error);
            const bool bStrategyFailed = Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed;
            if (bStarted && bStrategyFailed)
            {
                Error = TEXT("Graph accepted its definition but its initial execution strategy failed");
            }
            if (!bStarted || bStrategyFailed)
            {
                UE_LOG(LogTemp, Warning, TEXT("[PrototypeFlightFixture] %s start failed: %s"),
                       *Definition->CompatibilityProfile.ToString(), *Error);
            }
            return bStarted && !bStrategyFailed;
        }

        void Advance(float Delta)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
        }
    };
} // namespace AbilityPrototypeFlightNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeActorFlightGeometry,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.ActorFriendlyTriggerAndWallParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeActorFlightGeometry::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    for (const bool bWallFirst : {false, true})
    {
        float Baseline = 0.f;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Target->SetActorLocation(FVector(250, 0, 0));
            auto* Friendly = F.Other(FVector(80, 0, 0));
            Friendly->Tags = {TEXT("Player")};
            Friendly->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            F.Box(FVector(130, 0, 0), false);
            F.Box(FVector(bWallFirst ? 180 : 350, 0, 0), true);
            auto* Parameters = F.Config(EAbilityPrototypeKind::Projectile);
            FString Error;
            TestTrue(TEXT("Both geometry graphs start"), F.Start(*Parameters, bFine, true, Error));
            AddInfo(Error);
            F.Advance(.4f);
            if (!bFine)
            {
                Baseline = F.Health(F.Target);
                TestEqual(TEXT("Original Actor query ignores friendly/trigger but respects earlier wall"), Baseline,
                          bWallFirst ? 100.f : 90.f);
            }
            else
            {
                TestEqual(TEXT("Fine flight reaches the same actual Actor Health"), F.Health(F.Target), Baseline);
                TestTrue(TEXT("First impact/wall ends fine flight normally"), F.Run->IsFinished());
                TestEqual(TEXT("One prototype logical flight, no native extra projectile spawn"), F.Binding->Spawned,
                          1);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePiercingMassParity,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.MixedPiercingFalloffAndIndependentChildren",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePiercingMassParity::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    TArray<float> Baseline;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(2000, 0, 0));
        const int32 First = F.Mass(FVector(100, 0, 0));
        const int32 Second = F.Mass(FVector(250, 0, 0));
        auto* Parameters = F.Config(EAbilityPrototypeKind::Piercing);
        Parameters->Count = 2;
        Parameters->DamageFalloff = .5f;
        Parameters->OnHit = F.Child(5.f);
        FString Error;
        TestTrue(TEXT("Both piercing graphs start"), F.Start(*Parameters, bFine, false, Error));
        AddInfo(Error);
        F.Advance(.4f);
        const TArray<float> Health = {F.MassHealth(First), F.MassHealth(Second)};
        if (!bFine)
        {
            Baseline = Health;
            TestEqual(TEXT("Original first contact plus independent child damage"), Health[0], 155.f);
            TestEqual(TEXT("Original next contact falls off but its child remains fixed5"), Health[1], 175.f);
        }
        else
        {
            TestEqual(TEXT("Fine first actual Mass result matches original"), Health[0], Baseline[0]);
            TestEqual(TEXT("Fine falloff and independent child result matches original"), Health[1], Baseline[1]);
            TestEqual(TEXT("Only root plus two actual child fragments consume logical budget"), F.Binding->Spawned, 3);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeReturningRemainderParity,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.ReturningSameFrameRemainderAndTwoPhaseHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeReturningRemainderParity::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    float Baseline = 0.f;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(2000, 0, 0));
        const int32 Victim = F.Mass(FVector(100, 0, 0));
        auto* Parameters = F.Config(EAbilityPrototypeKind::Returning);
        Parameters->DamageFalloff = .5f;
        Parameters->OnHit = F.Child(7.f);
        Parameters->OnExpire = F.Child(9.f);
        FString Error;
        TestTrue(TEXT("Both returning graphs start"), F.Start(*Parameters, bFine, false, Error));
        AddInfo(Error);
        F.Advance(.85f);
        if (!bFine)
        {
            Baseline = F.MassHealth(Victim);
            TestEqual(TEXT("Original consumes outbound+return remainder and both fixed7 children"), Baseline, 126.f);
            TestEqual(TEXT("Original has two hits and one expiry child"), F.Binding->Spawned, 4);
        }
        else
        {
            TestEqual(TEXT("Fine return visits the same original life once per phase"), F.MassHealth(Victim), Baseline);
            TestEqual(TEXT("Fine has exact same hit/expiry logical fragments"), F.Binding->Spawned, 4);
            // Caller integration regression; physical lease semantics have an independent existing RED/GREEN suite.
            TestEqual(TEXT("Integration regression: both phases share one physical admission"),
                      F.Run->GetPhysicalSpawnCountTotal(), uint64(1));
            TestEqual(TEXT("Integration regression: returned flight releases its exact physical lease"),
                      F.Run->GetActivePhysicalSpawnCount(), 0);
        }
        F.Advance(.2f);
        TestTrue(TEXT("Return and its finite child presentations eventually close"), F.Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeTinyReturnTurn, "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.TinyTurnRemainderKeepsBothActualPasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeTinyReturnTurn::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Source->SetActorLocation(FVector(-100, 0, 0));
            F.Target->SetActorLocation(FVector(200, 0, 0));
            F.ActualDamageEffect = UPureMassIncomingDamageEffect::StaticClass();
            auto* Parameters = F.Config(EAbilityPrototypeKind::Returning);
            Parameters->DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
            Parameters->Speed = Parameters->Range = 400.f;
            Parameters->Damage = 25.f;
            Parameters->DamageFalloff = .5f;
            Parameters->Duration = 5.f;
            FString Error;
            if (!TestTrue(TEXT("Real original/fine returning profile starts"),
                          F.Start(*Parameters, bFine, bActor, Error)))
            {
                AddError(Error);
                return false;
            }
            F.Advance(.6f);
            TestEqual(TEXT("Actual outbound contact damages once"), F.Health(F.Target), 75.f);
            F.Advance(.4f);
            TestFalse(TEXT("Tiny turn remainder cannot finish while the caster is500cm away"), F.Run->IsFinished());
            F.Advance(.6f);
            TestEqual(TEXT("Actual return still reaches the original target with authored attenuation"),
                      F.Health(F.Target), 62.5f);
            F.Advance(.1f);
            TestEqual(TEXT("Remaining overlap does not repeat the return contact"), F.Health(F.Target), 62.5f);
            F.Source->SetActorLocation(FVector(-100, 100, 0));
            for (int32 Index = 0; Index < 30 && !F.Run->IsFinished(); ++Index)
            {
                F.Advance(.1f);
            }
            TestTrue(TEXT("Only actual return to the moved source closes the flight"), F.Run->IsFinished());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeFlightActualGECancel,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.MixedActualGECancellationStopsLaterContacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFlightActualGECancel::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(100, 0, 0));
        auto* Later = F.Other(FVector(220, 0, 0));
        const int32 Mass = F.Mass(FVector(330, 0, 0));
        auto* Parameters = F.Config(EAbilityPrototypeKind::Piercing);
        int32 Effects = 0;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const auto Handle = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                {
                    ++Effects;
                    F.Run->Cancel();
                }
            });
        FString Error;
        TestTrue(TEXT("Both cancellable mixed flights start"), F.Start(*Parameters, bFine, false, Error));
        AddInfo(Error);
        F.Advance(.4f);
        TestEqual(TEXT("First collision settles one actual GE before cancellation"), Effects, 1);
        TestEqual(TEXT("Applied first damage remains after cancellation"), F.Health(F.Target), 90.f);
        TestEqual(TEXT("Later Actor collision cannot damage after cancellation"), F.Health(Later), 100.f);
        TestEqual(TEXT("Later Mass collision cannot damage after cancellation"), F.MassHealth(Mass), 200.f);
        TestTrue(TEXT("Actual GE callback ends the root"), F.Run->IsFinished());
        TestEqual(TEXT("No business child admitted after cancellation"), F.Binding->Spawned, 1);
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeActorBeamInitialOrigin,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlight.ActorBeamInitialOriginOnFirstFrameDeadline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeActorBeamInitialOrigin::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    for (const bool bActor : {true, false})
    {
        float Baseline = 0.f;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            const FVector ChildOrigin(900, 50, 0);
            auto* ExpiryVictim = F.Other(ChildOrigin);
            auto* Parameters = F.Config(EAbilityPrototypeKind::Beam);
            Parameters->Duration = .1f;
            Parameters->OnExpire = F.Child(10.f);
            Parameters->OnExpire->Radius = 40.f;
            FString Error;
            TestTrue(TEXT("Both child-origin Beam graphs start"),
                     F.Start(*Parameters, bFine, bActor, Error, ChildOrigin));
            AddInfo(Error);
            F.Advance(.2f);
            if (!bFine)
            {
                Baseline = F.Health(ExpiryVictim);
                TestEqual(TEXT("Original Actor keeps requested initial origin; Mixed starts at caster+30"), Baseline,
                          bActor ? 90.f : 100.f);
            }
            else
            {
                TestEqual(TEXT("First-frame deadline expiry payload preserves the original profile location"),
                          F.Health(ExpiryVictim), Baseline);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeMixedPeriodLogicalOrder,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeFlow.MixedPeriodWaitsForRootProjectileDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeMixedPeriodLogicalOrder::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    int32 BaselineAtKill = -1;
    int32 BaselineSpawns = -1;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(2000, 0, 0));
        const int32 Victim = F.Mass(FVector(100, 0, 0), 30.f);
        auto* Parameters = F.Config(EAbilityPrototypeKind::Projectile);
        Parameters->OnPeriod = F.Child(50.f);
        Parameters->OnPeriod->Radius = 200.f;
        Parameters->OnHit = F.Child(7.f);
        int32 LogicalCountAtActualKill = -1;
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        const auto Handle = Sim->OnMonsterRemoved.AddLambda(
            [&](int32 Id, FName, EPureMassRemovalReason Reason)
            {
                if (Id == Victim && Reason == EPureMassRemovalReason::Killed)
                {
                    LogicalCountAtActualKill = F.Binding ? F.Binding->Spawned : -1;
                }
            });
        FString Error;
        TestTrue(TEXT("Both root/period flow graphs start without immediate failure"),
                 F.Start(*Parameters, bFine, false, Error));
        AddInfo(Error);
        F.Advance(.25f);
        TestEqual(TEXT("Actual target is killed once"), F.MassHealth(Victim), -1.f);
        if (!bFine)
        {
            BaselineAtKill = LogicalCountAtActualKill;
            BaselineSpawns = F.Binding->Spawned;
            TestEqual(TEXT("Original synchronous root damage kills before any queued logical child starts"),
                      BaselineAtKill, 1);
            TestEqual(TEXT("Original admits root, two period children and one hit child"), BaselineSpawns, 4);
        }
        else
        {
            TestEqual(TEXT("Fine period children cannot overtake root projectile damage"), LogicalCountAtActualKill,
                      BaselineAtKill);
            TestEqual(TEXT("Fine preserves root hit child even when the target dies before deferred hooks"),
                      F.Binding->Spawned, BaselineSpawns);
        }
        Sim->OnMonsterRemoved.Remove(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeMixedHitLogicalOrder,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlow.PierceHitChildWaitsForRemainingRootContacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeMixedHitLogicalOrder::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    int32 BaselineSpawns = -1;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(2000, 0, 0));
        const int32 First = F.Mass(FVector(100, 0, 0), 50.f);
        const int32 Second = F.Mass(FVector(220, 0, 0), 50.f);
        auto* Parameters = F.Config(EAbilityPrototypeKind::Piercing);
        Parameters->Count = 2;
        Parameters->OnHit = F.Child(50.f);
        Parameters->OnHit->Radius = 200.f;
        FString Error;
        TestTrue(TEXT("Both per-contact hook graphs start without immediate failure"),
                 F.Start(*Parameters, bFine, false, Error));
        AddInfo(Error);
        F.Advance(.3f);
        TestEqual(TEXT("Actual first victim dies from a hit child"), F.MassHealth(First), -1.f);
        TestEqual(TEXT("Actual second victim also dies from the shared child area"), F.MassHealth(Second), -1.f);
        if (!bFine)
        {
            BaselineSpawns = F.Binding->Spawned;
            TestEqual(TEXT("Original settles both physical root contacts before the first child can remove the second"),
                      BaselineSpawns, 3);
        }
        else
        {
            TestEqual(
                TEXT("Fine cannot lose the second root contact/hit child by running the first logical child early"),
                F.Binding->Spawned, BaselineSpawns);
            TestEqual(TEXT("Count-two completion releases the physical flight on its original contact frame"),
                      F.Run->GetActivePhysicalSpawnCount(), 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeSinglePelletVisual,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeFlow.SinglePelletUsesActualFrozenNiagara",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeSinglePelletVisual::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    UNiagaraSystem* System = nullptr;
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                              TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo")})
    {
        const auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!Catalog)
        {
            continue;
        }
        for (const auto& Row : Catalog->AbilityInformation)
        {
            const auto* Ability = Row.Ability ? Cast<UPrototypeAbility>(Row.Ability->GetDefaultObject()) : nullptr;
            if (Ability && Ability->Prototype && Ability->Prototype->LoopVFX)
            {
                System = Ability->Prototype->LoopVFX;
                break;
            }
        }
        if (System)
        {
            break;
        }
    }
    if (!TestNotNull(TEXT("Existing directory supplies a stable actual loop system"), System))
    {
        return false;
    }
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            auto* Parameters = F.Config(EAbilityPrototypeKind::Multishot);
            Parameters->LoopVFX = System;
            FString Error;
            AActor* Host = nullptr;
            if (bFine)
            {
                TestTrue(TEXT("Fine single-pellet Scope and actual flight start"),
                         F.Start(*Parameters, true, bActor, Error, FVector::ZeroVector, true));
                AddInfo(Error);
                for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
                {
                    Host = *It;
                }
                for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
                {
                    Host = *It;
                }
            }
            else
            {
                F.Binding = NewObject<UPrototypeCastContext>(F.World);
                F.Binding->Caster = F.Source;
                F.Binding->CastId = FGuid::NewGuid();
                F.Binding->DamageParams.SourceAbilitySystemComponent = F.ASC;
                TestTrue(TEXT("Original single-pellet source context is captured"),
                         F.Binding->CaptureSourceLife(Error));
                Host =
                    bActor
                        ? static_cast<AActor*>(APrototypeRuntime::SpawnDriven(
                              F.Binding, Parameters, FVector::ZeroVector, FVector::ForwardVector, nullptr, 0, true, {}))
                        : static_cast<AActor*>(APureMassRuntime::SpawnDriven(F.Binding, Parameters, FVector::ZeroVector,
                                                                             FVector::ForwardVector, {}, 0, true, {}));
            }
            if (!TestNotNull(TEXT("One existing runtime owns the single pellet"), Host))
            {
                continue;
            }
            if (!Host->HasActorBegunPlay())
            {
                Host->DispatchBeginPlay();
            }
            bool bActualLoop = false;
            for (auto* Component : TInlineComponentArray<UNiagaraComponent*>(Host))
            {
                bActualLoop |= Component->GetFName() == TEXT("Visual") && Component->GetAsset() == System;
            }
            TestTrue(bFine ? TEXT("Fine frozen single flag must reach the actual Niagara component")
                           : TEXT("Original bSingle=true baseline actually binds the Niagara loop"),
                     bActualLoop);
            TestEqual(TEXT("Single pellet consumes one logical fragment, no fan container expansion"),
                      F.Binding->Spawned, 1);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeDeferredHookLifetime,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFlow.DeferredHooksSurviveGCAndCancelWithTheirRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeDeferredHookLifetime::RunTest(const FString&)
{
    using namespace AbilityPrototypeFlightNodeTests;
    for (const bool bCancel : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(100, 0, 0));
        auto* Later = F.Other(FVector(220, 0, 0));
        auto* Parameters = F.Config(EAbilityPrototypeKind::Projectile);
        Parameters->OnPeriod = F.Child(7.f);
        Parameters->OnPeriod->Radius = 500.f;
        bool bCallbackRan = false;
        int32 LogicalCountAtContact = -1;
        const auto Handle = F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                if (!bCallbackRan && Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                {
                    bCallbackRan = true;
                    LogicalCountAtContact = F.Binding->Spawned;
                    if (bCancel)
                    {
                        F.Run->Cancel();
                    }
                    // 此时 Period 入口已预留但尚未启动；保活必须来自实际队列，而非测试持有参数。
                    CollectGarbage(RF_NoFlags);
                }
            });
        FString Error;
        TestTrue(TEXT("Fine flight with deferred periodic hooks starts"), F.Start(*Parameters, true, false, Error));
        AddInfo(Error);
        F.Advance(.25f);
        TestTrue(TEXT("Actual root GE callback exercises the lifetime boundary"), bCallbackRan);
        TestEqual(TEXT("First target keeps exactly the original physical hit and admitted periodic effects"),
                  F.Health(F.Target), bCancel ? 90.f : 70.f);
        TestEqual(TEXT("Cancellation suppresses all deferred area payloads on a distinct target"), F.Health(Later),
                  bCancel ? 100.f : 80.f);
        TestEqual(TEXT("Only uncancelled logical hooks may start after the root contact round"),
                  bCancel ? LogicalCountAtContact : F.Binding->Spawned, bCancel ? 1 : 3);
        TestEqual(TEXT("Physical contact completion or cancellation releases the physical lease"),
                  F.Run->GetActivePhysicalSpawnCount(), 0);
        F.Advance(.3f);
        TestTrue(TEXT("No deferred reference or reservation keeps the root open"), F.Run->IsFinished());
        TestEqual(TEXT("All helper invocation ownership is retired"), F.Run->GetActiveInvocationCount(), 0);
        F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    return true;
}
#endif
