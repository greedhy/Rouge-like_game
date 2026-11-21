// 测试链：精确边配方 -> 私有编译图 -> 同一GraphInstance -> 真实石化状态及目标GE。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityGraphPassThroughNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimeGraphEdgeTests
{
    /** 原边由Delay提供LiveTarget；原后继GE扣10血，插入分支只负责石化。 */
    struct FStage : FAbilityGraphTestFixture
    {
        FAbilityRuntimeRecipeContribution Edit;
        UAbilityGraphDefinition* Effect = nullptr;
        FGuid Destination;
        FStage()
        {
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("UnifiedV2");
            Effect = NewObject<UAbilityGraphDefinition>(World);
            Effect->SchemaVersion = 2;
            Effect->CompatibilityProfile = TEXT("UnifiedV2");
            auto Status = Graph->Nodes.Last();
            Status.Config = DuplicateObject<UAbilityNodeConfig>(Status.Config, Effect);
            Effect->EntryNodeId = Status.NodeId;
            Effect->Nodes.Add(Status);
            Destination = Graph->Nodes.Last().NodeId;
            auto* Damage = NewObject<UAbilityGameplayEffectNodeConfig>(Graph);
            Damage->bApplyToLiveActor = true;
            Damage->EffectClass = UArcaneShardTestDamageEffect::StaticClass();
            Graph->Nodes.Last().TypeId = TEXT("Effect.GameplayEffectTarget");
            Graph->Nodes.Last().Config = Damage;
            Edit.SourceId = TEXT("Test.Workshop.Edge");
            auto& Patch = Edit.Patches.AddDefaulted_GetRef();
            Patch.Operation = EAbilityRecipePatchOperation::InsertOnEdge;
            Patch.TargetNodeId = Graph->EntryNodeId;
            Patch.OutputPort = TEXT("Completed");
            Patch.TargetEdgeTo = Destination;
        }

        /** 冻结真实状态子图，保留编译器要验证的原始片段ID。 */
        bool Prepare(FString& Error)
        {
            auto& Patch = Edit.Patches[0];
            UAbilityGraphDefinition* Wrapped = nullptr;
            if (!AbilityGraphPassThroughNodes::WrapBranch(World, Effect, EAbilityPortPayload::LiveTarget, Wrapped,
                                                          Patch.ContinuationNodeId, Patch.ContinuationPort, Error))
            {
                return false;
            }
            Patch.BranchGraph = Wrapped;
            return true;
        }
    };

    bool HasEdge(const UAbilityGraphDefinition& Graph, FGuid From, FName Port, FGuid To)
    {
        return Graph.Edges.ContainsByPredicate(
            [&](const auto& Edge)
            {
                return Edge.From == From && Edge.OutputPort == Port && Edge.To == To;
            });
    }
} // namespace AbilityRuntimeGraphEdgeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopEdgeExact,
                                 "test_25d.UnifiedGA.RuntimeGraphWorkshop.Edge.ExactSiblingSharedTailAndBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopEdgeExact::RunTest(const FString&)
{
    using namespace AbilityRuntimeGraphEdgeTests;
    FStage F;
    const auto Root = F.Graph->EntryNodeId;
    auto Sibling = F.Graph->Nodes[0];
    Sibling.NodeId = FGuid::NewGuid();
    F.Graph->Nodes.Add(Sibling);
    auto BeforeSibling = F.Graph->Nodes[0];
    BeforeSibling.NodeId = FGuid::NewGuid();
    F.Graph->Nodes.Add(BeforeSibling);
    F.Graph->Edges.Insert(FAbilityGraphEdge{Root, BeforeSibling.NodeId, TEXT("Completed")}, 0);
    F.Graph->Edges.Add({Root, Sibling.NodeId, TEXT("Completed")});
    F.Graph->Edges.Add({Sibling.NodeId, F.Destination, TEXT("Completed")});
    FString Error;
    UAbilityGraphDefinition* Compiled = nullptr;
    const auto Before = AbilityRuntimeRecipes::Fingerprint(*F.Graph);
    if (!TestTrue(TEXT("Selected edge compiles"),
                  F.Prepare(Error) && AbilityRuntimeRecipes::Compile(F.World, F.Graph, {F.Edit}, Compiled, Error)))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("Only direct selected edge is removed"), HasEdge(*Compiled, Root, TEXT("Completed"), F.Destination));
    TestTrue(TEXT("Same-port sibling survives"), HasEdge(*Compiled, Root, TEXT("Completed"), Sibling.NodeId));
    TestTrue(TEXT("Shared tail remains reachable from sibling"),
             HasEdge(*Compiled, Sibling.NodeId, TEXT("Completed"), F.Destination));
    TestTrue(TEXT("Sibling before selected edge stays first"), Compiled->Edges[0].To == BeforeSibling.NodeId);
    TestTrue(TEXT("Sibling after selected edge stays third"), Compiled->Edges[2].To == Sibling.NodeId);
    const auto Entry = Compiled->Edges[1].To;
    TestTrue(TEXT("Replacement keeps original declaration order and continues to exact old destination"),
             HasEdge(*Compiled, Entry, TEXT("Continue"), F.Destination));
    TestEqual(TEXT("Exactly one adapter and one effect were copied"), Compiled->Nodes.Num(), F.Graph->Nodes.Num() + 2);
    TestEqual(TEXT("Source graph unchanged"), AbilityRuntimeRecipes::Fingerprint(*F.Graph), Before);
    F.Graph->Budget.MaxInvocations = 5;
    TestFalse(TEXT("Original five expanded calls fit but inserted nodes exceed same root budget"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {F.Edit}, Compiled, Error));
    TestNull(TEXT("Budget rejection returns no partial graph"), Compiled);
    F.Graph->Budget.MaxInvocations = 64;
    F.Graph->Budget.MaxActiveNodes = 1;
    TestFalse(TEXT("The waiting adapter and effect cannot share one active slot"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {F.Edit}, Compiled, Error));
    TestTrue(TEXT("Impossible active-slot budget has a precise diagnostic"), Error.Contains(TEXT("active-node")));
    TestNull(TEXT("No partial graph after active budget rejection"), Compiled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopEdgeGameplay,
                                 "test_25d.UnifiedGA.RuntimeGraphWorkshop.Edge.StatusBeforeSuccessorAndLifetimeGates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopEdgeGameplay::RunTest(const FString&)
{
    using namespace AbilityRuntimeGraphEdgeTests;
    enum class EInterruption
    {
        None,
        Cancel,
        RetireSource,
        ReuseTarget
    };
    for (const auto Mode :
         {EInterruption::None, EInterruption::Cancel, EInterruption::RetireSource, EInterruption::ReuseTarget})
    {
        FStage F;
        FString Error;
        // 故意延迟已注册子树，让恢复点跨帧。运行图不得从旧目标重新捕获输入。
        const FGuid Wait = FGuid::NewGuid();
        F.Effect->Nodes.Add({Wait, TEXT("Control.Delay"), NewObject<UAbilityDelayNodeConfig>(F.Effect)});
        F.Effect->Edges.Add({Wait, F.Effect->EntryNodeId, TEXT("Completed")});
        F.Effect->EntryNodeId = Wait;
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!F.Prepare(Error) || !AbilityRuntimeRecipes::Compile(F.World, F.Graph, {F.Edit}, Compiled, Error))
        {
            AddError(Error);
            return false;
        }
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        int32 HealthChanges = 0;
        bool bAlreadyPetrified = false;
        const auto Binding = TargetASC->GetGameplayAttributeValueChangeDelegate(UMyAttributeSet::GetHealthAttribute())
                                 .AddLambda(
                                     [&](const FOnAttributeChangeData& Change)
                                     {
                                         if (Change.NewValue < Change.OldValue)
                                         {
                                             ++HealthChanges;
                                             bAlreadyPetrified = F.IsPetrified();
                                         }
                                     });
        TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(F.World));
        TWeakObjectPtr<UObject> Context(NewObject<UPrototypeCastContext>(GetTransientPackage()));
        int32 Applied = 0;
        int32 Resumed = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.NodeType == TEXT("PassThrough.LiveTarget") && Event.OutputPort == TEXT("Continue"))
                {
                    ++Resumed;
                    TestTrue(TEXT("Resume retains original context across frames and collection"),
                             Context.IsValid() && Event.Input.Context == Context);
                }
                if (Event.NodeType != TEXT("Status.Petrify") || Event.OutputPort != TEXT("Completed"))
                {
                    return;
                }
                ++Applied;
                if (Mode == EInterruption::Cancel)
                {
                    Run->Cancel();
                }
                else if (Mode == EInterruption::RetireSource)
                {
                    F.Source->EnableDungeonPooling();
                }
                else if (Mode == EInterruption::ReuseTarget)
                {
                    F.Target->EnableDungeonPooling();
                }
            });
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.LiveTargetToken = F.TargetToken;
        Input.Context = Context;
        if (!TestTrue(TEXT("Original life starts"), Run->StartWithInput(Compiled, Input, {}, Error)))
        {
            AddError(Error);
            return false;
        }
        Run->Advance(.3f);
        TestEqual(TEXT("No early health change while subtree delay is pending"), HealthChanges, 0);
        if (Mode == EInterruption::None)
        {
            CollectGarbage(RF_NoFlags);
        }
        ++GFrameCounter;
        Run->Advance(.3f);
        TestEqual(TEXT("Effect subtree ran once before interruption"), Applied, 1);
        TestEqual(TEXT("Only uninterrupted successor applied health GE"), HealthChanges,
                  Mode == EInterruption::None ? 1 : 0);
        TestEqual(TEXT("Continue is published exactly once or suppressed by retirement"), Resumed,
                  Mode == EInterruption::None ? 1 : 0);
        if (Mode == EInterruption::None)
        {
            TestTrue(TEXT("Actual target already petrified inside GE's health-change callback"), bAlreadyPetrified);
            TestEqual(TEXT("Actual successor damage"),
                      TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        }
        TestTrue(TEXT("Success or invalidated original life releases the root"), Run->IsFinished());
        TestEqual(TEXT("No retained invocation"), Run->GetActiveInvocationCount(), 0);
        TargetASC->GetGameplayAttributeValueChangeDelegate(UMyAttributeSet::GetHealthAttribute()).Remove(Binding);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopEdgeReject,
                                 "test_25d.UnifiedGA.RuntimeGraphWorkshop.Edge.InvalidPayloadContinuationAndProjection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorkshopEdgeReject::RunTest(const FString&)
{
    using namespace AbilityRuntimeGraphEdgeTests;
    FStage F;
    FString Error;
    if (!F.Prepare(Error))
    {
        AddError(Error);
        return false;
    }
    UAbilityGraphDefinition* Compiled = nullptr;
    FGuid Continuation;
    FName Port;
    for (const auto Incompatible : {EAbilityPortPayload::PreparedInventory, EAbilityPortPayload::Origin})
    {
        TestFalse(TEXT("Cannot wrap live state with prepared inventory or position input"),
                  AbilityGraphPassThroughNodes::WrapBranch(F.World, F.Effect, Incompatible, Compiled, Continuation,
                                                           Port, Error));
    }
    const auto Before = AbilityRuntimeRecipes::Fingerprint(*F.Graph);
    auto* OriginEffect = NewObject<UAbilityGraphDefinition>(F.World);
    OriginEffect->SchemaVersion = 2;
    OriginEffect->CompatibilityProfile = TEXT("UnifiedV2");
    OriginEffect->EntryNodeId = FGuid::NewGuid();
    OriginEffect->Nodes.Add(
        {OriginEffect->EntryNodeId, TEXT("Control.DelayOrigin"), NewObject<UAbilityDelayNodeConfig>(OriginEffect)});
    auto Bad = F.Edit;
    if (!TestTrue(TEXT("An origin wrapper is valid on an origin edge"),
                  AbilityGraphPassThroughNodes::WrapBranch(F.World, OriginEffect, EAbilityPortPayload::Origin, Compiled,
                                                           Continuation, Port, Error)))
    {
        return false;
    }
    Bad.Patches[0].BranchGraph = Compiled;
    Bad.Patches[0].ContinuationNodeId = Continuation;
    Bad.Patches[0].ContinuationPort = Port;
    TestFalse(TEXT("A valid Origin wrapper cannot forward a selected LiveTarget edge as position"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {Bad}, Compiled, Error));
    Bad = F.Edit;
    Bad.Patches[0].ContinuationPort = TEXT("Apply");
    TestFalse(TEXT("Effect publication cannot resume original branch"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {Bad}, Compiled, Error));
    Bad = F.Edit;
    Bad.Patches[0].ContinuationNodeId = F.Effect->EntryNodeId;
    TestFalse(TEXT("Effect node cannot impersonate retained-payload continuation"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {Bad}, Compiled, Error));
    Bad = F.Edit;
    Bad.Patches[0].TargetEdgeTo = FGuid::NewGuid();
    TestFalse(TEXT("Missing exact edge is rejected"),
              AbilityRuntimeRecipes::Compile(F.World, F.Graph, {Bad}, Compiled, Error));
    TestNull(TEXT("Rejection returns no partial graph"), Compiled);
    TestEqual(TEXT("Rejected edits keep original configuration"), AbilityRuntimeRecipes::Fingerprint(*F.Graph), Before);

    auto* Parameters = NewObject<UAbilityPrototype>(F.World);
    Parameters->Kind = EAbilityPrototypeKind::Burst;
    Parameters->Cooldown = 0.f;
    Parameters->DamageType = FMyGameplayTags::Get().Damage_Magic;
    auto* Legacy = UAbilityLegacyGraphCompiler::Compile(F.World, Parameters, TEXT("LegacyMixed.v1"), Error);
    auto* Projected = Legacy ? AbilityAuthoredGraphFineCompiler::Compile(F.World, Legacy, Error) : nullptr;
    if (!TestNotNull(TEXT("Normalized original projection"), Projected))
    {
        return false;
    }
    const auto* Anchor = Projected->Nodes.FindByPredicate(
        [](const auto& N)
        {
            return N.TypeId == TEXT("Control.PrototypeLifetime");
        });
    const auto* Protected = Projected->Edges.FindByPredicate(
        [&](const auto& E)
        {
            return Anchor && E.From == Anchor->NodeId && E.OutputPort == TEXT("Action");
        });
    if (!TestNotNull(TEXT("Real original projected edge"), Protected))
    {
        return false;
    }
    Bad = F.Edit;
    Bad.Patches[0].TargetNodeId = Protected->From;
    Bad.Patches[0].OutputPort = Protected->OutputPort;
    Bad.Patches[0].TargetEdgeTo = Protected->To;
    const auto ProjectedHash = AbilityRuntimeRecipes::Fingerprint(*Projected);
    TestFalse(TEXT("Insertion cannot bypass original projection proof"),
              AbilityRuntimeRecipes::Compile(F.World, Projected, {Bad}, Compiled, Error));
    TestTrue(TEXT("Rejected for original projection"), Error.Contains(TEXT("projected")));
    TestEqual(TEXT("Original proof and budget survive rejection"), AbilityRuntimeRecipes::Fingerprint(*Projected),
              ProjectedHash);
    return true;
}
#endif
