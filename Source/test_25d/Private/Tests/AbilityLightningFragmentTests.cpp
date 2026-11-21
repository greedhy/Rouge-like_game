// 测试链：原注册闪电策略真实Actor/Mass对照 -> 细图同源参数 -> 独立载荷替换与128目标预算。
// 所有生命、扣血和光束均来自生产路径；不修改CDO/资产，不把 dormant BFS当成真实Fission前端。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityLightningTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityLightningFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace AbilityLightningFragmentTests
{
    UAbilityGraphDefinition* Macro(UObject* Outer, UAbilityLightningNodeConfig& Settings)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(Outer);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        const FGuid Id = FGuid::NewGuid();
        Result->EntryNodeId = Id;
        Result->Nodes = {{Id,
                          Settings.IsA<UAbilitySequentialChainNodeConfig>() ? TEXT("Delivery.SequentialChain")
                                                                            : TEXT("Delivery.ForkedVolley"),
                          DuplicateObject<UAbilityLightningNodeConfig>(&Settings, Result)}};
        return Result;
    }

    UAbilityLightningNodeConfig* Settings(UObject* Outer, bool bSequential)
    {
        UAbilityLightningNodeConfig* Result =
            bSequential ? static_cast<UAbilityLightningNodeConfig*>(NewObject<UAbilitySequentialChainNodeConfig>(Outer))
                        : static_cast<UAbilityLightningNodeConfig*>(NewObject<UAbilityForkedVolleyNodeConfig>(Outer));
        Result->MaxTargets = 4;
        Result->JumpRadius = 200.f;
        Result->DamageFalloff = .5f;
        return Result;
    }

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UPureMassSubsystem* Mass = nullptr;

        FWorld()
        {
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Target->SetActorLocation(FVector(300, 0, 0));
            auto* TargetASC = Target->GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        }

        int32 Spawn(FVector Position, float Health)
        {
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = Health;
            Type->Speed = 0.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Position);
        }

        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : -1.f;
        }

        FAbilityNodeInput Input(int32 First, float Damage = 25.f)
        {
            FAbilityNodeInput Result;
            Result.SourceToken = SourceToken;
            Result.Origin = Source->GetActorLocation();
            Result.DamageParams.SourceAbilitySystemComponent = ASC;
            Result.DamageParams.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
            Result.DamageParams.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
            Result.DamageParams.BaseDamage = Damage;
            // 原闪电明确关闭DOT，且不借投射物入口额外掷击退随机数。
            Result.DamageParams.DebuffChance = 100.f;
            FAbilityTargetToken Token;
            FString Error;
            AbilityExecutionIdentity::CaptureTarget(SourceToken, FPureMassCombatTarget(First), Token, Error);
            Result.LiveTargetToken = Token;
            return Result;
        }

        TArray<FBatch6ChainSegment> Segments() const
        {
            TArray<FBatch6ChainSegment> Result;
            for (TActorIterator<ABatch6ChainPresentation> It(World); It; ++It)
            {
                Result.Append(It->Segments);
            }
            return Result;
        }
    };

    bool Start(FAutomationTestBase& Test, FWorld& F, bool bFine, UAbilityLightningNodeConfig& Config,
               const FAbilityNodeInput& Input, UAbilityGraphInstance*& Out)
    {
        auto* Graph = bFine ? AbilityLightningFragments::Build(F.World, Config) : Macro(F.World, Config);
        if (!Test.TestNotNull(bFine ? TEXT("Fine graph exposes the real lightning axes")
                                    : TEXT("Original macro control definition exists"),
                              Graph))
        {
            return false;
        }
        Out = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        FAbilityExecutionOptions Options;
        Options.BudgetPolicy = Graph->BudgetPolicy;
        const bool bStarted = Out->StartWithInput(Graph, Input, Options, Error);
        Test.TestTrue(bFine ? TEXT("Fine default executes") : TEXT("Original macro control executes"), bStarted);
        if (!bStarted)
        {
            Test.AddInfo(Error);
        }
        return bStarted;
    }
} // namespace AbilityLightningFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningFineDefault,
                                 "test_25d.DynamicMage.SpecialLightning.MixedDefaultVolleyAndSequential",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningFineDefault::RunTest(const FString&)
{
    using namespace AbilityLightningFragmentTests;
    // 先完整运行两个旧控制；细图尚未实现也不能跳过另一模式的真实前置。
    for (const bool bFine : {false, true})
    {
        for (const bool bSequential : {false, true})
        {
            FWorld F;
            const int32 First = F.Spawn(FVector(150, 0, 0), 10.f);
            const int32 Third = F.Spawn(FVector(450, 0, 0), 1000.f);
            const int32 Fourth = F.Spawn(FVector(600, 0, 0), 1000.f);
            auto* Config = Settings(F.World, bSequential);
            auto Input = F.Input(First);
            UAbilityGraphInstance* Run = nullptr;
            if (!Start(*this, F, bFine, *Config, Input, Run))
            {
                continue;
            }
            TestEqual(TEXT("First actual Mass hit is lethal"), F.Health(First), -1.f);
            const auto ActorHealth = [&F]()
            {
                return F.Target->GetAbilitySystemComponent()->GetNumericAttribute(
                    UMyAttributeSet::GetHealthAttribute());
            };
            if (bSequential)
            {
                TestEqual(TEXT("Sequence waits before touching the Actor"), ActorHealth(), 1000.f);
                ++GFrameCounter;
                Run->Advance(.09f);
                TestEqual(TEXT("No early 90ms Actor damage"), ActorHealth(), 1000.f);
                ++GFrameCounter;
                Run->Advance(.011f);
                TestEqual(TEXT("Lethal first life does not stop delayed Actor hit"), ActorHealth(), 987.5f);
                TestEqual(TEXT("Third life still waits for the next interval"), F.Health(Third), 1000.f);
                ++GFrameCounter;
                Run->Advance(1.f);
                TestEqual(TEXT("Long frame advances exactly one hop and one further falloff"), F.Health(Third),
                          993.75f);
                TestEqual(TEXT("Long frame never catches up a fourth hit"), F.Health(Fourth), 1000.f);
                const auto Beams = F.Segments();
                TestEqual(TEXT("Three actual separate segments exist"), Beams.Num(), 3);
                TestTrue(TEXT("Later segment starts from historical previous hit"),
                         Beams.ContainsByPredicate(
                             [](const FBatch6ChainSegment& Segment)
                             {
                                 return Segment.Start.Equals(FVector(300, 0, 0)) &&
                                        Segment.End.Equals(FVector(450, 0, 0));
                             }));
                Run->Cancel();
                ++GFrameCounter;
                Run->Advance(1.f);
                TestEqual(TEXT("Cancel stops the pending fourth life"), F.Health(Fourth), 1000.f);
            }
            else
            {
                TestEqual(TEXT("Volley Actor receives one secondary falloff immediately"), ActorHealth(), 987.5f);
                TestEqual(TEXT("Volley never expands beyond the first neighborhood"), F.Health(Third), 1000.f);
                const auto Beams = F.Segments();
                TestEqual(TEXT("One cue batch contains exactly two segments"), Beams.Num(), 2);
                for (const auto& Segment : Beams)
                {
                    TestEqual(TEXT("Every volley beam starts at caster"), Segment.Start, Input.Origin);
                }
                TestTrue(TEXT("Volley completes synchronously"), Run->IsFinished());
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningFinePayload,
                                 "test_25d.DynamicMage.SpecialLightning.ReplaceOnlyPayloadWithRealPetrifyBatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningFinePayload::RunTest(const FString&)
{
    using namespace AbilityLightningFragmentTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        const int32 First = F.Spawn(FVector(150, 0, 0), 1000.f);
        auto* Config = Settings(F.World, false);
        auto* Graph = bFine ? AbilityLightningFragments::Build(F.World, *Config) : Macro(F.World, *Config);
        if (!TestNotNull(bFine ? TEXT("Replaceable fine graph exists") : TEXT("Original damage control exists"), Graph))
        {
            continue;
        }
        if (bFine)
        {
            for (const FName Axis :
                 {FName(TEXT("Control.LightningSteps")), FName(TEXT("Query.LightningTargets")),
                  FName(TEXT("Effect.LightningDamageBatch")), FName(TEXT("Presentation.LightningBeams"))})
            {
                TestTrue(TEXT("Actual query/control/payload/cue are explicit independent nodes"),
                         Graph->Nodes.ContainsByPredicate(
                             [Axis](const FAbilityGraphNode& Node)
                             {
                                 return Node.TypeId == Axis;
                             }));
            }
            auto* Sink = Graph->Nodes.FindByPredicate(
                [](const FAbilityGraphNode& Node)
                {
                    return Node.TypeId == TEXT("Effect.LightningDamageBatch");
                });
            if (!TestNotNull(TEXT("Only the actual payload is replaced"), Sink))
            {
                continue;
            }
            Sink->TypeId = TEXT("Status.PetrifyBatch");
            auto* Status = NewObject<UAbilityPetrifyBatchConfig>(Graph);
            Status->Duration = 2.f;
            Sink->Config = Status;
        }
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        FAbilityExecutionOptions Options;
        Options.BudgetPolicy = Graph->BudgetPolicy;
        if (!TestTrue(TEXT("Selected original/fine pipeline starts"),
                      Run->StartWithInput(Graph, F.Input(First), Options, Error)))
        {
            AddInfo(Error);
            continue;
        }
        FPureMassView View;
        F.Mass->Resolve(First, View);
        TestEqual(TEXT("Only replacing damage changes first Mass HP"), View.Health, bFine ? 1000.f : 975.f);
        TestEqual(TEXT("Only replacing damage changes Actor HP"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  bFine ? 1000.f : 987.5f);
        TestEqual(TEXT("Independent payload applies real Mass petrify"), View.bPetrified, bFine);
        TestEqual(TEXT("Independent payload applies real Actor petrify"), F.Target->GetMesh()->bPauseAnims != 0, bFine);
        TestEqual(TEXT("Payload replacement keeps both real beam segments"), F.Segments().Num(), 2);
        TestTrue(TEXT("Replacement still completes normally"), Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningFineFullRange,
                                 "test_25d.DynamicMage.SpecialLightning.All128TargetsAndMinimumHopDelay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningFineFullRange::RunTest(const FString&)
{
    using namespace AbilityLightningFragmentTests;
    for (const bool bFine : {false, true})
    {
        for (const bool bSequential : {false, true})
        {
            FWorld F;
            F.Target->Destroy();
            TArray<int32> Targets;
            for (int32 Index = 0; Index < 128; ++Index)
            {
                const FVector Position = bSequential ? FVector(100.f + Index * 20.f, 0, 0)
                                                     : FVector(100.f + (Index % 16) * 20.f, (Index / 16) * 20.f, 0);
                Targets.Add(F.Spawn(Position, 10.f));
            }
            auto* Config = Settings(F.World, bSequential);
            Config->MaxTargets = 128;
            Config->DamageFalloff = 1.f;
            // Volley原半径上限2000，128个点放入2D网格；Sequence仍用单条20cm邻接链。
            if (!bSequential)
            {
                Config->JumpRadius = 1000.f;
            }
            else
            {
                Config->JumpRadius = 25.f;
                CastChecked<UAbilitySequentialChainNodeConfig>(Config)->JumpDelay = .01f;
            }
            UAbilityGraphInstance* Run = nullptr;
            if (!Start(*this, F, bFine, *Config, F.Input(Targets[0], 1.f), Run))
            {
                continue;
            }
            if (bSequential)
            {
                for (int32 Step = 0; Step < 127; ++Step)
                {
                    ++GFrameCounter;
                    Run->Advance(.0101f);
                }
            }
            int32 ActualHits = 0;
            for (const int32 Id : Targets)
            {
                ActualHits += FMath::IsNearlyEqual(F.Health(Id), 9.f);
            }
            TestEqual(TEXT("All128 original lives take one real damage without silent64 truncation"), ActualHits, 128);
            TestEqual(TEXT("All128 real beam segments are preserved"), F.Segments().Num(), 128);
            // 原Volley同步完成全部GE/表现，但128个观测结果仍按每帧64事件排空；不把队列尾部当漏伤害。
            for (int32 Drain = 0; Drain < 4 && !Run->IsFinished(); ++Drain)
            {
                ++GFrameCounter;
                Run->Advance(0.f);
            }
            TestTrue(TEXT("Finite old range completes within its own host budget"), Run->IsFinished());
            TestEqual(TEXT("Only normal completion is accepted"), Run->GetEndReason(),
                      EAbilityGraphEndReason::Completed);
        }
    }
    return true;
}

namespace AbilityLightningFragmentTests
{
    /** 只在本测试注册TargetSet→TargetSet夹具；生产节点和所有实际副作用保持原实现。 */
    bool SubsetPipeline(FAutomationTestBase& Test, bool bPetrify, bool bInject)
    {
        FWorld F;
        const int32 First = F.Spawn(FVector(150, 0, 0), 1000.f);
        const int32 Extra = F.Spawn(FVector(900, 0, 0), 1000.f);
        auto* Original = Settings(F.World, false);
        auto* Graph = AbilityLightningFragments::Build(F.World, *Original);
        if (!Test.TestNotNull(TEXT("Proven real fine default exists for typed-set seam"), Graph))
        {
            return false;
        }
        auto* Query = Graph->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Query.LightningTargets");
            });
        auto* Sink = Graph->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.LightningDamageBatch");
            });
        if (!Query || !Sink)
        {
            Test.AddError(TEXT("Real fine axes are missing."));
            return false;
        }
        const FGuid QueryId = Query->NodeId;
        const FGuid SinkId = Sink->NodeId;
        if (bPetrify)
        {
            Sink->TypeId = TEXT("Status.PetrifyBatch");
            Sink->Config = NewObject<UAbilityPetrifyBatchConfig>(Graph);
        }
        const FGuid FilterId = FGuid::NewGuid();
        auto* Filter = NewObject<UAbilityLightningSubsetTestConfig>(Graph);
        Filter->InjectedEntityId = bInject ? Extra : INDEX_NONE;
        Graph->Nodes.Add({FilterId, TEXT("Test.LightningSubset"), Filter});
        for (auto& Edge : Graph->Edges)
        {
            if (Edge.From == QueryId && Edge.To == SinkId)
            {
                Edge.To = FilterId;
            }
        }
        Graph->Edges.Add({FilterId, SinkId, TEXT("Completed")});
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = TEXT("Test.LightningSubset");
        Descriptor.ConfigClass = UAbilityLightningSubsetTestConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityLightningSubsetTestExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::TargetSet;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bSustainedSafe = true;
        Descriptor.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
        FString Error;
        if (!Test.TestTrue(TEXT("Test-only subset adapter is registered explicitly"),
                           Registry.Register(Descriptor, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        Test.AddInfo(bInject
                         ? TEXT("SetContract=InjectedUncapturedLife; policy=reject entire payload before GE")
                         : TEXT("SetContract=LegitimateSubset; ordinal=original secondary, not new index0 primary"));
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityExecutionOptions Options;
        Options.BudgetPolicy = Graph->BudgetPolicy;
        if (!Test.TestTrue(TEXT("Real query and explicit typed-set adapter execute"),
                           Run->StartWithInput(Graph, F.Input(First), Options, Registry, Error)))
        {
            Test.AddInfo(Error);
            return false;
        }
        Test.TestEqual(TEXT("Excluded original primary receives no hidden damage"), F.Health(First), 1000.f);
        const float ExpectedActor = bPetrify || bInject ? 1000.f : 987.5f;
        Test.TestEqual(
            TEXT("Selected secondary retains its original falloff, and invalid injection is atomic"),
            F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
            ExpectedActor);
        Test.TestEqual(TEXT("New uncaptured life never inherits step authorization"), F.Health(Extra), 1000.f);
        if (bPetrify)
        {
            FPureMassView View;
            F.Mass->Resolve(First, View);
            Test.TestFalse(TEXT("Excluded primary never receives replacement state"), View.bPetrified);
            Test.TestTrue(TEXT("Selected Actor receives actual replacement state"), F.Target->GetMesh()->bPauseAnims);
        }
        const auto Beams = F.Segments();
        Test.TestEqual(TEXT("Cue follows actual typed recipients, not the stale full frame"), Beams.Num(),
                       bInject ? 0 : 1);
        if (!bInject && Beams.Num() == 1)
        {
            Test.TestEqual(TEXT("The one actual beam ends at selected Actor"), Beams[0].End, FVector(300, 0, 0));
        }
        return true;
    }
} // namespace AbilityLightningFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningTypedSubsetDamage,
                                 "test_25d.DynamicMage.SpecialLightning.TypedSubsetDamagePreservesOriginalOrdinal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningTypedSubsetDamage::RunTest(const FString&)
{
    AbilityLightningFragmentTests::SubsetPipeline(*this, false, false);
    AbilityLightningFragmentTests::SubsetPipeline(*this, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningTypedSubsetCue,
                                 "test_25d.DynamicMage.SpecialLightning.TypedSubsetPetrifyCueMatchesRecipients",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightningTypedSubsetCue::RunTest(const FString&)
{
    return AbilityLightningFragmentTests::SubsetPipeline(*this, true, false);
}
#endif
