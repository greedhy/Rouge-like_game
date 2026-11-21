// 测试链：真实配置/可信模板 → 同一配方编译与图 → 实际PM弹体/Actor和Mass目标 → 生命周期断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "UObject/UnrealType.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionPropagationParameters,
                                 "test_25d.GraphExpansion.Propagation.TypedInPlaceParameters",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionPropagationParameters::RunTest(const FString&)
{
    auto* Config = NewObject<UAbilityForkedPropagationNodeConfig>();
    Config->Delivery.ProjectileClass = AMyProjectile::StaticClass();
    for (const auto Pair : {TPair<const TCHAR*, float>(TEXT("Set.MaxTargets"), 7.f),
                            {TEXT("Set.JumpDelay"), .2f},
                            {TEXT("Set.JumpRadius"), 600.f},
                            {TEXT("Set.PropagationFalloff"), .75f},
                            {TEXT("Set.MaxDepth"), 3.f},
                            {TEXT("Set.BranchesPerTarget"), 3.f},
                            {TEXT("Set.ForkConcurrency"), 1.f}})
    {
        const auto* Parameter = AbilityRuntimeModifierTemplates::FindParameterTemplate(Pair.Key);
        if (!TestNotNull(Pair.Key, Parameter))
        {
            return false;
        }
        FString Error;
        TestTrue(Pair.Key, Config->TrySetRecipeParameter(Parameter->Parameter, Pair.Value, Error));
    }
    TestEqual(TEXT("Actual config consumes max-target edits"), Config->MaxTargets, 7);
    TestEqual(TEXT("Actual config consumes jump-delay edits"), Config->JumpDelay, .2f);
    TestEqual(TEXT("Actual config consumes radius edits"), Config->JumpRadius, 600.f);
    TestEqual(TEXT("Actual config consumes depth edits"), Config->MaxDepth, 3);
    TestEqual(TEXT("Actual config consumes branch edits"), Config->BranchesPerTarget, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionConcurrentFork,
                                 "test_25d.GraphExpansion.Fork.RealSiblingFlightsOverlap",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionConcurrentFork::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    const int32 A = F.Spawn(FVector(300, 0, 0), 10.f);
    F.Spawn(FVector(550, -150, 0));
    F.Spawn(FVector(550, 150, 0));
    F.Spawn(FVector(800, -300, 0));
    F.Spawn(FVector(800, 300, 0));
    auto* Graph = F.Build(false, false);
    auto* Config = CastChecked<UAbilityForkedPropagationNodeConfig>(Graph->Nodes[0].Config);
    const auto* Concurrent = FindFProperty<FBoolProperty>(Config->GetClass(), TEXT("bConcurrentBranches"));
    if (!TestNotNull(TEXT("A real concurrent scheduling mode exists"), Concurrent))
    {
        return false;
    }
    TestFalse(TEXT("Legacy configurations retain FIFO by default"), Concurrent->GetPropertyValue_InContainer(Config));
    Concurrent->SetPropertyValue_InContainer(Config, true);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Actual concurrent graph starts"),
                  Run->StartWithInput(Graph, F.MakeInput(A, Error), {}, F.Registry(), Error)))
    {
        AddInfo(Error);
        return false;
    }
    int32 Peak = F.LiveShots();
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    for (int32 Frame = 0; Frame < 1200 && !Run->IsFinished(); ++Frame)
    {
        F.Fly(*Run, .01f, Seen);
        Peak = FMath::Max(Peak, F.LiveShots());
    }
    TestTrue(TEXT("At least two real sibling arrows are simultaneously alive"), Peak >= 2);
    TestTrue(TEXT("All real branches eventually finish"), Run->IsFinished());
    TestEqual(TEXT("Each admitted original life still uses one actual arrow"), Seen.Num(), 5);
    TestEqual(TEXT("No active physical lease remains"), Run->GetActivePhysicalSpawnCount(), 0);
    TestEqual(TEXT("No projectile survives completion"), F.LiveShots(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionRuleRegistration,
                                 "test_25d.GraphExpansion.Rules.RealRegisteredStrategies",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionRuleRegistration::RunTest(const FString&)
{
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    bool bAll = true;
    for (const FName Type :
         {FName(TEXT("Query.SelectTargets")), FName(TEXT("Condition.Killed")), FName(TEXT("Condition.Chance"))})
    {
        const auto* Descriptor = Registry.Find(Type);
        bAll &= TestNotNull(*Type.ToString(), Descriptor);
        if (Descriptor)
        {
            TestTrue(TEXT("Registered rule has an actual execution class"), Descriptor->ExecutionClass != nullptr);
            TestTrue(TEXT("Registered rule supports Actor and Mass targets"),
                     Descriptor->bSupportsActorTargets && Descriptor->bSupportsMassTargets);
        }
    }
    return bAll;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionMixedSelection, "test_25d.GraphExpansion.Rules.ActorMassTargetPolicies",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionMixedSelection::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    auto* Actor = F.World->SpawnActor<AEnemyCharacter>();
    Actor->Tags.AddUnique(TEXT("Enemy"));
    Actor->SetActorLocation(FVector(150, -150, 0));
    auto* ASC = Actor->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Actor, Actor);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 90.f);
    const int32 Far = F.Spawn(FVector(650, 0, 0));
    const int32 Low = F.Spawn(FVector(400, 150, 0));
    // 真实伤害改变生命比例，排序不能用测试直接改查询快照。
    FPureMassDamage Damage;
    Damage.Amount = 150.f;
    Damage.Source = F.Source;
    F.Mass->ApplyDamage(Low, Damage);
    FString Error;
    TArray<FAbilitySelectedCandidate> Selected;
    auto Select = [&](EAbilityTargetSelectionPolicy Policy)
    {
        return AbilityTargetSelectionService::Select(
                   F.SourceToken, FVector::ZeroVector, 1000.f, 1, Policy,
                   [](const auto&)
                   {
                       return false;
                   },
                   Selected, Error) &&
               Selected.Num() == 1;
    };
    TestTrue(TEXT("Nearest selection uses Actor and Mass candidates"), Select(EAbilityTargetSelectionPolicy::Nearest));
    TestTrue(TEXT("Nearest picks actual Actor"), Selected.Num() == 1 && Selected[0].Target.Target.Actor.Get() == Actor);
    TestTrue(TEXT("Farthest selection succeeds"), Select(EAbilityTargetSelectionPolicy::Farthest));
    TestTrue(TEXT("Farthest picks actual Mass"), Selected.Num() == 1 && Selected[0].Target.Target.EntityId == Far);
    TestTrue(TEXT("Lowest health-ratio selection succeeds"), Select(EAbilityTargetSelectionPolicy::LowestHealthRatio));
    TestTrue(TEXT("Lowest ratio compares actual mixed attributes"),
             Selected.Num() == 1 && Selected[0].Target.Target.EntityId == Low);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionActualKillGate,
                                 "test_25d.GraphExpansion.Rules.ActualDamageGateQueryAndState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionActualKillGate::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    for (const bool bLethal : {false, true})
    {
        FFixture F;
        const int32 Victim = F.Spawn(FVector(300, 0, 0), bLethal ? 10.f : 200.f);
        const int32 Neighbour = F.Spawn(FVector(550, 0, 0));
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        const FGuid Damage = F.Add(*Graph, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(Graph));
        const FGuid Gate = F.Add(*Graph, TEXT("Condition.Killed"), NewObject<UAbilityKilledGateConfig>(Graph));
        auto* QueryConfig = NewObject<UAbilitySelectTargetsConfig>(Graph);
        QueryConfig->Radius = 400.f;
        const FGuid Query = F.Add(*Graph, TEXT("Query.SelectTargets"), QueryConfig);
        auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(Graph);
        Petrify->DurationSeconds = 2.f;
        const FGuid Status = F.Add(*Graph, TEXT("Status.Petrify"), Petrify);
        Graph->EntryNodeId = Damage;
        Graph->Edges = {{Damage, Gate, TEXT("DamageResolved")},
                        {Gate, Query, TEXT("Passed")},
                        {Query, Status, TEXT("TargetFound")}};
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Passed = 0;
        int32 Rejected = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("Passed"))
                {
                    ++Passed;
                    TestFalse(TEXT("Gate does not revive the lethal target token"),
                              Event.Input.LiveTargetToken.IsSet());
                }
                if (Event.OutputPort == TEXT("Rejected"))
                {
                    ++Rejected;
                }
            });
        FString Error;
        TestTrue(TEXT("Real damage/condition/query/status graph starts"),
                 Run->StartWithInput(Graph, F.MakeInput(Victim, Error), {}, F.Registry(), Error));
        TestEqual(TEXT("Gate chooses exactly the actual damage outcome"), Passed, bLethal ? 1 : 0);
        TestEqual(TEXT("The other outcome is exclusive"), Rejected, bLethal ? 0 : 1);
        FPureMassView View;
        TestTrue(TEXT("Neighbour still resolves"), F.Mass->Resolve(Neighbour, View));
        TestEqual(TEXT("Only a real kill applies the chained state to a fresh target"), View.bPetrified, bLethal);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionChanceEndpoints,
                                 "test_25d.GraphExpansion.Rules.ChanceEndpointsPublishOneSafePort",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionChanceEndpoints::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    for (const float Chance : {0.f, 1.f})
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        auto* Config = NewObject<UAbilityChanceGateConfig>(Graph);
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        Config->Chance = Chance;
        Graph->EntryNodeId = F.Add(*Graph, TEXT("Condition.Chance"), Config);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Passed = 0;
        int32 Rejected = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Passed += Event.OutputPort == TEXT("Passed") ? 1 : 0;
                Rejected += Event.OutputPort == TEXT("Rejected") ? 1 : 0;
            });
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        FString Error;
        TestTrue(TEXT("Finite chance graph executes"), Run->StartWithInput(Graph, Input, {}, F.Registry(), Error));
        TestEqual(TEXT("Probability endpoint is exact"), Passed, Chance == 1.f ? 1 : 0);
        TestEqual(TEXT("Exactly one conditional output is published"), Passed + Rejected, 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionCompilerReplay,
                                 "test_25d.GraphExpansion.Propagation.AtomicCompileAndStableIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionCompilerReplay::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    auto* Base = F.Build(false, false);
    const FGuid Root = Base->EntryNodeId;
    FAbilityRuntimeRecipeContribution Change;
    Change.SourceId = TEXT("Test.EditTargets");
    FAbilityRuntimeRecipePatch Patch;
    Patch.TargetNodeId = Root;
    Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Patch.Parameter = EAbilityConfigParameter::PropagationMaxTargets;
    Patch.Scalar = 7.f;
    Change.Patches.Add(Patch);
    UAbilityGraphDefinition* Compiled = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Actual compiler accepts consumed propagation parameter"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Change}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Base and original assets keep their original count"),
              CastChecked<UAbilityPropagationNodeConfig>(Base->Nodes[0].Config)->MaxTargets, 5);
    TestEqual(TEXT("Private compiled node consumes the new count"),
              CastChecked<UAbilityPropagationNodeConfig>(Compiled->Nodes[0].Config)->MaxTargets, 7);
    TestEqual(TEXT("Node identity remains selectable"), Compiled->EntryNodeId, Root);
    TestEqual(TEXT("Parameter edits do not rewrite edges"), Compiled->Edges.Num(), Base->Edges.Num());
    FAbilityRuntimeRecipeSnapshot Snapshot;
    Snapshot.Graph = Compiled;
    Snapshot.ContentHash = AbilityRuntimeRecipes::Fingerprint(*Compiled);
    FAbilityRuntimeGraphView View;
    TestTrue(TEXT("Actual graph view can describe consumed new parameters"),
             AbilityRuntimeGraphViews::Build(Snapshot, View, Error));
    const auto* RootView = View.Nodes.FindByPredicate(
        [Root](const auto& Node)
        {
            return Node.NodeId == Root;
        });
    TestTrue(TEXT("UI offers real in-place max-target and concurrency controls"),
             RootView && RootView->SupportedModifiers.Contains(TEXT("Set.MaxTargets")) &&
                 RootView->SupportedModifiers.Contains(TEXT("Set.ForkConcurrency")));
    const FString AcceptedHash = Snapshot.ContentHash;
    Change.Patches[0].Scalar = 3.5f;
    UAbilityGraphDefinition* Invalid = nullptr;
    TestFalse(TEXT("Fractional target counts are rejected atomically"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Change}, Invalid, Error));
    TestTrue(TEXT("Rejected edits preserve the already accepted graph"),
             AbilityRuntimeRecipes::Fingerprint(*Compiled) == AcceptedHash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionForkCapacity,
                                 "test_25d.GraphExpansion.Fork.ConcurrentCapacityWaitsWithoutRefund",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionForkCapacity::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    const int32 First = F.Spawn(FVector(300, 0, 0), 10.f);
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const float Angle = -.95f + Index * 1.9f / 7.f;
        F.Spawn(FVector(300 + 600 * FMath::Cos(Angle), 600 * FMath::Sin(Angle), 0));
    }
    auto* Graph = F.Build(false, false);
    auto* Config = CastChecked<UAbilityForkedPropagationNodeConfig>(Graph->Nodes[0].Config);
    Config->bConcurrentBranches = true;
    Config->BranchesPerTarget = 8;
    Config->MaxDepth = 1;
    Config->MaxTargets = 9;
    Config->JumpRadius = 1000.f;
    Graph->Budget.MaxActiveNodes = 4;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Concurrent graph starts with four shared physical slots"),
             Run->StartWithInput(Graph, F.MakeInput(First, Error), {}, F.Registry(), Error));
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    int32 Peak = 0;
    for (int32 Frame = 0; Frame < 1800 && !Run->IsFinished(); ++Frame)
    {
        F.Fly(*Run, .01f, Seen);
        Peak = FMath::Max(Peak, Run->GetActivePhysicalSpawnCount());
    }
    TestTrue(TEXT("Saturated siblings wait and later finish"),
             Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    TestTrue(TEXT("Concurrent leases never exceed the same root capacity"), Peak >= 2 && Peak <= 4);
    TestEqual(TEXT("All nine admitted deliveries consume cumulative budget exactly once"),
              Run->GetPhysicalSpawnCountTotal(), uint64(9));
    TestEqual(TEXT("All capacity is returned at completion"), Run->GetActivePhysicalSpawnCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionForkCancel, "test_25d.GraphExpansion.Fork.CancelReleasesEverySibling",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionForkCancel::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    const int32 First = F.Spawn(FVector(300, 0, 0), 10.f);
    F.Spawn(FVector(550, -150, 0));
    F.Spawn(FVector(550, 150, 0));
    auto* Graph = F.Build(false, false);
    auto* Config = CastChecked<UAbilityForkedPropagationNodeConfig>(Graph->Nodes[0].Config);
    Config->bConcurrentBranches = true;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Cancellation scenario starts"),
             Run->StartWithInput(Graph, F.MakeInput(First, Error), {}, F.Registry(), Error));
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    for (int32 Frame = 0; Frame < 500 && !Run->IsFinished() && F.LiveShots() < 2; ++Frame)
    {
        F.Fly(*Run, .01f, Seen);
    }
    TestTrue(TEXT("Cancellation occurs with two actual sibling arrows alive"), F.LiveShots() >= 2);
    const uint64 Used = Run->GetPhysicalSpawnCountTotal();
    Run->Cancel();
    TestEqual(TEXT("All sibling projectiles are cancelled"), F.LiveShots(), 0);
    TestEqual(TEXT("Every exact lease is released"), Run->GetActivePhysicalSpawnCount(), 0);
    TestEqual(TEXT("Cancellation does not refund cumulative generation"), Run->GetPhysicalSpawnCountTotal(), Used);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionOrderedEdits,
                                 "test_25d.GraphExpansion.Propagation.OrderedWorkshopReplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionOrderedEdits::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    auto* Base = F.Build(false, false);
    FAbilityRuntimeRecipeContribution Replace;
    Replace.SourceId = TEXT("Test.Ordered.Replace");
    Replace.Priority = 1;
    FAbilityRuntimeRecipePatch Replacement;
    Replacement.TargetNodeId = Base->EntryNodeId;
    Replacement.Operation = EAbilityRecipePatchOperation::ReplaceNode;
    Replacement.ReplacementTypeId = TEXT("Propagation.Forked");
    Replacement.ReplacementConfig = DuplicateObject<UAbilityNodeConfig>(Base->Nodes[0].Config, F.World);
    Replace.Patches.Add(Replacement);
    FAbilityRuntimeRecipeContribution First;
    First.SourceId = TEXT("Test.Ordered.First");
    First.Priority = 2;
    FAbilityRuntimeRecipePatch Parameter;
    Parameter.TargetNodeId = Base->EntryNodeId;
    Parameter.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Parameter.Parameter = EAbilityConfigParameter::PropagationMaxTargets;
    Parameter.Scalar = 6;
    First.Patches.Add(Parameter);
    auto Second = First;
    Second.SourceId = TEXT("Test.Ordered.Second");
    Second.Priority = 3;
    Second.Patches[0].Scalar = 7;
    // 反射仅用于先在旧实现中运行同一失败测试，不作为产品配置或调用通路。
    auto* Ordered =
        FindFProperty<FBoolProperty>(FAbilityRuntimeRecipeContribution::StaticStruct(), TEXT("bOrderedWorkshopEdit"));
    for (auto* Change : {&Replace, &First, &Second})
    {
        if (Ordered)
        {
            Ordered->SetPropertyValue_InContainer(Change, true);
        }
    }
    UAbilityGraphDefinition* Result = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Ordered workshop can edit a replacement and edit the same axis again"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Replace, First, Second}, Result, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Latest ordered value wins"),
              CastChecked<UAbilityPropagationNodeConfig>(Result->Nodes[0].Config)->MaxTargets, 7);
    TestEqual(TEXT("Replacing and re-editing preserve node identity"), Result->EntryNodeId, Base->EntryNodeId);
    TestTrue(TEXT("Undo replays the remaining accepted history"),
             AbilityRuntimeRecipes::Compile(F.World, Base, {Replace, First}, Result, Error));
    TestEqual(TEXT("Undo restores previous parameter"),
              CastChecked<UAbilityPropagationNodeConfig>(Result->Nodes[0].Config)->MaxTargets, 6);
    if (Ordered)
    {
        Ordered->SetPropertyValue_InContainer(&First, false);
    }
    TestFalse(TEXT("Strict non-workshop sources retain conflict rejection"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Replace, First}, Result, Error));
    TestNull(TEXT("Rejected conflict publishes no graph"), Result);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphExpansionStrictProvenance,
                                 "test_25d.GraphExpansion.Propagation.StrictWriterSurvivesOrderedEdits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphExpansionStrictProvenance::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture F;
    auto* Base = F.Build(false, false);
    FAbilityRuntimeRecipeContribution Strict;
    Strict.SourceId = TEXT("Test.StrictSource");
    Strict.Priority = 1;
    FAbilityRuntimeRecipePatch Axis;
    Axis.TargetNodeId = Base->EntryNodeId;
    Axis.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Axis.Parameter = EAbilityConfigParameter::PropagationFalloff;
    Axis.Scalar = .9f;
    Strict.Patches.Add(Axis);
    auto Ordered = Strict;
    Ordered.SourceId = TEXT("Test.OrderedOtherAxis");
    Ordered.Priority = 2;
    Ordered.bOrderedWorkshopEdit = true;
    Ordered.Patches[0].Parameter = EAbilityConfigParameter::PropagationMaxTargets;
    Ordered.Patches[0].Scalar = 6.f;
    FAbilityRuntimeRecipeContribution Replace;
    Replace.SourceId = TEXT("Test.OrderedLaterReplacement");
    Replace.Priority = 3;
    Replace.bOrderedWorkshopEdit = true;
    FAbilityRuntimeRecipePatch Replacement;
    Replacement.TargetNodeId = Base->EntryNodeId;
    Replacement.Operation = EAbilityRecipePatchOperation::ReplaceNode;
    Replacement.ReplacementTypeId = TEXT("Propagation.Forked");
    Replacement.ReplacementConfig = DuplicateObject<UAbilityNodeConfig>(Base->Nodes[0].Config, F.World);
    Replace.Patches.Add(Replacement);
    UAbilityGraphDefinition* Accepted = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Independent ordered axis can coexist with a strict source"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Strict, Ordered}, Accepted, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const FString AcceptedHash = AbilityRuntimeRecipes::Fingerprint(*Accepted);
    UAbilityGraphDefinition* Rejected = nullptr;
    TestFalse(TEXT("An intervening ordered writer cannot erase older strict provenance"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Strict, Ordered, Replace}, Rejected, Error));
    TestNull(TEXT("Conflicting replacement publishes no candidate"), Rejected);
    const auto* Values = CastChecked<UAbilityPropagationNodeConfig>(Accepted->Nodes[0].Config);
    TestEqual(TEXT("Strict source value survives rejected replacement"), Values->DamageFalloff, .9f);
    TestEqual(TEXT("Other ordered axis also survives"), Values->MaxTargets, 6);
    TestEqual(TEXT("Accepted content remains immutable"), AbilityRuntimeRecipes::Fingerprint(*Accepted), AcceptedHash);
    return true;
}
#endif
