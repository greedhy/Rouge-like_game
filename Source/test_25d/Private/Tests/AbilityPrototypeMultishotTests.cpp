// 调用链：原 LegacyGraph/原独立 Runtime 与独立细图 -> 扇形单弹 -> 实际 Actor GE/Mass 属性和同深度/64准入。
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeMultishotTests
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
        TArray<AEnemyCharacter*> Actors;
        TArray<int32> MassIds;
        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }
        UAbilityPrototype* Burst(float Damage)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = EAbilityPrototypeKind::Burst;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = Damage;
            Result->DamagePerLevel = 0.f;
            Result->Cooldown = 0.f;
            Result->Duration = .1f;
            Result->Radius = 10.f;
            return Result;
        }
        UAbilityPrototype* Config()
        {
            auto* Result = Burst(40.f);
            Result->Kind = EAbilityPrototypeKind::Multishot;
            Result->Duration = 2.f;
            Result->Count = 3;
            Result->SpreadDegrees = 90.f;
            Result->Speed = 1000.f;
            Result->Range = 400.f;
            Result->CollisionRadius = 5.f;
            Result->OnHit = Burst(3.f);
            Result->OnExpire = Burst(5.f);
            return Result;
        }
        void Targets(bool bActor)
        {
            Target->SetActorLocation(FVector(2000, 0, 0));
            for (const float Angle : {-45.f, 0.f, 45.f})
            {
                const FVector Position = FVector::ForwardVector.RotateAngleAxis(Angle, FVector::UpVector) * 250.f;
                if (bActor)
                {
                    auto* Actor = World->SpawnActor<AEnemyCharacter>();
                    Actor->Tags = {TEXT("Enemy")};
                    Actor->SetActorLocation(Position);
                    auto* Component = Actor->GetAbilitySystemComponent();
                    Component->InitAbilityActorInfo(Actor, Actor);
                    Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
                    Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
                    Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
                    // 本例只验证胶囊方向。Native HealthBar 的500x500 UI碰撞面不是该测试的命中体。
                    for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Actor))
                    {
                        if (Primitive != Actor->GetCapsuleComponent())
                        {
                            Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                        }
                    }
                    Actor->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                    Actor->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
                    Actor->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
                    Actors.Add(Actor);
                }
                else
                {
                    auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
                    Sim->Enable();
                    auto* Type = NewObject<UPureMassMonsterType>(World);
                    Type->Health = 200.f;
                    Type->XPReward = 0;
                    Type->Speed = 0.f;
                    Type->Radius = 2.f;
                    Type->HalfHeight = 10.f;
                    MassIds.Add(Sim->SpawnMonster(Type, Position));
                }
            }
        }
        float Health(int32 Index, bool bActor) const
        {
            if (bActor)
            {
                return Actors[Index]->GetAbilitySystemComponent()->GetNumericAttribute(
                    UMyAttributeSet::GetHealthAttribute());
            }
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassIds[Index], View) ? View.Health : -1.f;
        }
        bool Context(int32 InitialSpawns, FString& Error)
        {
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->Level = 1;
            Binding->CastId = FGuid::NewGuid();
            Binding->Spawned = InitialSpawns;
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            return Binding->CaptureSourceLife(Error);
        }
        bool Start(UAbilityGraphDefinition& Definition, FString& Error)
        {
            FAbilityNodeInput Input;
            Input.SourceToken = SourceToken;
            Input.Context = Binding;
            Input.Direction = FVector::ForwardVector;
            Input.DamageParams = Binding->DamageParams;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            AbilityPrototypeDistributionNodes::Register(Registry);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition.CompatibilityProfile;
            Options.BudgetPolicy = Definition.BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            Run = NewObject<UAbilityGraphInstance>(World);
            const bool bAccepted = Run->StartWithInput(&Definition, Input, Options, Registry, Error);
            return bAccepted && !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }
    };

    /** 独立Scope0证明根合同；depth3测试再接实际Scope3容器，不伪造根验证。 */
    UAbilityGraphDefinition* Fine(FWorld& F, UAbilityPrototype& Parameters, bool bActor, int32 Depth, FString& Error)
    {
        auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
        Definition->SchemaVersion = 2;
        Definition->CompatibilityProfile = bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
        Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
        auto Scope = [&](UAbilityPrototype* Data, int32 LogicalDepth, bool bSingle)
        {
            auto* Config = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Config->Parameters = Data;
            Config->Profile = Definition->CompatibilityProfile;
            Config->LogicalDepth = LogicalDepth;
            Config->bSingleProjectile = bSingle;
            return Add(*Definition, TEXT("Scope.Prototype"), Config);
        };
        const int32 Container = Scope(&Parameters, Depth, false);
        Definition->EntryNodeId = Definition->Nodes[Container].NodeId;
        if (Depth > 0)
        {
            const int32 Root = Scope(F.Burst(0.f), 0, false);
            Definition->EntryNodeId = Definition->Nodes[Root].NodeId;
            Connect(*Definition, Definition->Nodes[Root].NodeId, TEXT("Ready"), Definition->Nodes[Container].NodeId);
        }
        const int32 Fan = Add(*Definition, TEXT("Distribution.PrototypeMultishot"),
                              NewObject<UAbilityPrototypeMultishotNodeConfig>(Definition));
        const int32 Pellet = Scope(&Parameters, Depth, true);
        const int32 Flight = Add(*Definition, TEXT("Delivery.PrototypeFlight"),
                                 NewObject<UAbilityPrototypeFlightNodeConfig>(Definition));
        auto* Payload = NewObject<UAbilityPrototypeDamageNodeConfig>(Definition);
        Payload->bConsumeInputScales = true;
        const int32 Damage = Add(*Definition, TEXT("Effect.PrototypeDamageBatch"), Payload);
        Connect(*Definition, Definition->Nodes[Container].NodeId, TEXT("Ready"), Definition->Nodes[Fan].NodeId);
        Connect(*Definition, Definition->Nodes[Fan].NodeId, TEXT("Pellet"), Definition->Nodes[Pellet].NodeId);
        Connect(*Definition, Definition->Nodes[Pellet].NodeId, TEXT("Ready"), Definition->Nodes[Flight].NodeId);
        Connect(*Definition, Definition->Nodes[Flight].NodeId, TEXT("Contact"), Definition->Nodes[Damage].NodeId);
        if (Depth < 3)
        {
            for (const bool bHit : {true, false})
            {
                auto* Child = AbilityPrototypeFineCompiler::Compile(
                    Definition, bHit ? Parameters.OnHit : Parameters.OnExpire, Definition->CompatibilityProfile, Error);
                if (!Child)
                {
                    return nullptr;
                }
                Definition->Nodes.Append(Child->Nodes);
                Definition->Edges.Append(Child->Edges);
                Connect(*Definition, Definition->Nodes[bHit ? Damage : Flight].NodeId,
                        bHit ? TEXT("DamageResolved") : TEXT("Expired"), Child->EntryNodeId);
            }
        }
        return Definition;
    }
} // namespace AbilityPrototypeMultishotTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeMultishotDirections,
                                 "test_25d.UnifiedGA.DynamicMage.Multishot.ActorMassDirectionsAndPelletHooks",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeMultishotDirections::RunTest(const FString&)
{
    using namespace AbilityPrototypeMultishotTests;
    for (const bool bActor : {true, false})
    {
        TArray<float> Baseline;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Targets(bActor);
            auto* Parameters = F.Config();
            FString Error;
            TestTrue(TEXT("Source context captured"), F.Context(0, Error));
            auto* Definition =
                bFine ? Fine(F, *Parameters, bActor, 0, Error)
                      : UAbilityLegacyGraphCompiler::Compile(
                            F.World, Parameters, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!TestNotNull(TEXT("Actual fan definition"), Definition))
            {
                AddInfo(Error);
                continue;
            }
            TestTrue(TEXT("Fan graph starts, not immediate Failed"), F.Start(*Definition, Error));
            AddInfo(Error);
            ++GFrameCounter;
            F.Run->Advance(.4f);
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (!bFine)
                {
                    Baseline.Add(F.Health(Index, bActor));
                    TestEqual(TEXT("Original three actual directions have root+independent hit/expiry payloads"),
                              Baseline[Index], bActor ? 80.f : 152.f);
                }
                else
                {
                    TestEqual(TEXT("Each fine physical pellet hits the matching actual target"),
                              F.Health(Index, bActor), Baseline[Index]);
                }
            }
            TestEqual(TEXT("Container has no own expiry: one container+3pellets+3hit+3expiry fragments"),
                      F.Binding->Spawned, 10);
            if (bFine)
            {
                TestEqual(TEXT("Three offensive physical admissions, no container/visual double charge"),
                          F.Run->GetPhysicalSpawnCountTotal(), uint64(3));
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeMultishotAdmission,
    "test_25d.UnifiedGA.DynamicMage.Multishot.SameLogicalDepthAndContainerPellet64Admission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeMultishotAdmission::RunTest(const FString&)
{
    using namespace AbilityPrototypeMultishotTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bDepthThree : {true, false})
        {
            for (const bool bFine : {false, true})
            {
                FWorld F;
                if (bDepthThree)
                {
                    F.Targets(bActor);
                }
                auto* Parameters = F.Config();
                FString Error;
                TestTrue(TEXT("Admission source captured"), F.Context(bDepthThree ? 0 : 62, Error));
                if (bFine)
                {
                    auto* Definition = Fine(F, *Parameters, bActor, bDepthThree ? 3 : 0, Error);
                    if (!TestNotNull(TEXT("Root0 proof plus requested logical container"), Definition))
                    {
                        continue;
                    }
                    TestTrue(TEXT("Bounded fan admission graph starts"), F.Start(*Definition, Error));
                }
                else
                {
                    if (bDepthThree)
                    {
                        TestTrue(TEXT("Independent original root reserves depth0"), F.Binding->ReserveSpawn(0));
                    }
                    AActor* Container =
                        bActor
                            ? static_cast<AActor*>(APrototypeRuntime::Spawn(F.Binding, Parameters, FVector::ZeroVector,
                                                                            FVector::ForwardVector, nullptr,
                                                                            bDepthThree ? 3 : 0))
                            : static_cast<AActor*>(APureMassRuntime::Spawn(F.Binding, Parameters, FVector::ZeroVector,
                                                                           FVector::ForwardVector, {},
                                                                           bDepthThree ? 3 : 0));
                    if (TestNotNull(TEXT("Original container"), Container))
                    {
                        Container->DispatchBeginPlay();
                    }
                }
                const int32 Expected = bDepthThree ? 5 : 64;
                TestEqual(TEXT("Same-depth3 pellets admitted; at62 container and exactly one pellet reach64"),
                          F.Binding->Spawned, Expected);
                if (bDepthThree)
                {
                    if (bFine)
                    {
                        ++GFrameCounter;
                        F.Run->Advance(.4f);
                    }
                    else
                    {
                        for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
                        {
                            if (!It->IsFinished())
                            {
                                if (!It->HasActorBegunPlay())
                                {
                                    It->DispatchBeginPlay();
                                }
                                It->Tick(.4f);
                            }
                        }
                        for (TActorIterator<APureMassRuntime> It(F.World); It; ++It)
                        {
                            if (!It->IsFinished())
                            {
                                if (!It->HasActorBegunPlay())
                                {
                                    It->DispatchBeginPlay();
                                }
                                It->Tick(.4f);
                            }
                        }
                    }
                    for (int32 Index = 0; Index < 3; ++Index)
                    {
                        TestEqual(
                            TEXT("Actual depth3 pellet damage occurs but its depth4 hit/expiry children are rejected"),
                            F.Health(Index, bActor), bActor ? 90.f : 160.f);
                    }
                    TestEqual(TEXT("Forbidden depth4 children never increase logical count"), F.Binding->Spawned,
                              Expected);
                }
                if (bFine)
                {
                    TestEqual(TEXT("Only admitted pellets reserve physical leases"),
                              F.Run->GetPhysicalSpawnCountTotal(), uint64(bDepthThree ? 3 : 1));
                    F.Run->Cancel();
                    TestEqual(TEXT("Cancel releases every admitted pellet lease"), F.Run->GetActivePhysicalSpawnCount(),
                              0);
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeDefaultMultishotAdmissionFailure,
    "test_25d.UnifiedGA.DynamicMage.Multishot.ActualDefaultCompilerPreservesLegacyGraphQuotaFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeDefaultMultishotAdmissionFailure::RunTest(const FString&)
{
    using namespace AbilityPrototypeMultishotTests;
    // 与上面的raw standalone测试分开：实际LegacyGraph在第二个pellet获取失败时结束整图。
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Targets(bActor);
            auto* Parameters = F.Config();
            FString Error;
            if (!TestTrue(TEXT("Actual default source context captured before intentional quota refusal"),
                          F.Context(62, Error)))
            {
                AddError(Error);
                continue;
            }
            const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
            auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Parameters, Profile, Error)
                                     : UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, Profile, Error);
            if (!TestNotNull(TEXT("Real default compiler produces quota-sensitive full graph"), Definition))
            {
                AddError(Error);
                continue;
            }
            const bool bAccepted = F.Start(*Definition, Error);
            TestFalse(TEXT("Actual full graph rejects runtime child acquisition after root63 and first pellet64"),
                      bAccepted);
            TestEqual(TEXT("Only root63 and the first real pellet64 reserve original context"), F.Binding->Spawned, 64);
            TestTrue(TEXT("Real default graph is terminal after failed child acquisition"), F.Run->IsFinished());
            TestEqual(TEXT("Default graph preserves failure rather than raw standalone skip semantics"),
                      F.Run->GetEndReason(), EAbilityGraphEndReason::Failed);
            F.Run->Advance(.4f);
            for (int32 Index = 0; Index < 3; ++Index)
            {
                TestEqual(TEXT("A failed full graph cannot leave its first pellet delivering later damage"),
                          F.Health(Index, bActor), bActor ? 100.f : 200.f);
            }
            if (bFine)
            {
                TestEqual(TEXT("Failure releases any admitted first-pellet physical lease"),
                          F.Run->GetActivePhysicalSpawnCount(), 0);
            }
            AddInfo(FString::Printf(TEXT("Default %s Actor=%d Start=%d End=%d Context=%d"),
                                    bFine ? TEXT("Fine") : TEXT("LegacyGraph"), bActor, bAccepted,
                                    static_cast<int32>(F.Run->GetEndReason()), F.Binding->Spawned));
        }
    }
    return true;
}
#endif
