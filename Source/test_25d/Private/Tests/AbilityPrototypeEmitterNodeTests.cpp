// 调用链：Standalone Runtime / 当前 LegacyGraph / 独立发射细图 -> 真子Burst -> Actor GE或Mass实际属性。
// 三列不能混为同一合同：Standalone的Spawn成功和LegacyGraph的helper成功在64边界不同。
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeEmitterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeEmitterNodeTests
{
    enum class EPath : uint8
    {
        Standalone,
        LegacyGraph,
        Fine,
        ActualDefaultFine
    };

    const TCHAR* Name(EPath Path)
    {
        return Path == EPath::Standalone    ? TEXT("Standalone")
               : Path == EPath::LegacyGraph ? TEXT("LegacyGraph")
               : Path == EPath::Fine        ? TEXT("ManualFine")
                                            : TEXT("ActualDefaultFine");
    }

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
        AActor* RootHost = nullptr;
        EPath Path = EPath::Standalone;
        bool bActor = true;
        int32 MassId = INDEX_NONE;

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** Actor仅胶囊参与旧物理broadphase；Mixed改用真实Mass以验证独立伤害后端。 */
        void Targets(bool bActorProfile, FVector Position = FVector(200, 0, 0))
        {
            bActor = bActorProfile;
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         100.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Target))
            {
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            if (bActor)
            {
                Target->SetActorLocation(Position);
                Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
                Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
            }
            else
            {
                Target->SetActorLocation(FVector(2000, 0, 0));
                auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
                Sim->Enable();
                auto* Type = NewObject<UPureMassMonsterType>(World);
                Type->Health = 200.f;
                Type->XPReward = 0;
                Type->Speed = 0.f;
                Type->Radius = 2.f;
                MassId = Sim->SpawnMonster(Type, Position);
            }
        }

        float Health() const
        {
            if (bActor)
            {
                return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
            }
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) ? View.Health : -1.f;
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
            Result->Kind = EAbilityPrototypeKind::Emitter;
            Result->Count = 3;
            Result->Period = .1f;
            Result->Duration = 2.f;
            Result->OnPeriod = Burst(25.f);
            return Result;
        }

        /** 图子片段由现有Compiler组装；只调整实际逻辑深度，不复制第二套伤害执行。 */
        UAbilityGraphDefinition* Fine(UAbilityPrototype& Config, FString& Error)
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
            auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Scope->Parameters = &Config;
            Scope->Profile = Definition->CompatibilityProfile;
            Scope->LogicalRejectionPolicy = EAbilityPrototypeLogicalRejectionPolicy::FailExecution;
            const int32 Root = Add(*Definition, TEXT("Scope.Prototype"), Scope);
            const int32 Emitter = Add(*Definition, TEXT("Control.PrototypeEmitter"),
                                      NewObject<UAbilityPrototypeEmitterNodeConfig>(Definition));
            Definition->EntryNodeId = Definition->Nodes[Root].NodeId;
            Connect(*Definition, Definition->Nodes[Root].NodeId, TEXT("Ready"), Definition->Nodes[Emitter].NodeId);
            for (const bool bExpiry : {false, true})
            {
                auto* ParametersToCompile = bExpiry ? Config.OnExpire.Get() : Config.OnPeriod.Get();
                if (!ParametersToCompile)
                {
                    continue;
                }
                auto* Child = AbilityPrototypeFineCompiler::Compile(Definition, ParametersToCompile,
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
                        ChildScope->LogicalRejectionPolicy = EAbilityPrototypeLogicalRejectionPolicy::FailExecution;
                    }
                }
                Definition->Nodes.Append(Child->Nodes);
                Definition->Edges.Append(Child->Edges);
                Connect(*Definition, Definition->Nodes[Emitter].NodeId, bExpiry ? TEXT("Expired") : TEXT("Launch"),
                        Child->EntryNodeId);
            }
            return Definition;
        }

        /** 捕获来源并启动生产原路径/细图；只接受未立即Failed的图，避免空路径作为对照。 */
        bool Start(EPath InPath, UAbilityPrototype& Config, int32 InitialSpawns, FString& Error)
        {
            Path = InPath;
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->CastId = FGuid::NewGuid();
            Binding->Level = 1;
            Binding->Spawned = InitialSpawns;
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            if (!Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            if (Path == EPath::Standalone)
            {
                RootHost = bActor ? static_cast<AActor*>(APrototypeRuntime::Spawn(Binding, &Config, FVector::ZeroVector,
                                                                                  FVector::ForwardVector, nullptr))
                                  : static_cast<AActor*>(APureMassRuntime::Spawn(Binding, &Config, FVector::ZeroVector,
                                                                                 FVector::ForwardVector));
                if (RootHost)
                {
                    RootHost->DispatchBeginPlay();
                }
                return RootHost != nullptr;
            }
            const FName Profile = bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1");
            auto* Definition = Path == EPath::Fine ? Fine(Config, Error)
                               : Path == EPath::ActualDefaultFine
                                   ? AbilityPrototypeFineCompiler::Compile(World, &Config, Profile, Error)
                                   : UAbilityLegacyGraphCompiler::Compile(World, &Config, Profile, Error);
            if (!Definition)
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Binding->GetSourceLife();
            Input.Context = Binding;
            Input.Direction = FVector::ForwardVector;
            Input.DamageParams = Binding->DamageParams;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            AbilityPrototypeEmitterNodes::Register(Registry);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            Run = NewObject<UAbilityGraphInstance>(World);
            const bool bAccepted = Run->StartWithInput(Definition, Input, Options, Registry, Error);
            if (bActor)
            {
                for (TActorIterator<APrototypeRuntime> It(World); It; ++It)
                {
                    if (Path == EPath::Fine || Path == EPath::ActualDefaultFine ||
                        (It->Prototype && It->Prototype->Kind == EAbilityPrototypeKind::Emitter))
                    {
                        RootHost = *It;
                        break;
                    }
                }
            }
            else
            {
                for (TActorIterator<APureMassRuntime> It(World); It; ++It)
                {
                    if (Path == EPath::Fine || Path == EPath::ActualDefaultFine ||
                        (It->Prototype && It->Prototype->Kind == EAbilityPrototypeKind::Emitter))
                    {
                        RootHost = *It;
                        break;
                    }
                }
            }
            return bAccepted && !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }

        /** 临时World未BeginPlay，显式启动真正子Burst；不把未启动的Standalone子对象当无伤害基准。 */
        void Advance(float Delta)
        {
            ++GFrameCounter;
            if (Run)
            {
                Run->Advance(Delta);
                return;
            }
            if (RootHost)
            {
                RootHost->Tick(Delta);
            }
            for (TActorIterator<APrototypeRuntime> It(World); It; ++It)
            {
                if (*It != RootHost)
                {
                    if (!It->HasActorBegunPlay())
                    {
                        It->DispatchBeginPlay();
                    }
                    It->Tick(Delta);
                }
            }
            for (TActorIterator<APureMassRuntime> It(World); It; ++It)
            {
                if (*It != RootHost)
                {
                    if (!It->HasActorBegunPlay())
                    {
                        It->DispatchBeginPlay();
                    }
                    It->Tick(Delta);
                }
            }
        }

        bool RootFinished() const
        {
            if (const auto* ActorHost = Cast<APrototypeRuntime>(RootHost))
            {
                return ActorHost->IsFinished();
            }
            const auto* MixedHost = Cast<APureMassRuntime>(RootHost);
            return MixedHost && MixedHost->IsFinished();
        }
        bool GraphFailed() const
        {
            return Run && Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed;
        }
    };
} // namespace AbilityPrototypeEmitterNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeEmitterNormalProfiles,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeEmitter.ThreeColumnsRealPayloadAndCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeEmitterNormalProfiles::RunTest(const FString&)
{
    using namespace AbilityPrototypeEmitterNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const EPath Path : {EPath::Standalone, EPath::LegacyGraph, EPath::Fine, EPath::ActualDefaultFine})
        {
            FWorld F;
            F.Targets(bActor);
            auto* Config = F.Parameters();
            Config->OnExpire = F.Burst(7.f);
            FString Error;
            if (!TestTrue(FString(Name(Path)) + TEXT(" actual source/root starts"), F.Start(Path, *Config, 0, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Advance(.35f);
            TestEqual(FString(Name(Path)) + TEXT(" real independently assembled child damage"), F.Health(),
                      bActor ? 60.f : 118.f);
            TestEqual(FString(Name(Path)) + TEXT(" root+3period+expiry logical fragments"), F.Binding->Spawned, 5);
            TestTrue(FString(Name(Path)) + TEXT(" finite count closes emitter before duration"), F.RootFinished());
            F.Advance(.2f);
            TestFalse(FString(Name(Path)) + TEXT(" normal admission never fails its graph"), F.GraphFailed());
            if (F.Run)
            {
                TestTrue(FString(Name(Path)) + TEXT(" child lifetimes retire after their actual duration"),
                         F.Run->IsFinished());
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeEmitterIdleAndTail,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeEmitter.ThreeColumnsIdleClockAndFinalTail",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeEmitterIdleAndTail::RunTest(const FString&)
{
    using namespace AbilityPrototypeEmitterNodeTests;
    // 移动的是Actor目标，两种profile都可观测；Mixed真实Mass伤害已由上一用例覆盖。
    for (const bool bActor : {true, false})
    {
        for (const EPath Path : {EPath::Standalone, EPath::LegacyGraph, EPath::Fine, EPath::ActualDefaultFine})
        {
            FWorld F;
            F.Targets(true, FVector(1000, 0, 0));
            F.bActor = bActor;
            auto* Config = F.Parameters();
            Config->Count = 16;
            // 使用可精确表示的时间，隔离真正profile差异，避免把十进制阈值舍入误当新回归。
            Config->Period = .25f;
            Config->Duration = 1.25f;
            FString Error;
            if (!TestTrue(FString(Name(Path)) + TEXT(" idle source/root starts"), F.Start(Path, *Config, 0, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Advance(.375f);
            TestEqual(FString(Name(Path)) + TEXT(" idle does not create a logical child"), F.Binding->Spawned, 1);
            F.Target->SetActorLocation(FVector(200, 0, 0));
            F.Advance(.125f);
            TestEqual(FString(Name(Path)) + TEXT(" Actor fmod remainder/Mixed zero idle clock remain distinct"),
                      F.Binding->Spawned, bActor ? 2 : 1);
            F.Advance(1.f);
            TestEqual(FString(Name(Path)) + TEXT(" Actor consumes clipped tail; ordinary Mixed exits first"),
                      F.Binding->Spawned, bActor ? 5 : 1);
            TestEqual(FString(Name(Path)) + TEXT(" clock difference changes actual GE count"),
                      F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                      bActor ? 60.f : 100.f);
            TestTrue(FString(Name(Path)) + TEXT(" duration terminates emitter"), F.RootFinished());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeEmitterBudgetColumns,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeEmitter.ThreeColumnsBudget63And64Admission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeEmitterBudgetColumns::RunTest(const FString&)
{
    using namespace AbilityPrototypeEmitterNodeTests;
    for (const bool bActor : {true, false})
    {
        bool bOriginalGraphFailed = false;
        bool bOriginalGraphRootFinished = false;
        for (const EPath Path : {EPath::Standalone, EPath::LegacyGraph, EPath::Fine, EPath::ActualDefaultFine})
        {
            FWorld F;
            F.Targets(bActor);
            auto* Config = F.Parameters();
            Config->Count = 2;
            FString Error;
            if (!TestTrue(FString(Name(Path)) + TEXT(" root63 starts from62"), F.Start(Path, *Config, 62, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Advance(.35f);
            AddInfo(FString::Printf(TEXT("%s Actor=%d Spawned=%d RootFinished=%d GraphFailed=%d HP=%.1f"), Name(Path),
                                    bActor, F.Binding->Spawned, F.RootFinished(), F.GraphFailed(), F.Health()));
            TestEqual(FString(Name(Path)) + TEXT(" root63 permits exactly one real child64"), F.Binding->Spawned, 64);
            TestEqual(FString(Name(Path)) + TEXT(" refused later child cannot add actual damage"), F.Health(),
                      bActor ? 90.f : 175.f);
            if (Path == EPath::Standalone)
            {
                TestFalse(TEXT("Standalone actual spawn failure cannot satisfy Count2"), F.RootFinished());
            }
            else if (Path == EPath::LegacyGraph)
            {
                bOriginalGraphFailed = F.GraphFailed();
                bOriginalGraphRootFinished = F.RootFinished();
                TestTrue(TEXT("Current LegacyGraph helper admission satisfies Count2 before runtime refusal"),
                         bOriginalGraphRootFinished);
                TestTrue(TEXT("Current LegacyGraph reports failed child runtime acquisition at budget64"),
                         bOriginalGraphFailed);
            }
            else
            {
                TestEqual(TEXT("Default fine helper profile must explicitly account for original emitter lifetime"),
                          F.RootFinished(), bOriginalGraphRootFinished);
                TestEqual(TEXT("Default fine helper profile must not silently hide original failure policy"),
                          F.GraphFailed(), bOriginalGraphFailed);
            }
        }
    }
    return true;
}
#endif
