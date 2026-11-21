// 调用链：真实表现DA+不同玩法Config -> 实际authored拓扑 -> 旧控制/孤立展开 -> Mass实际伤害。
// 不使用原DA重编作为期待值；额外边/断开边均只存在实际Graph中，资产始终只读。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityAuthoredGraphFineCompilerTests
{
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        int32 Victim = INDEX_NONE;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Context = nullptr;

        FWorld()
        {
            Target->Destroy();
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->Speed = 0.f;
            Type->XPReward = 0;
            Victim = Mass->SpawnMonster(Type, FVector(10, 0, 0));
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        float Health() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Victim, View) ? View.Health : -1.f;
        }

        UAbilityGraphDefinition* Authored()
        {
            auto* Result = NewObject<UAbilityGraphDefinition>(World);
            Result->SchemaVersion = 2;
            Result->CompatibilityProfile = TEXT("LegacyMixed.v1");
            Result->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
            return Result;
        }

        UAbilityPrototype* Presentation()
        {
            return LoadObject<UAbilityPrototype>(
                nullptr,
                TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Burst.DA_Prototype_Burst"));
        }

        /** 同一个真实DA可呈现多个私有玩法版本，不能把原Asset当成每个节点的实际参数。 */
        FGuid Add(UAbilityGraphDefinition& Definition, UAbilityPrototype& Original, float Damage, int32 Depth)
        {
            auto* Config = NewObject<UAbilityLegacyNodeConfig>(&Definition);
            Config->Profile = Definition.CompatibilityProfile;
            Config->LegacyDepth = Depth;
            Config->PresentationParameters = &Original;
            Config->Parameters = DuplicateObject(&Original, Config);
            Config->Parameters->Damage = Damage;
            Config->Parameters->DamagePerLevel = 0.f;
            Config->Parameters->Radius = 20.f;
            Config->Parameters->Duration = .1f;
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = TEXT("Legacy.Mixed.Burst");
            Node.Config = Config;
            Definition.Nodes.Add(Node);
            if (!Definition.EntryNodeId.IsValid())
            {
                Definition.EntryNodeId = Node.NodeId;
            }
            return Node.NodeId;
        }

        bool Start(UAbilityGraphDefinition* Definition, FString& Error, TSharedPtr<TArray<FVector>> Admitted = nullptr)
        {
            Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            if (!Context->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            Run = NewObject<UAbilityGraphInstance>(World);
            if (Admitted)
            {
                Run->OnEvent.AddLambda(
                    [Admitted](const FAbilityGraphEvent& Event)
                    {
                        if ((Event.NodeType == TEXT("Legacy.Mixed.Burst") && Event.OutputPort == TEXT("OnHit")) ||
                            (Event.NodeType == TEXT("Event.OriginalImpact") && Event.OutputPort == TEXT("Forward")))
                        {
                            Admitted->Add(Event.Input.Origin);
                        }
                    });
            }
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }
    };

    /** 每次规范化都调用待测入口，并拒绝宏换名，不以原DA重新编译绕过实际图。 */
    UAbilityGraphDefinition* Expand(FAutomationTestBase& Test, FWorld& F, UAbilityGraphDefinition& Actual, bool bFine)
    {
        FString Error;
        TArray<FString> Validation;
        if (!Test.TestTrue(TEXT("Original authored graph is valid before expansion"),
                           Actual.ValidateForAuthoring(Validation)))
        {
            Test.AddInfo(FString::Join(Validation, TEXT(";")));
            return nullptr;
        }
        auto* Result = bFine ? AbilityAuthoredGraphFineCompiler::Compile(F.World, &Actual, Error) : &Actual;
        if (!Test.TestNotNull(
                FString(bFine ? TEXT("Isolated expanded graph: ") : TEXT("Actual original graph: ")) + Error, Result))
        {
            return nullptr;
        }
        if (bFine)
        {
            for (const auto& Node : Result->Nodes)
            {
                Test.TestFalse(TEXT("Normalized authored graph contains no complete legacy strategy"),
                               Node.Config->IsA<UAbilityLegacyNodeConfig>() ||
                                   Node.TypeId.ToString().StartsWith(TEXT("Legacy.")));
            }
        }
        return Result;
    }
} // namespace AbilityAuthoredGraphFineCompilerTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredParametersAreGameplayTruth,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.PrivateParametersOverrideStablePresentationDA",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredParametersAreGameplayTruth::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Actual stable Burst presentation DA"), Asset))
        {
            return false;
        }
        const float OriginalDamage = Asset->Damage;
        auto* Actual = F.Authored();
        F.Add(*Actual, *Asset, 17.f, 0);
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Authored private version actually runs: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Actual gameplay uses Config.Parameters17 instead of original DA damage"), F.Health(), 183.f);
        TestEqual(TEXT("Stable presentation asset remains unchanged"), Asset->Damage, OriginalDamage);
        TestEqual(TEXT("One isolated logical fragment reserves once"), F.Context->Spawned, 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredExtraEdgeSurvives,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.GraphOnlyHitBranchSurvivesWithoutDAChild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredExtraEdgeSurvives::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Stable presentation asset"), Asset))
        {
            return false;
        }
        auto* Actual = F.Authored();
        const FGuid Root = F.Add(*Actual, *Asset, 11.f, 0);
        const FGuid Extra = F.Add(*Actual, *Asset, 7.f, 1);
        FAbilityGraphEdge Edge;
        Edge.From = Root;
        Edge.OutputPort = TEXT("OnHit");
        Edge.To = Extra;
        Actual->Edges.Add(Edge);
        TestNull(TEXT("Extra actual graph branch is absent from the root private DA links"),
                 CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes[0].Config)->Parameters->OnHit.Get());
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Graph-only branch executes through the selected path: ") + Error,
                      F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Actual graph-only OnHit branch applies separately configured damage"), F.Health(), 182.f);
        TestEqual(TEXT("Root and actual extra branch each reserve once"), F.Context->Spawned, 2);
        TestEqual(TEXT("Expansion never removes original authored edge"), Actual->Edges.Num(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredDisconnectedDAChildStaysDisconnected,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.DisconnectedOldDAChildIsNotRegenerated",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredDisconnectedDAChildStaysDisconnected::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Stable presentation asset"), Asset))
        {
            return false;
        }
        auto* Actual = F.Authored();
        F.Add(*Actual, *Asset, 10.f, 0);
        auto* Parameters = CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes[0].Config)->Parameters.Get();
        auto* OldChild = DuplicateObject(Parameters, Parameters);
        OldChild->Damage = 50.f;
        Parameters->OnHit = OldChild;
        TestEqual(TEXT("Old private DA has a child but actual graph intentionally has no edge"), Actual->Edges.Num(),
                  0);
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Disconnected authored root runs: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Expansion does not resurrect old child damage"), F.Health(), 190.f);
        TestEqual(TEXT("Disconnected old child never reserves a logical spawn"), F.Context->Spawned, 1);
        TestEqual(TEXT("Actual disconnected topology remains untouched"), Actual->Edges.Num(), 0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredCompletedEdgeSurvives,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.CompletedExtraEdgeRunsAfterPrivateDuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredCompletedEdgeSurvives::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Stable actual presentation asset"), Asset))
        {
            return false;
        }
        auto* Actual = F.Authored();
        const auto Root = F.Add(*Actual, *Asset, 10.f, 0);
        const auto Child = F.Add(*Actual, *Asset, 7.f, 1);
        FAbilityGraphEdge Edge;
        Edge.From = Root;
        Edge.OutputPort = TEXT("Completed");
        Edge.To = Child;
        Actual->Edges.Add(Edge);
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Actual Completed extra edge starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Completed child is not a root-entry action"), F.Health(), 190.f);
        ++GFrameCounter;
        F.Run->Advance(.11f);
        TestEqual(TEXT("Actual private0.1 duration releases the graph-only completion child"), F.Health(), 183.f);
        TestEqual(TEXT("Completion child owns one independent logical reserve"), F.Context->Spawned, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredImpactPermissionBridge,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.ConfirmedImpactExtraBranchKeepsExplicitPermissionConversion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredImpactPermissionBridge::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Actual stable presentation asset"), Asset))
        {
            return false;
        }
        auto* Actual = F.Authored();
        const auto Root = F.Add(*Actual, *Asset, 11.f, 0);
        FAbilityGraphNode Resolve;
        Resolve.NodeId = FGuid::NewGuid();
        Resolve.TypeId = TEXT("Target.ResolveImpact");
        Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Actual);
        FAbilityGraphNode State;
        State.NodeId = FGuid::NewGuid();
        State.TypeId = TEXT("Status.Petrify");
        State.Config = NewObject<UAbilityPetrifyNodeConfig>(Actual);
        Actual->Nodes.Add(Resolve);
        Actual->Nodes.Add(State);
        FAbilityGraphEdge Hit;
        Hit.From = Root;
        Hit.OutputPort = TEXT("OnHit");
        Hit.To = Resolve.NodeId;
        Actual->Edges.Add(Hit);
        FAbilityGraphEdge Live;
        Live.From = Resolve.NodeId;
        Live.OutputPort = TEXT("TargetFound");
        Live.To = State.NodeId;
        Actual->Edges.Add(Live);
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Actual explicit permission branch executes: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        FPureMassView View;
        TestEqual(TEXT("Gameplay still uses the private root damage"), F.Health(), 189.f);
        TestTrue(TEXT("Historical original life is explicitly resolved before actual state"),
                 F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(F.Victim, View) && View.bPetrified);
        if (bFine)
        {
            TestTrue(TEXT("DamageResult is explicitly adapted to the old ConfirmedImpact contract"),
                     Definition->Nodes.ContainsByPredicate(
                         [](const auto& Node)
                         {
                             return Node.TypeId == TEXT("Event.ImpactFact");
                         }));
        }
    }
    return true;
}
namespace AbilityAuthoredGraphFineCompilerTests
{
    /** 真实旧图作为准入对照：内部ImpactFact不应制造新逻辑调用，原额外节点却必须保留累计计数。 */
    bool CrowdedOriginalAdmission(FAutomationTestBase& Test, bool bOriginalStateNodes, bool bActive32 = false)
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
            TArray<int32> Targets{F.Victim};
            auto* Type = NewObject<UPureMassMonsterType>(F.World);
            Type->Health = 200.f;
            Type->Radius = 2.f;
            Type->HalfHeight = 10.f;
            Type->Speed = 0.f;
            Type->XPReward = 0;
            for (int32 Index = 0; Index < (bOriginalStateNodes ? 0 : bActive32 ? 39 : 69); ++Index)
            {
                Targets.Add(Mass->SpawnMonster(Type, FVector(100 + Index * 10, 0, 0)));
            }
            auto* Asset = F.Presentation();
            if (!Test.TestNotNull(TEXT("Actual stable source asset"), Asset))
            {
                return false;
            }
            auto* Actual = F.Authored();
            Actual->Budget.MaxActiveNodes = 64;
            const FGuid Root = F.Add(*Actual, *Asset, 1.f, 0);
            CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes[0].Config)->Parameters->Radius = 1000.f;
            FGuid Destination;
            if (!bOriginalStateNodes)
            {
                Destination = F.Add(*Actual, *Asset, 2.f, 1);
                CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes.Last().Config)->Parameters->Radius = 1.f;
                if (bActive32)
                {
                    CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes.Last().Config)->Parameters->Duration = 5.f;
                    Actual->CompatibilityProfile = TEXT("UnifiedV2");
                    Actual->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
                    Actual->Budget.MaxActiveNodes = 32;
                }
            }
            else
            {
                // 串行Field令active始终低于32，只暴露原工具节点累计64调用，而非并发门。
                auto* RootConfig = CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes[0].Config);
                RootConfig->Parameters->Kind = EAbilityPrototypeKind::Field;
                RootConfig->Parameters->Duration = 8.f;
                RootConfig->Parameters->Period = .1f;
                Actual->Nodes[0].TypeId = TEXT("Legacy.Mixed.Field");
                Actual->CompatibilityProfile = TEXT("UnifiedV2");
                Actual->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
                Actual->Budget.MaxActiveNodes = 32;
                FAbilityGraphNode Resolve;
                Resolve.NodeId = FGuid::NewGuid();
                Resolve.TypeId = TEXT("Target.ResolveImpact");
                Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Actual);
                FAbilityGraphNode State;
                State.NodeId = FGuid::NewGuid();
                State.TypeId = TEXT("Status.Petrify");
                State.Config = NewObject<UAbilityPetrifyNodeConfig>(Actual);
                Actual->Nodes.Add(Resolve);
                Actual->Nodes.Add(State);
                Destination = Resolve.NodeId;
                FAbilityGraphEdge Live;
                Live.From = Resolve.NodeId;
                Live.OutputPort = TEXT("TargetFound");
                Live.To = State.NodeId;
                Actual->Edges.Add(Live);
            }
            FAbilityGraphEdge Hit;
            Hit.From = Root;
            Hit.OutputPort = TEXT("OnHit");
            Hit.To = Destination;
            Actual->Edges.Add(Hit);
            auto* Definition = Expand(Test, F, *Actual, bFine);
            if (!Definition)
            {
                return false;
            }
            FString Error;
            auto Admitted = MakeShared<TArray<FVector>>();
            if (!Test.TestTrue(TEXT("Actual crowded authored graph is accepted before runtime admission"),
                               F.Start(Definition, Error, Admitted)))
            {
                return false;
            }
            for (int32 Step = 0; Step < (bOriginalStateNodes ? 70 : 1); ++Step)
            {
                ++GFrameCounter;
                F.Run->Advance(bOriginalStateNodes ? .1f : .25f);
            }
            if (bOriginalStateNodes)
            {
                ++GFrameCounter;
                F.Run->Advance(2.f);
            }
            if (bActive32)
            {
                ++GFrameCounter;
                F.Run->Advance(6.f);
            }
            const auto Expected =
                bOriginalStateNodes ? EAbilityGraphEndReason::BudgetExceeded : EAbilityGraphEndReason::Completed;
            Test.TestEqual(FString(bFine ? TEXT("Fine") : TEXT("Original")) +
                               TEXT(" keeps precise authored admission outcome"),
                           F.Run->GetEndReason(), Expected);
            Test.TestEqual(TEXT("Original extra utility nodes do not consume Context logical spawns"),
                           F.Context->Spawned,
                           bOriginalStateNodes ? 1
                           : bActive32         ? 32
                                               : 64);
            int32 Petrified = 0;
            for (int32 Index = 0; Index < Targets.Num(); ++Index)
            {
                FPureMassView View;
                Test.TestTrue(TEXT("Original life survives the actual configured root damage"),
                              Mass->Resolve(Targets[Index], View));
                const float ExpectedHealth = bOriginalStateNodes             ? 168.f
                                             : Index < (bActive32 ? 31 : 63) ? 197.f
                                                                             : 199.f;
                Test.TestEqual(FString::Printf(TEXT("%s target%d root and admitted payload damage"),
                                               bFine ? TEXT("Fine") : TEXT("Original"), Index),
                               View.Health, ExpectedHealth);
                Petrified += View.bPetrified ? 1 : 0;
            }
            if (bOriginalStateNodes)
            {
                Test.TestEqual(TEXT("First31 real states land before Resolve32 exhausts original64"), Petrified, 1);
            }
            if (!bOriginalStateNodes && !bActive32)
            {
                FString Sequence;
                for (const auto& Position : *Admitted)
                {
                    Sequence += FString::Printf(TEXT(" %.0f"), Position.X);
                }
                Test.AddInfo(FString(bFine ? TEXT("Fine accepted origins:") : TEXT("Original accepted origins:")) +
                             Sequence);
            }
        }
        return true;
    }
} // namespace AbilityAuthoredGraphFineCompilerTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredTransparentAdmission64,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.TransparentImpactBridgePreservesOriginal64Admission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredTransparentAdmission64::RunTest(const FString&)
{
    return AbilityAuthoredGraphFineCompilerTests::CrowdedOriginalAdmission(*this, false);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalUtilityAdmission64,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.OriginalUtilityNodesKeepOriginalInvocationBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalUtilityAdmission64::RunTest(const FString&)
{
    return AbilityAuthoredGraphFineCompilerTests::CrowdedOriginalAdmission(*this, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalActive32,
    "test_25d.UnifiedGA.DynamicMage.AuthoredExpansion.MainLifetimesKeepActualBoundedActive32",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalActive32::RunTest(const FString&)
{
    return AbilityAuthoredGraphFineCompilerTests::CrowdedOriginalAdmission(*this, false, true);
}

namespace AbilityAuthoredGraphFineCompilerTests
{
    /** 原工具节点是真实原图的一部分；低事件额度使事件边界先于64调用边界发生。 */
    UAbilityGraphDefinition* StatefulActual(FWorld& F, UAbilityPrototype& Asset)
    {
        auto* Actual = F.Authored();
        const auto Root = F.Add(*Actual, Asset, 1.f, 0);
        auto* Config = CastChecked<UAbilityLegacyNodeConfig>(Actual->Nodes[0].Config);
        Config->Parameters->Kind = EAbilityPrototypeKind::Field;
        Config->Parameters->Duration = 8.f;
        Config->Parameters->Period = .1f;
        Actual->Nodes[0].TypeId = TEXT("Legacy.Mixed.Field");
        Actual->CompatibilityProfile = TEXT("UnifiedV2");
        Actual->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
        Actual->Budget.MaxActiveNodes = 32;
        FAbilityGraphNode Resolve;
        Resolve.NodeId = FGuid::NewGuid();
        Resolve.TypeId = TEXT("Target.ResolveImpact");
        Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Actual);
        FAbilityGraphNode State;
        State.NodeId = FGuid::NewGuid();
        State.TypeId = TEXT("Status.Petrify");
        State.Config = NewObject<UAbilityPetrifyNodeConfig>(Actual);
        Actual->Nodes.Add(Resolve);
        Actual->Nodes.Add(State);
        Actual->Edges.Add({Root, Resolve.NodeId, TEXT("OnHit")});
        Actual->Edges.Add({Resolve.NodeId, State.NodeId, TEXT("TargetFound")});
        return Actual;
    }
} // namespace AbilityAuthoredGraphFineCompilerTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalEventLimit,
    "test_25d.UnifiedGA.DynamicMage.OriginalProjection.AccumulatedOriginalEventsKeepBoundedLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalEventLimit::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Actual stable source asset"), Asset))
        {
            return false;
        }
        auto* Actual = StatefulActual(F, *Asset);
        Actual->Budget.MaxEvents = 4;
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Actual bounded event graph starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Field starts its clock without an immediate pulse"), F.Health(), 200.f);
        ++GFrameCounter;
        F.Run->Advance(.1f);
        TestEqual(TEXT("First real Field action lands before the event limit"), F.Health(), 199.f);
        ++GFrameCounter;
        F.Run->Advance(.1f);
        TestTrue(FString(bFine ? TEXT("Fine") : TEXT("Original")) + TEXT(" reaches its original event boundary"),
                 F.Run->IsFinished());
        TestEqual(TEXT("Fifth original event fails Bounded rather than becoming an unlimited helper event"),
                  F.Run->GetEndReason(), EAbilityGraphEndReason::BudgetExceeded);
        TestEqual(TEXT("Second real action remains committed when its state publication exceeds the event budget"),
                  F.Health(), 198.f);
        TestEqual(TEXT("Event failure does not invent a second logical prototype"), F.Context->Spawned, 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalFrameLimit,
    "test_25d.UnifiedGA.DynamicMage.OriginalProjection.ActualFrameCountsOriginalEventsWithoutHelperInflation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalFrameLimit::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Actual stable source asset"), Asset))
        {
            return false;
        }
        auto* Actual = StatefulActual(F, *Asset);
        Actual->Budget.MaxEventsPerTick = 1;
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Frame-limited real graph starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        auto IsPetrified = [&]()
        {
            FPureMassView View;
            return F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(F.Victim, View) && View.bPetrified;
        };
        TestEqual(TEXT("Field starts its clock without an immediate pulse"), F.Health(), 200.f);
        ++GFrameCounter;
        F.Run->Advance(.1f);
        TestEqual(TEXT("Helpers still commit the first real root action in its actual pulse frame"), F.Health(), 199.f);
        TestFalse(TEXT("First original event invokes Resolve; its second event waits for the next actual frame"),
                  IsPetrified());
        F.Run->Advance(0.f);
        TestFalse(TEXT("Repeated Advance in the same actual frame cannot obtain a fresh original event quota"),
                  IsPetrified());
        ++GFrameCounter;
        F.Run->Advance(0.f);
        TestTrue(TEXT("Next actual frame consumes TargetFound and applies the original real state"), IsPetrified());
        TestFalse(TEXT("Frame throttling keeps the original clock alive"), F.Run->IsFinished());
        F.Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalDepthEight,
    "test_25d.UnifiedGA.DynamicMage.OriginalProjection.LegalOriginalDepthEightDoesNotCountReplacementHelpers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalDepthEight::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Asset = F.Presentation();
        if (!TestNotNull(TEXT("Actual stable source asset"), Asset))
        {
            return false;
        }
        auto* Actual = F.Authored();
        FGuid Previous = F.Add(*Actual, *Asset, 1.f, 0);
        FName Port(TEXT("OnHit"));
        Actual->CompatibilityProfile = TEXT("UnifiedV2");
        Actual->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
        Actual->Budget.MaxActiveNodes = 32;
        for (int32 Index = 0; Index < 7; ++Index)
        {
            FAbilityGraphNode Utility;
            Utility.NodeId = FGuid::NewGuid();
            Utility.TypeId = TEXT("Event.OriginalOrigin");
            auto* Config = NewObject<UAbilityOriginalPortNodeConfig>(Actual);
            Config->bRequestChild = false;
            Utility.Config = Config;
            Actual->Nodes.Add(Utility);
            Actual->Edges.Add({Previous, Utility.NodeId, Port});
            Previous = Utility.NodeId;
            Port = TEXT("Forward");
        }
        auto* Definition = Expand(*this, F, *Actual, bFine);
        if (!Definition)
        {
            return false;
        }
        FString Error;
        if (!TestTrue(TEXT("Legal actual original depth8 starts: ") + Error, F.Start(Definition, Error)))
        {
            return false;
        }
        TestEqual(TEXT("Original damage is unchanged by seven transparent original utility calls"), F.Health(), 199.f);
        ++GFrameCounter;
        F.Run->Advance(.11f);
        TestEqual(TEXT("Depth8 original source completes; generated query/contact bridges do not consume its depth"),
                  F.Run->GetEndReason(), EAbilityGraphEndReason::Completed);
        TestEqual(TEXT("Original utilities are not additional prototype spawns"), F.Context->Spawned, 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAuthoredOriginalEdgeProof,
    "test_25d.UnifiedGA.DynamicMage.OriginalProjection.EdgeManifestBindsOriginalOrderAndRejectsHelperEntryBypass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAuthoredOriginalEdgeProof::RunTest(const FString&)
{
    using namespace AbilityAuthoredGraphFineCompilerTests;
    FWorld F;
    auto* Asset = F.Presentation();
    if (!TestNotNull(TEXT("Actual stable source asset"), Asset))
    {
        return false;
    }
    auto* Actual = F.Authored();
    const auto Root = F.Add(*Actual, *Asset, 1.f, 0);
    const auto A = F.Add(*Actual, *Asset, 2.f, 1);
    const auto B = F.Add(*Actual, *Asset, 3.f, 1);
    Actual->Edges = {{Root, A, TEXT("OnHit")}, {Root, B, TEXT("OnHit")}};
    auto* Normalized = Expand(*this, F, *Actual, true);
    if (!Normalized)
    {
        return false;
    }
    TestEqual(TEXT("Every real source edge remains in the ordered proof manifest"),
              Normalized->OriginalProjection.Edges.Num(), 2);
    auto* Reordered = DuplicateObject(Normalized, F.World);
    Reordered->OriginalProjection.Edges.Swap(0, 1);
    TestTrue(TEXT("Changing original sibling declaration order changes its semantic admission hash"),
             AbilityRuntimeRecipes::Fingerprint(*Reordered) != AbilityRuntimeRecipes::Fingerprint(*Normalized));

    auto* Bypassed = DuplicateObject(Normalized, F.World);
    const auto* Entry = Bypassed->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.OriginalIndex == 1 && Node.OriginalRole == EAbilityOriginalNodeRole::Entry;
        });
    const auto* Impact = Bypassed->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.OriginalIndex == 0 && Node.TypeId == TEXT("Event.ImpactFact");
        });
    if (!TestNotNull(TEXT("Actual child entry exists"), Entry) ||
        !TestNotNull(TEXT("Actual transparent fact helper exists"), Impact))
    {
        return false;
    }
    auto* Bypass = Bypassed->Edges.FindByPredicate(
        [&](const auto& Edge)
        {
            return Edge.To == Entry->NodeId;
        });
    if (!TestNotNull(TEXT("Actual endpoint-to-original-entry edge exists"), Bypass))
    {
        return false;
    }
    Bypass->From = Impact->NodeId;
    Bypass->OutputPort = TEXT("Completed");
    TArray<FString> Errors;
    TestFalse(TEXT("A type-correct transparent helper cannot bypass the original mapped publication endpoint"),
              Bypassed->ValidateForAuthoring(Errors));

    auto* Unmapped = DuplicateObject(Normalized, F.World);
    auto* Endpoint = Unmapped->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Event.OriginalImpact");
        });
    if (!TestNotNull(TEXT("Original confirmed-impact endpoint exists"), Endpoint))
    {
        return false;
    }
    Endpoint->OriginalOutputs.Reset();
    TestFalse(TEXT("Connected old entries cannot evade original event accounting by deleting port proof"),
              Unmapped->ValidateForAuthoring(Errors));
    return true;
}
#endif
