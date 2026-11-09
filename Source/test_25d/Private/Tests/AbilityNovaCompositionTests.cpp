// 自动化链：Nova 实际伤害 -> 原生命快照显式解析 -> Petrify 状态；失效快照不得控制新实体。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityNovaNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNovaPetrifyComposition, "test_25d.UnifiedGA.Nova.DamageSnapshotComposesPetrify",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNovaPetrifyComposition::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(600, 0, 0));
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Nova, Resolve, Petrify;
    Nova.NodeId = FGuid::NewGuid();
    Nova.TypeId = TEXT("Shape.ArcaneNova");
    auto* NovaConfig = NewObject<UAbilityNovaNodeConfig>(Graph);
    NovaConfig->Radius = 200.f;
    NovaConfig->bIncludeMass = true;
    Nova.Config = NovaConfig;
    Resolve.NodeId = FGuid::NewGuid();
    Resolve.TypeId = TEXT("Target.ResolveDamageTarget");
    Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Graph);
    Petrify.NodeId = FGuid::NewGuid();
    Petrify.TypeId = TEXT("Status.Petrify");
    Petrify.Config = NewObject<UAbilityPetrifyNodeConfig>(Graph);
    Graph->Nodes = {Nova, Resolve, Petrify};
    Graph->EntryNodeId = Nova.NodeId;
    FAbilityGraphEdge HitEdge;
    HitEdge.From = Nova.NodeId;
    HitEdge.To = Resolve.NodeId;
    HitEdge.OutputPort = TEXT("DamageResolved");
    FAbilityGraphEdge StatusEdge;
    StatusEdge.From = Resolve.NodeId;
    StatusEdge.To = Petrify.NodeId;
    StatusEdge.OutputPort = TEXT("TargetFound");
    Graph->Edges = {HitEdge, StatusEdge};
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = FVector(600, 0, 0);
    Input.DamageParams.BaseDamage = 25.f;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput DamageSnapshot;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("DamageResolved"))
            {
                DamageSnapshot = Event.Input;
            }
        });
    FString Error;
    TestTrue(TEXT("New graph validates and executes cross-family actions"),
             Run->StartWithInput(Graph, Input, {}, Error));
    FPureMassView View;
    TestTrue(TEXT("Original entity alive"), Mass->Resolve(Id, View));
    TestEqual(TEXT("One Nova damage"), View.Health, 75.f);
    TestTrue(TEXT("Revalidated target receives composed petrify"), View.bPetrified);
    TestTrue(TEXT("Synchronous graph finishes"), Run->IsFinished());
    TestTrue(TEXT("Damage emits historical identity without live authority"),
             DamageSnapshot.Impact.IsSet() && !DamageSnapshot.LiveTargetToken.IsSet());
    FPureMassDamage Lethal;
    Lethal.Source = F.Source;
    Lethal.Amount = 1000.f;
    Mass->ApplyDamage(Id, Lethal);
    const int32 Replacement = Mass->SpawnMonster(Type, FVector(600, 0, 0));
    Graph->EntryNodeId = Resolve.NodeId;
    Graph->Nodes = {Resolve, Petrify};
    Graph->Edges = {StatusEdge};
    Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Historical impact can be checked after death"),
             Run->StartWithInput(Graph, DamageSnapshot, {}, Error));
    TestTrue(TEXT("Lost target branch completes normally"), Run->IsFinished());
    TestTrue(TEXT("Replacement entity exists"), Mass->Resolve(Replacement, View));
    TestFalse(TEXT("Old snapshot cannot petrify replacement"), View.bPetrified);
    TestEqual(TEXT("Replacement health unchanged"), View.Health, 100.f);
    return true;
}
#endif
