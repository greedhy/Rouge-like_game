// 调用链：Automation -> 动态构建 Schema 2 图 -> 独立查询/节拍/锚点/减速节点 -> 原目标与状态服务。
// 验证真实 Actor/Mass 状态、有限调度与取消；不保存资产，不激活另一个完整 GA。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "Components/BoxComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MyGameplayTags.h"

namespace AbilityBehaviorNodeTests
{
    /** 每例仅在本地注册真实节点；正式 RegisterBuiltins 接线由协调者在 GREEN 后完成。 */
    FAbilityNodeRegistry Registry()
    {
        FAbilityNodeRegistry Result;
        Result.RegisterBuiltins();
        AbilityBehaviorNodes::Register(Result);
        return Result;
    }

    UAbilityGraphDefinition* Graph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(World);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Result->Nodes = {Node};
        Result->EntryNodeId = Node.NodeId;
        return Result;
    }

    FAbilityNodeInput Input(const FAbilityGraphTestFixture& Fixture)
    {
        FAbilityNodeInput Result;
        Result.SourceToken = Fixture.SourceToken;
        Result.Origin = FVector::ZeroVector;
        Result.Direction = FVector::ForwardVector;
        Result.EventCenter = Result.Origin;
        return Result;
    }

    /** Mass 类型仅存在临时世界；实际扣血/减速仍走生产子系统。 */
    UPureMassMonsterType* Type(UWorld* World)
    {
        auto* Result = NewObject<UPureMassMonsterType>(World);
        Result->Health = 300.f;
        Result->XPReward = 0;
        Result->Speed = 100.f;
        return Result;
    }

    void Connect(UAbilityGraphDefinition& Definition, int32 From, FName Port, int32 To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = Definition.Nodes[From].NodeId;
        Edge.OutputPort = Port;
        Edge.To = Definition.Nodes[To].NodeId;
        Definition.Edges.Add(Edge);
    }

    void Add(UAbilityGraphDefinition& Definition, FName TypeId, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TypeId;
        Node.Config = Config;
        Definition.Nodes.Add(Node);
    }
} // namespace AbilityBehaviorNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityNearestBehaviorNodeTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.NearestRealTargetsAndVisibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 最近目标不是候选列表前缀；混合身份、排除输入目标与真实墙体遮挡均在查询层决定。 */
bool FAbilityNearestBehaviorNodeTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorNodeTests;
    FAbilityGraphTestFixture F;
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    auto* Monster = Type(F.World);
    for (int32 Index = 0; Index < 260; ++Index)
    {
        Sim->SpawnMonster(Monster, FVector(300 + Index % 20, 0, 0));
    }
    const int32 NearestId = Sim->SpawnMonster(Monster, FVector(80, 0, 0));
    auto* Config = NewObject<UAbilityNearestNodeConfig>(F.World);
    Config->Radius = 500.f;
    FAbilityNodeInput Payload = Input(F);
    const auto Nodes = Registry();
    TOptional<FAbilityNodeInput> SelectedPayload;
    auto Query = [&](const FAbilityNodeInput& Request, TOptional<FAbilityTargetToken>& Found, int32& NoTarget)
    {
        Found.Reset();
        SelectedPayload.Reset();
        NoTarget = 0;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("TargetFound"))
                {
                    Found = Event.Input.LiveTargetToken;
                    SelectedPayload = Event.Input;
                }
                if (Event.OutputPort == TEXT("NoTarget"))
                {
                    ++NoTarget;
                }
            });
        FString Error;
        TestTrue(TEXT("Nearest graph input starts"),
                 Run->StartWithInput(Graph(F.World, TEXT("Query.Nearest"), Config), Request, {}, Nodes, Error));
        return Run;
    };
    TOptional<FAbilityTargetToken> Found;
    int32 NoTarget = 0;
    auto* Run = Query(Payload, Found, NoTarget);
    if (TestTrue(TEXT("Nearest node produces a live target"), Found.IsSet()))
    {
        TestEqual(TEXT("Nearest candidate beyond the first 256 entries is not truncated"), Found->Target.EntityId,
                  NearestId);
        Payload.LiveTargetToken = Found;
    }
    if (TestTrue(TEXT("Nearest output contains the selected request context"), SelectedPayload.IsSet()))
    {
        TestTrue(TEXT("Target selection preserves the delivery start origin"),
                 SelectedPayload->Origin.Equals(Payload.Origin));
        TestTrue(TEXT("Target selection publishes the selected position separately"),
                 SelectedPayload->EventCenter.Equals(FVector(80, 0, 0)));
    }
    TestEqual(TEXT("Nearest normal completion is successful"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);
    Config->bExcludeInputTarget = true;
    Query(Payload, Found, NoTarget);
    if (TestTrue(TEXT("Excluding initial target finds another representation"), Found.IsSet()))
    {
        TestTrue(TEXT("Actor at 150 is selected after excluding Mass at 80"), Found->Target.Actor.Get() == F.Target);
    }
    Config->bExcludeInputTarget = false;
    Config->bIncludeMass = false;
    Query(Payload, Found, NoTarget);
    if (TestTrue(TEXT("Actor-only query finds actor"), Found.IsSet()))
    {
        TestTrue(TEXT("Actor-only filter does not select a Mass candidate"), Found->Target.IsActor());
    }
    Config->bIncludeMass = true;
    Config->Radius = 10.f;
    Query(Payload, Found, NoTarget);
    TestFalse(TEXT("Empty radius does not invent a target"), Found.IsSet());
    TestEqual(TEXT("Empty radius explicitly reports NoTarget once"), NoTarget, 1);

    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->AddInstanceComponent(Box);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 500, 500));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(40, 0, 0));
    Config->Radius = 500.f;
    Query(Payload, Found, NoTarget);
    TestFalse(TEXT("Wall blocks all next targets"), Found.IsSet());
    TestEqual(TEXT("Blocked search is a normal NoTarget result"), NoTarget, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityRepeatBehaviorCompositionTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.RepeatShapeDamageAndSlow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 节拍不内嵌伤害：运行时把 Pulse 接到查询，再独立接伤害和状态，观察两种实际输出。 */
bool FAbilityRepeatBehaviorCompositionTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorNodeTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    const int32 Id = Sim->SpawnMonster(Type(F.World), FVector(100, 0, 0));
    auto* Repeat = NewObject<UAbilityRepeatNodeConfig>(F.World);
    Repeat->Count = 3;
    Repeat->Period = .25f;
    Repeat->Duration = 4.f;
    auto* Definition = Graph(F.World, TEXT("Control.Repeat"), Repeat);
    auto* Query = NewObject<UAbilityQueryNodeConfig>(Definition);
    Query->Query.Radius = 180.f;
    Query->Query.bIncludeActors = false;
    auto* Damage = NewObject<UAbilityDamageNodeConfig>(Definition);
    Damage->Damage = 25.f;
    Damage->DamagePerLevel = 0.f;
    Damage->DamageType = FMyGameplayTags::Get().Damage_Magic;
    auto* Slow = NewObject<UAbilitySlowNodeConfig>(Definition);
    Slow->Multiplier = .6f;
    Add(*Definition, TEXT("Query.Shape"), Query);
    Add(*Definition, TEXT("Effect.Damage"), Damage);
    Add(*Definition, TEXT("Status.Slow"), Slow);
    Connect(*Definition, 0, TEXT("Pulse"), 1);
    Connect(*Definition, 1, TEXT("TargetFound"), 2);
    Connect(*Definition, 1, TEXT("TargetFound"), 3);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Pulses = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Pulses += Event.OutputPort == TEXT("Pulse");
        });
    const auto Nodes = Registry();
    FString Error;
    TestTrue(TEXT("Runtime-created repeat/shape/effect graph starts"),
             Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Run->Advance(.24f);
    FPureMassView View;
    TestTrue(TEXT("Target still resolves before first period"), Sim->Resolve(Id, View));
    TestEqual(TEXT("No early hidden damage"), View.Health, 300.f);
    Run->Advance(.01f);
    TestTrue(TEXT("Target resolves after first pulse"), Sim->Resolve(Id, View));
    TestEqual(TEXT("First pulse applies actual Mass damage once"), View.Health, 275.f);
    auto* Entities = F.World->GetSubsystem<UMassEntitySubsystem>();
    const auto& Fragment = Entities->GetEntityManager().GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Id));
    if (TestEqual(TEXT("Independent slow payload uses original Mass state service"), Fragment.SpellSlows.Num(), 1))
    {
        TestTrue(TEXT("Mass slow uses configured multiplier"),
                 FMath::IsNearlyEqual(Fragment.SpellSlows[0].X, .6, 1.e-6));
    }
    Run->Advance(.5f);
    TestTrue(TEXT("Target resolves after finite repeated effects"), Sim->Resolve(Id, View));
    TestEqual(TEXT("Three pulses apply exactly three damage payloads"), View.Health, 225.f);
    TestEqual(TEXT("Pulse count is independent of payload type"), Pulses, 3);
    TestTrue(TEXT("Finite repeat and descendants are fully released"), Run->IsFinished());
    Run->Advance(5.f);
    Sim->Resolve(Id, View);
    TestEqual(TEXT("Completed schedule never resumes"), View.Health, 225.f);

    auto* Cancelled = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Second independent graph starts"),
             Cancelled->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Cancelled->Advance(.25f);
    Sim->Resolve(Id, View);
    const float BeforeCancel = View.Health;
    Cancelled->Cancel();
    Cancelled->Advance(10.f);
    Sim->Resolve(Id, View);
    TestEqual(TEXT("Cancel stops all future payloads"), View.Health, BeforeCancel);
    TestEqual(TEXT("Graph cancellation preserves already applied owned target state"), Fragment.SpellSlows.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityRepeatProfileNodeTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.RepeatProfilesAndFiniteBoundaries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 配置明确区分积压保留/夹取/单步丢弃；有限节点不冒充无限通道，取消不补完成事件。 */
bool FAbilityRepeatProfileNodeTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorNodeTests;
    FAbilityGraphTestFixture F;
    const auto Nodes = Registry();
    for (const auto Profile :
         {EAbilityRepeatCadenceProfile::MixedEightPreserve, EAbilityRepeatCadenceProfile::ExtensionSixteenClamp,
          EAbilityRepeatCadenceProfile::SingleDiscard})
    {
        auto* Config = NewObject<UAbilityRepeatNodeConfig>(F.World);
        Config->Count = 20;
        Config->Period = .1f;
        Config->Duration = 10.f;
        Config->CadenceProfile = Profile;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Pulses = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Pulses += Event.OutputPort == TEXT("Pulse");
            });
        FString Error;
        TestTrue(TEXT("Explicit cadence profile starts"),
                 Run->StartWithInput(Graph(F.World, TEXT("Control.Repeat"), Config), Input(F), {}, Nodes, Error));
        Run->Advance(2.5f);
        const int32 Expected = Profile == EAbilityRepeatCadenceProfile::MixedEightPreserve      ? 8
                               : Profile == EAbilityRepeatCadenceProfile::ExtensionSixteenClamp ? 16
                                                                                                : 1;
        TestEqual(TEXT("Hitch uses exact declared catch-up budget"), Pulses, Expected);
        Run->Advance(0.f);
        const int32 AfterDrain = Profile == EAbilityRepeatCadenceProfile::MixedEightPreserve      ? 16
                                 : Profile == EAbilityRepeatCadenceProfile::ExtensionSixteenClamp ? 17
                                                                                                  : 1;
        TestEqual(TEXT("Next tick preserves or discards backlog as declared"), Pulses, AfterDrain);
        Run->Cancel();
    }
    for (const bool bInclusive : {false, true})
    {
        auto* Config = NewObject<UAbilityRepeatNodeConfig>(F.World);
        Config->Count = 10;
        Config->Period = .5f;
        Config->Duration = 1.f;
        Config->bIncludeFinalBoundary = bInclusive;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Pulses = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Pulses += Event.OutputPort == TEXT("Pulse");
            });
        FString Error;
        TestTrue(TEXT("Finite duration schedule starts"),
                 Run->StartWithInput(Graph(F.World, TEXT("Control.Repeat"), Config), Input(F), {}, Nodes, Error));
        Run->Advance(1.f);
        TestEqual(TEXT("Final boundary inclusion is explicit"), Pulses, bInclusive ? 2 : 1);
        TestTrue(TEXT("Duration completion releases the scheduler"), Run->IsFinished());
    }
    auto* Immediate = NewObject<UAbilityRepeatNodeConfig>(F.World);
    Immediate->Count = 1;
    Immediate->bEmitImmediately = true;
    auto* ImmediateRun = NewObject<UAbilityGraphInstance>(F.World);
    int32 ImmediatePulses = 0;
    ImmediateRun->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            ImmediatePulses += Event.OutputPort == TEXT("Pulse");
        });
    FString Error;
    ImmediateRun->StartWithInput(Graph(F.World, TEXT("Control.Repeat"), Immediate), Input(F), {}, Nodes, Error);
    TestEqual(TEXT("Immediate first pulse is opt-in"), ImmediatePulses, 1);
    TestTrue(TEXT("One immediate pulse completes finite Count=1"), ImmediateRun->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityAnchorBehaviorNodeTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.AnchorUsesExplicitPosition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 锚点只决定后续位置，不要求已命中的首目标仍存活，也不为控制节点发明活目标令牌。 */
bool FAbilityAnchorBehaviorNodeTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorNodeTests;
    FAbilityGraphTestFixture F;
    F.Source->SetActorLocation(FVector(40, 20, 10));
    FAbilityNodeInput Payload = Input(F);
    Payload.Origin = FVector(300, 50, 10);
    FAbilityImpactSnapshot Impact;
    Impact.Location = FVector(700, 100, 20);
    Impact.ConfirmedTarget = F.TargetToken;
    Payload.Impact = Impact;
    F.Target->Destroy();
    const auto Nodes = Registry();
    for (const auto Mode :
         {EAbilitySpatialAnchor::OriginalOrigin, EAbilitySpatialAnchor::Source, EAbilitySpatialAnchor::ConfirmedImpact})
    {
        auto* Config = NewObject<UAbilityAnchorNodeConfig>(F.World);
        Config->Anchor = Mode;
        Config->Offset = FVector(0, 0, 7);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TOptional<FAbilityNodeInput> Completed;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("Completed"))
                {
                    Completed = Event.Input;
                }
            });
        FString Error;
        TestTrue(TEXT("Anchor starts with valid historical geometry"),
                 Run->StartWithInput(Graph(F.World, TEXT("Spatial.Anchor"), Config), Payload, {}, Nodes, Error));
        const FVector Expected = (Mode == EAbilitySpatialAnchor::OriginalOrigin ? Payload.Origin
                                  : Mode == EAbilitySpatialAnchor::Source       ? F.Source->GetActorLocation()
                                                                                : Impact.Location) +
                                 Config->Offset;
        if (TestTrue(TEXT("Anchor publishes completed position"), Completed.IsSet()))
        {
            TestTrue(TEXT("Origin is chosen anchor plus world offset"), Completed->Origin.Equals(Expected));
            TestTrue(TEXT("Event center agrees with actual output origin"), Completed->EventCenter.Equals(Expected));
            TestFalse(TEXT("Anchor never upgrades history to live target permission"),
                      Completed->LiveTargetToken.IsSet());
        }
    }
    Payload.Impact.Reset();
    auto* Missing = NewObject<UAbilityAnchorNodeConfig>(F.World);
    Missing->Anchor = EAbilitySpatialAnchor::ConfirmedImpact;
    auto* Failed = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    Failed->StartWithInput(Graph(F.World, TEXT("Spatial.Anchor"), Missing), Payload, {}, Nodes, Error);
    TestEqual(TEXT("Missing confirmed impact fails instead of using zero position"), Failed->GetEndReason(),
              EAbilityGraphEndReason::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilitySlowBehaviorNodeTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.SlowPreservesTargetLifeAndOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Actor 的真实速度状态由原组件持有；图结束/取消不撤销已生效独立减速，旧租约不能再刷新。 */
bool FAbilitySlowBehaviorNodeTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorNodeTests;
    FAbilityGraphTestFixture F;
    auto* Config = NewObject<UAbilitySlowNodeConfig>(F.World);
    Config->Multiplier = .5f;
    Config->Duration = 2.f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput Payload = Input(F);
    Payload.LiveTargetToken = F.TargetToken;
    FString Error;
    const auto Nodes = Registry();
    TestTrue(TEXT("Slow payload graph starts"),
             Run->StartWithInput(Graph(F.World, TEXT("Status.Slow"), Config), Payload, {}, Nodes, Error));
    TestEqual(TEXT("Actor speed multiplier changes through real original component"),
              UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
    TestTrue(TEXT("Slow node finishes without waiting for target state expiration"), Run->IsFinished());
    Run->Cancel();
    TestEqual(TEXT("Graph cancellation does not erase already applied independent slow"),
              UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
    F.Target->EnableDungeonPooling();
    auto* Stale = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Old target token cannot start an effect on a changed pool lease"),
              Stale->StartWithInput(Graph(F.World, TEXT("Status.Slow"), Config), Payload, {}, Nodes, Error));
    TestEqual(TEXT("Old lease does not control new life speed"), UBatch6SlowComponent::GetMultiplier(F.Target), 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityBehaviorNodeValidationTest,
                                 "test_25d.UnifiedGA.DynamicMage.Behaviors.InvalidConfigurationsAreRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 非法输入在图执行前拒绝，不靠执行到一半才产生隐式默认行为。 */
bool FAbilityBehaviorNodeValidationTest::RunTest(const FString&)
{
    auto* Nearest = NewObject<UAbilityNearestNodeConfig>();
    auto* Repeat = NewObject<UAbilityRepeatNodeConfig>();
    auto* Anchor = NewObject<UAbilityAnchorNodeConfig>();
    auto* Slow = NewObject<UAbilitySlowNodeConfig>();
    FString Error;
    Nearest->bIncludeActors = Nearest->bIncludeMass = false;
    TestFalse(TEXT("Nearest needs at least one supported target representation"), Nearest->Validate(Error));
    Nearest->bIncludeActors = true;
    Nearest->Radius = -1.f;
    TestFalse(TEXT("Nearest refuses invalid radius"), Nearest->Validate(Error));
    Repeat->Count = 0;
    TestFalse(TEXT("Finite scheduler refuses zero count"), Repeat->Validate(Error));
    Repeat->Count = 65;
    TestFalse(TEXT("Finite scheduler refuses excess count"), Repeat->Validate(Error));
    Repeat->Count = 3;
    Repeat->Period = 0.f;
    TestFalse(TEXT("Finite scheduler refuses zero period"), Repeat->Validate(Error));
    Repeat->Period = .5f;
    Repeat->CadenceProfile = static_cast<EAbilityRepeatCadenceProfile>(255);
    TestFalse(TEXT("Unknown cadence policy cannot request sustained execution"), Repeat->Validate(Error));
    Anchor->Anchor = static_cast<EAbilitySpatialAnchor>(255);
    TestFalse(TEXT("Unknown anchor mode cannot silently select a position"), Anchor->Validate(Error));
    Slow->Multiplier = 0.f;
    TestFalse(TEXT("Slow refuses invalid multiplier"), Slow->Validate(Error));
    Slow->Multiplier = .5f;
    Slow->Duration = 0.f;
    TestFalse(TEXT("Slow refuses nonpositive duration"), Slow->Validate(Error));
    return true;
}
#endif
