// 调用链：Automation -> 内存 Schema2 配方 -> 真实布防/波形/发射调度 -> Actor/Mass 投递及效果。
// 测试不创建旧完整 GA，不修改 Content；用实际属性/状态及根预算验证可替换节点。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "Actor/MyProjectile.h"
#include "Components/BoxComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"

namespace AbilityDynamicCadenceTests
{
    /** 本地注册真实策略，测试不依赖全局 Registry 是否已完成接线。 */
    FAbilityNodeRegistry Registry()
    {
        FAbilityNodeRegistry Result;
        Result.RegisterBuiltins();
        AbilityBehaviorNodes::Register(Result);
        AbilityTemporalNodes::Register(Result);
        AbilityDeliveryNodes::Register(Result);
        return Result;
    }

    int32 Add(UAbilityGraphDefinition& Graph, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        return Graph.Nodes.Add(Node);
    }

    UAbilityGraphDefinition* Graph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(World);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        Add(*Result, Type, Config);
        Result->EntryNodeId = Result->Nodes[0].NodeId;
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

    FAbilityNodeInput Input(const FAbilityGraphTestFixture& Fixture)
    {
        FAbilityNodeInput Result;
        Result.SourceToken = Fixture.SourceToken;
        Result.Direction = FVector::ForwardVector;
        return Result;
    }

    /** Mass 保持实际 300HP；零经验避免测试世界进入玩家升级流程。 */
    int32 Spawn(UPureMassSubsystem& Sim, UWorld* World, FVector Position)
    {
        auto* Type = NewObject<UPureMassMonsterType>(World);
        Type->Health = 300.f;
        Type->XPReward = 0;
        Type->Speed = 100.f;
        return Sim.SpawnMonster(Type, Position);
    }

    float Health(UPureMassSubsystem& Sim, int32 Id)
    {
        FPureMassView View;
        return Sim.Resolve(Id, View) ? View.Health : -1.f;
    }

    int32 AddDamage(UAbilityGraphDefinition& Definition)
    {
        auto* Config = NewObject<UAbilityDamageNodeConfig>(&Definition);
        Config->Damage = 25.f;
        Config->DamagePerLevel = 0.f;
        Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
        return Add(Definition, TEXT("Effect.Damage"), Config);
    }

    int32 AddSlow(UAbilityGraphDefinition& Definition)
    {
        auto* Config = NewObject<UAbilitySlowNodeConfig>(&Definition);
        Config->Multiplier = .6f;
        return Add(Definition, TEXT("Status.Slow"), Config);
    }

    /** 同一位置输出可独立搭配范围和效果；不在调度器内调用 Hit。 */
    void AddAreaDamage(UAbilityGraphDefinition& Definition, FName Port, float Radius, bool bVisibility = true)
    {
        auto* Query = NewObject<UAbilityQueryNodeConfig>(&Definition);
        Query->Query.Radius = Radius;
        Query->Query.bIncludeActors = false;
        Query->Query.bRequireVisibility = bVisibility;
        const int32 QueryIndex = Add(Definition, TEXT("Query.Shape"), Query);
        const int32 DamageIndex = AddDamage(Definition);
        Connect(Definition, 0, Port, QueryIndex);
        Connect(Definition, QueryIndex, TEXT("TargetFound"), DamageIndex);
    }

    /** 飞行仍由真实 ProjectileMovement 推进，图只负责生命周期和后继。 */
    void TickProjectiles(UWorld* World, UAbilityGraphInstance& Run, float Duration)
    {
        for (float Age = 0.f; Age + UE_SMALL_NUMBER < Duration && !Run.IsFinished(); Age += .01f)
        {
            TArray<TWeakObjectPtr<AMyProjectile>> Flying;
            for (TActorIterator<AMyProjectile> It(World); It; ++It)
            {
                Flying.Add(*It);
            }
            for (auto Projectile : Flying)
            {
                if (Projectile.IsValid() && !Projectile->IsActorBeingDestroyed())
                {
                    Projectile->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
            }
            Run.Advance(.01f);
        }
    }
} // namespace AbilityDynamicCadenceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitTargetPayloadTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaitArmingAndReplaceablePayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Triggered 只授权位置；后继在真实范围查询后分别施加伤害与状态。 */
bool FWaitTargetPayloadTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    const int32 Id = Spawn(*Sim, F.World, FVector(100, 0, 0));
    auto* Config = NewObject<UAbilityWaitTargetNodeConfig>(F.World);
    Config->ArmDelay = .5f;
    Config->Duration = 2.f;
    auto* Definition = Graph(F.World, TEXT("Query.WaitForTarget"), Config);
    AddAreaDamage(*Definition, TEXT("Triggered"), 180.f);
    const int32 Slow = AddSlow(*Definition);
    Connect(*Definition, 1, TEXT("TargetFound"), Slow);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Triggered = 0;
    bool bPositionOnly = false;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("Triggered"))
            {
                ++Triggered;
                bPositionOnly = !Event.Input.LiveTargetToken.IsSet() && Event.Input.Origin.IsNearlyZero();
            }
        });
    FString Error;
    const auto Nodes = Registry();
    auto Payload = Input(F);
    FAbilityTargetToken InitialTarget;
    TestTrue(TEXT("Extra live input target is captured"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), InitialTarget, Error));
    Payload.LiveTargetToken = InitialTarget;
    TestTrue(TEXT("Wait graph starts"), Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
    Run->Advance(.25f);
    TestEqual(TEXT("No payload before arming"), Health(*Sim, Id), 300.f);
    TestEqual(TEXT("No early trigger"), Triggered, 0);
    Run->Advance(.25f);
    TestEqual(TEXT("Armed target causes exactly one trigger"), Triggered, 1);
    TestTrue(TEXT("Triggered is an origin payload, not a fabricated live target"), bPositionOnly);
    TestEqual(TEXT("Independent area damage was actually applied"), Health(*Sim, Id), 275.f);
    const auto& Fragment =
        F.World->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager().GetFragmentDataChecked<FPureMassFragment>(
            Sim->GetEntity(Id));
    TestEqual(TEXT("Independent slow payload uses the actual Mass state service"), Fragment.SpellSlows.Num(), 1);
    TestEqual(TEXT("Independent status has completed"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);
    Run->Advance(1.f);
    TestEqual(TEXT("Completed trap cannot damage a second time"), Health(*Sim, Id), 275.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitTargetDeadlineCancelTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaitDeadlineAndCancel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 截止是总寿命，不在跨过 ArmDelay 的大帧后再开启一段新寿命。 */
bool FWaitTargetDeadlineCancelTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    const int32 Id = Spawn(*Sim, F.World, FVector(150, 0, 0));
    const auto Nodes = Registry();
    auto* Config = NewObject<UAbilityWaitTargetNodeConfig>(F.World);
    Config->Radius = 100.f;
    Config->ArmDelay = 0.f;
    Config->Duration = .5f;
    auto* Definition = Graph(F.World, TEXT("Query.WaitForTarget"), Config);
    AddAreaDamage(*Definition, TEXT("Completed"), 200.f);
    FString Error;
    auto* Cancelled = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Cancellable wait starts"), Cancelled->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Cancelled->Advance(.2f);
    Cancelled->Cancel();
    Cancelled->Advance(1.f);
    TestEqual(TEXT("Cancellation does not execute expiry explosion"), Health(*Sim, Id), 300.f);
    auto* Expired = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Finite wait starts"), Expired->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Expired->Advance(.5f);
    TestTrue(TEXT("Finite wait completes at the configured hard deadline"), Expired->IsFinished());
    TestEqual(TEXT("Normal completion can explicitly route a different expiry payload"), Health(*Sim, Id), 275.f);

    Config->Radius = 300.f;
    Config->ArmDelay = .75f;
    Config->Duration = 1.f;
    auto* Hitch = NewObject<UAbilityGraphInstance>(F.World);
    int32 Triggers = 0;
    Hitch->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Triggers += Event.OutputPort == TEXT("Triggered");
        });
    TestTrue(TEXT("Hitch case starts"),
             Hitch->StartWithInput(Graph(F.World, TEXT("Query.WaitForTarget"), Config), Input(F), {}, Nodes, Error));
    Hitch->Advance(2.f);
    TestTrue(TEXT("Large delta cannot extend arming plus total lifetime"), Hitch->IsFinished());
    TestEqual(TEXT("Exclusive deadline is checked before a late trigger"), Triggers, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitTargetVisibilityTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaitExplicitVisibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 原 Actor 陷阱的存在检测不要求 LOS，原 Mixed 陷阱要求；新配置必须显式保留。 */
bool FWaitTargetVisibilityTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    const int32 Id = Spawn(*Sim, F.World, FVector(100, 0, 0));
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->AddInstanceComponent(Box);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 200, 200));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(40, 0, 0));
    auto* Config = NewObject<UAbilityWaitTargetNodeConfig>(F.World);
    Config->ArmDelay = 0.f;
    Config->Duration = 2.f;
    const auto Nodes = Registry();
    FString Error;
    for (bool bVisible : {true, false})
    {
        Config->bRequireVisibility = bVisible;
        auto* Definition = Graph(F.World, TEXT("Query.WaitForTarget"), Config);
        AddAreaDamage(*Definition, TEXT("Triggered"), 180.f, false);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TestTrue(TEXT("Explicit LOS wait starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(.1f);
        TestEqual(TEXT("Explicit visibility selects correct trap trigger policy"), Health(*Sim, Id),
                  bVisible ? 300.f : 275.f);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWavefrontGeometryPrimitiveTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.SharedWaveGeometry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 保护原波形 float 距离和 double 平方距离的不同边界，而不以近似比较统一它们。 */
bool FWavefrontGeometryPrimitiveTest::RunTest(const FString&)
{
    using namespace AbilityBehaviorPrimitives;
    const FVector Zero = FVector::ZeroVector;
    TestTrue(TEXT("First shell includes the center"),
             ContainsWavePoint(Zero, Zero, 0.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestFalse(TEXT("Later shell excludes its previous inner boundary"),
              ContainsWavePoint(Zero, FVector(50, 0, 0), 50.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestTrue(TEXT("Growing sphere includes already passed geometry"),
             ContainsWavePoint(Zero, FVector(50, 0, 0), 50.f, 100.f, EAbilityWaveCoverage::GrowingSphere3D));
    TestTrue(TEXT("Shell includes its outer boundary"),
             ContainsWavePoint(Zero, FVector(100, 0, 0), 50.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestFalse(TEXT("Wave geometry is 3D, not an implicit plane"),
              ContainsWavePoint(Zero, FVector(0, 0, 110), 0.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestTrue(TEXT("Actor shell preserves the original float magnitude conversion"),
             ContainsWavePoint(Zero, FVector(100.000001, 0, 0), 0.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestFalse(TEXT("Mixed growing sphere preserves the original squared comparison"),
              ContainsWavePoint(Zero, FVector(100.000001, 0, 0), 0.f, 100.f, EAbilityWaveCoverage::GrowingSphere3D));
    TestFalse(TEXT("Invalid backward radius fails closed"),
              ContainsWavePoint(Zero, FVector(80, 0, 0), 200.f, 100.f, EAbilityWaveCoverage::SweptShell3D));
    TestFalse(TEXT("Unknown coverage fails closed"),
              ContainsWavePoint(Zero, Zero, 0.f, 100.f, static_cast<EAbilityWaveCoverage>(255)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWavefrontReplaceablePayloadTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaveCoverageAndReplaceablePayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Wave 仅发布空间选中的原目标；Damage 可以换成 Slow，growing 与 shell 不混为一谈。 */
bool FWavefrontReplaceablePayloadTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    const auto Nodes = Registry();
    for (const auto Profile : {EAbilityWaveQueryProfile::ActorFront3D, EAbilityWaveQueryProfile::MixedGrowing3D,
                               EAbilityWaveQueryProfile::Batch6Front3D, EAbilityWaveQueryProfile::Batch6Growing3D})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        Sim->Enable();
        const int32 Inner = Spawn(*Sim, F.World, FVector(50, 0, 0));
        const int32 Boundary = Spawn(*Sim, F.World, FVector(100, 0, 0));
        auto* Config = NewObject<UAbilityWavefrontNodeConfig>(F.World);
        Config->Radius = 400.f;
        Config->Speed = 1000.f;
        Config->Duration = 10.f;
        Config->Profile = Profile;
        auto* Definition = Graph(F.World, TEXT("Spatial.Wavefront"), Config);
        const int32 Damage = AddDamage(*Definition);
        const int32 Slow = AddSlow(*Definition);
        Connect(*Definition, 0, TEXT("TargetFound"), Damage);
        Connect(*Definition, 0, TEXT("TargetFound"), Slow);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Targets = 0;
        bool bOriginPreserved = true;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("TargetFound"))
                {
                    ++Targets;
                    bOriginPreserved &= Event.Input.Origin.IsNearlyZero() && !Event.Input.EventCenter.IsNearlyZero();
                }
            });
        FString Error;
        TestTrue(TEXT("Wave graph starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(.1f);
        TestEqual(TEXT("First spatial step applies independent payload at inner point"), Health(*Sim, Inner), 275.f);
        TestEqual(TEXT("First step includes exact outer boundary"), Health(*Sim, Boundary), 275.f);
        const int32 EnteredBehind = Spawn(*Sim, F.World, FVector(30, 0, 0));
        Run->Advance(.1f);
        const bool bGrowing =
            Profile == EAbilityWaveQueryProfile::MixedGrowing3D || Profile == EAbilityWaveQueryProfile::Batch6Growing3D;
        TestEqual(TEXT("Profile decides whether late inner entry is covered"), Health(*Sim, EnteredBehind),
                  bGrowing ? 275.f : 300.f);
        TestEqual(TEXT("Previously hit original life is not hit repeatedly"), Health(*Sim, Inner), 275.f);
        TestEqual(TEXT("Target count reflects geometry, not embedded damage"), Targets, bGrowing ? 3 : 2);
        TestTrue(TEXT("Wave keeps center origin and publishes target position separately"), bOriginPreserved);
        const auto& Fragment =
            F.World->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager().GetFragmentDataChecked<FPureMassFragment>(
                Sim->GetEntity(Inner));
        TestEqual(TEXT("Wave can apply a state payload without a separate complete GA"), Fragment.SpellSlows.Num(), 1);
        Run->Advance(.2f);
        TestTrue(TEXT("Reached radius completes wave and descendants"), Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWavefrontFinalBoundaryTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaveFinalBoundaryProfiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 普通 Mixed 在寿命边界先结束，Actor/第六批先消费最后的有效片段。 */
bool FWavefrontFinalBoundaryTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    const auto Nodes = Registry();
    for (const auto Profile : {EAbilityWaveQueryProfile::ActorFront3D, EAbilityWaveQueryProfile::MixedGrowing3D,
                               EAbilityWaveQueryProfile::Batch6Front3D, EAbilityWaveQueryProfile::Batch6Growing3D})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        Sim->Enable();
        const int32 Id = Spawn(*Sim, F.World, FVector(80, 0, 0));
        auto* Config = NewObject<UAbilityWavefrontNodeConfig>(F.World);
        Config->Radius = 500.f;
        Config->Speed = 100.f;
        Config->Duration = 1.f;
        Config->Profile = Profile;
        auto* Definition = Graph(F.World, TEXT("Spatial.Wavefront"), Config);
        const int32 Damage = AddDamage(*Definition);
        Connect(*Definition, 0, TEXT("TargetFound"), Damage);
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Finite boundary wave starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(2.f);
        TestEqual(TEXT("Exact original final-frame policy controls real payload"), Health(*Sim, Id),
                  Profile == EAbilityWaveQueryProfile::MixedGrowing3D ? 300.f : 275.f);
        TestTrue(TEXT("Every finite profile ends after total duration"), Run->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWavefrontOriginalLifeTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WaveOriginalLifeAndCancel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 尚未被波前接触但已观察的 Actor 也绑定原 lease，不能在同地址复用后重新捕获。 */
bool FWavefrontOriginalLifeTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    FAbilityGraphTestFixture F;
    auto* Config = NewObject<UAbilityWavefrontNodeConfig>(F.World);
    Config->Radius = 500.f;
    Config->Speed = 100.f;
    Config->Duration = 8.f;
    Config->bIncludeMass = false;
    auto* Definition = Graph(F.World, TEXT("Spatial.Wavefront"), Config);
    const int32 Slow = AddSlow(*Definition);
    Connect(*Definition, 0, TEXT("TargetFound"), Slow);
    const auto Nodes = Registry();
    auto* Original = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Original life wave starts"), Original->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Original->Advance(1.f);
    TestEqual(TEXT("Target beyond first front has not received status"), UBatch6SlowComponent::GetMultiplier(F.Target),
              1.f);
    F.Target->EnableDungeonPooling();
    Original->Advance(1.f);
    TestEqual(TEXT("Old observed life does not turn into new pooled lease"),
              UBatch6SlowComponent::GetMultiplier(F.Target), 1.f);
    Original->Cancel();
    auto* Fresh = NewObject<UAbilityGraphInstance>(F.World);
    int32 Events = 0;
    Fresh->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Events += Event.OutputPort == TEXT("TargetFound");
        });
    TestTrue(TEXT("Fresh wave captures the actual current lease"),
             Fresh->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    Fresh->Advance(2.f);
    TestTrue(TEXT("Fresh query applies real state to the current life"),
             FMath::IsNearlyEqual(UBatch6SlowComponent::GetMultiplier(F.Target), .6f));
    TestEqual(TEXT("Only one state payload was published"), Events, 1);
    Fresh->Cancel();
    Fresh->Advance(5.f);
    TestEqual(TEXT("Cancellation cannot publish more target events"), Events, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterRealDeliveryTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.EmitterRealDeliveryAndRootBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Launch 接真实投递，命中后再独立结算效果；发射调度不能直接扣血或另起根预算。 */
bool FEmitterRealDeliveryTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    const int32 Id = Spawn(*Sim, F.World, FVector(300, 0, 0));
    auto* Config = NewObject<UAbilityEmitterNodeConfig>(F.World);
    Config->Count = 2;
    Config->Period = .1f;
    Config->Duration = 2.f;
    auto* Definition = Graph(F.World, TEXT("Control.Emitter"), Config);
    auto* Delivery = NewObject<UAbilityDeliveryNodeConfig>(Definition);
    Delivery->Delivery.ProjectileClass = AMyProjectile::StaticClass();
    Delivery->Delivery.Speed = 600.f;
    Delivery->Delivery.CollisionRadius = 8.f;
    Delivery->Delivery.bApplyDamage = false;
    const int32 Flight = Add(*Definition, TEXT("Delivery.ProjectileAwait"), Delivery);
    const int32 Resolve =
        Add(*Definition, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Definition));
    const int32 Damage = AddDamage(*Definition);
    const int32 Slow = AddSlow(*Definition);
    Connect(*Definition, 0, TEXT("Launch"), Flight);
    Connect(*Definition, Flight, TEXT("Hit"), Resolve);
    Connect(*Definition, Resolve, TEXT("TargetFound"), Damage);
    Connect(*Definition, Resolve, TEXT("TargetFound"), Slow);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Launches = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Launches += Event.OutputPort == TEXT("Launch");
        });
    auto Payload = Input(F);
    Payload.Direction = FVector::RightVector;
    const auto Nodes = Registry();
    FString Error;
    TestTrue(TEXT("Emitter-delivery-payload graph starts"), Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
    Run->Advance(.1f);
    TestEqual(TEXT("First admitted launch is a real child call"), Launches, 1);
    TestEqual(TEXT("Target is not damaged before flight"), Health(*Sim, Id), 300.f);
    Run->Advance(.1f);
    TestEqual(TEXT("Finite emitter admits exactly configured child calls"), Launches, 2);
    TestFalse(TEXT("Two physical deliveries consumed the same root spawn budget"), Run->ReservePhysicalSpawn(63));
    TickProjectiles(F.World, *Run, 2.f);
    TestEqual(TEXT("Two actual projectile contacts each apply one independent payload"), Health(*Sim, Id), 250.f);
    const auto& Fragment =
        F.World->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager().GetFragmentDataChecked<FPureMassFragment>(
            Sim->GetEntity(Id));
    TestEqual(TEXT("Projectile contacts can apply the alternate state payload"), Fragment.SpellSlows.Num(), 1);
    TestTrue(TEXT("Emitter and both admitted deliveries finish under one graph"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterCadenceAdmissionTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.EmitterCadenceAndAdmission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Count 只统计准入成功，空闲/拒绝不会伪装成完成；显式保留两类周期追赶合同。 */
bool FEmitterCadenceAdmissionTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    const auto Nodes = Registry();
    for (const auto Profile : {EAbilityEmitterProfile::ActorCatchUp, EAbilityEmitterProfile::MixedEight})
    {
        FAbilityGraphTestFixture F;
        auto* Config = NewObject<UAbilityEmitterNodeConfig>(F.World);
        Config->Profile = Profile;
        Config->Count = 32;
        Config->Period = .1f;
        Config->Duration = 10.f;
        auto* Definition = Graph(F.World, TEXT("Control.Emitter"), Config);
        const int32 Slow = AddSlow(*Definition);
        Connect(*Definition, 0, TEXT("Launch"), Slow);
        int32 Launches = 0;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Launches += Event.OutputPort == TEXT("Launch");
            });
        FString Error;
        TestTrue(TEXT("Explicit emitter cadence starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(2.5f);
        TestEqual(TEXT("Per-frame successful launch count preserves the old profile"), Launches,
                  Profile == EAbilityEmitterProfile::ActorCatchUp ? 25 : 8);
        Run->Advance(0.f);
        TestEqual(TEXT("Only Mixed retains eight-pulse backlog for next frame"), Launches,
                  Profile == EAbilityEmitterProfile::ActorCatchUp ? 25 : 16);
        Run->Cancel();

        Config->Count = 1;
        Config->Duration = 2.f;
        auto* NoEdge = NewObject<UAbilityGraphInstance>(F.World);
        TestTrue(TEXT("Emitter with no Launch edge remains valid finite behavior"),
                 NoEdge->StartWithInput(Graph(F.World, TEXT("Control.Emitter"), Config), Input(F), {}, Nodes, Error));
        NoEdge->Advance(.5f);
        TestFalse(TEXT("Rejected child does not satisfy successful Count"), NoEdge->IsFinished());
        TestEqual(TEXT("Rejected child does not publish fake Launch"), NoEdge->GetAcceptedEventCount(), uint64(0));
        NoEdge->Cancel();

        Config->Count = 2;
        auto* LimitedDefinition = Graph(F.World, TEXT("Control.Emitter"), Config);
        LimitedDefinition->Budget.MaxInvocations = 2;
        const int32 LimitedSlow = AddSlow(*LimitedDefinition);
        Connect(*LimitedDefinition, 0, TEXT("Launch"), LimitedSlow);
        auto* Limited = NewObject<UAbilityGraphInstance>(F.World);
        int32 Admitted = 0;
        Limited->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Admitted += Event.OutputPort == TEXT("Launch");
            });
        TestTrue(TEXT("Root-budget emitter starts"),
                 Limited->StartWithInput(LimitedDefinition, Input(F), {}, Nodes, Error));
        Limited->Advance(.3f);
        TestEqual(TEXT("Same root only admits one successor under MaxInvocations 2"), Admitted, 1);
        TestFalse(TEXT("Budget-rejected successor did not count as a second success"), Limited->IsFinished());
        Limited->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmitterIdleAndBoundaryTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.EmitterIdleAndFinalBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 空闲丢弃时间的方式及截止前后次序不同，不能只以统一 Repeat 替代。 */
bool FEmitterIdleAndBoundaryTest::RunTest(const FString&)
{
    using namespace AbilityDynamicCadenceTests;
    const auto Nodes = Registry();
    for (const auto Profile : {EAbilityEmitterProfile::ActorCatchUp, EAbilityEmitterProfile::MixedEight})
    {
        FAbilityGraphTestFixture F;
        F.Target->Destroy();
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        Sim->Enable();
        auto* Config = NewObject<UAbilityEmitterNodeConfig>(F.World);
        Config->Profile = Profile;
        Config->Count = 3;
        Config->Period = .1f;
        Config->Duration = 2.f;
        auto* Definition = Graph(F.World, TEXT("Control.Emitter"), Config);
        const int32 Slow = AddSlow(*Definition);
        Connect(*Definition, 0, TEXT("Launch"), Slow);
        int32 Launches = 0;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                Launches += Event.OutputPort == TEXT("Launch");
            });
        FString Error;
        TestTrue(TEXT("Idle emitter starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(.35f);
        TestEqual(TEXT("No target means no successful emission"), Launches, 0);
        Spawn(*Sim, F.World, FVector(100, 0, 0));
        Run->Advance(.051f);
        TestEqual(TEXT("Actor keeps modulo fraction; Mixed clears idle clock"), Launches,
                  Profile == EAbilityEmitterProfile::ActorCatchUp ? 1 : 0);
        Run->Cancel();

        Config->Count = 3;
        Config->Period = .5f;
        Config->Duration = .5f;
        auto* BoundaryDefinition = Graph(F.World, TEXT("Control.Emitter"), Config);
        const int32 BoundarySlow = AddSlow(*BoundaryDefinition);
        Connect(*BoundaryDefinition, 0, TEXT("Launch"), BoundarySlow);
        auto* Boundary = NewObject<UAbilityGraphInstance>(F.World);
        int32 FinalLaunches = 0;
        Boundary->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                FinalLaunches += Event.OutputPort == TEXT("Launch");
            });
        TestTrue(TEXT("Exact boundary emitter starts"),
                 Boundary->StartWithInput(BoundaryDefinition, Input(F), {}, Nodes, Error));
        Boundary->Advance(1.f);
        TestEqual(TEXT("Actor consumes final partial frame; Mixed expires first"), FinalLaunches,
                  Profile == EAbilityEmitterProfile::ActorCatchUp ? 1 : 0);
        TestTrue(TEXT("Finite emitter still honors hard lifetime"), Boundary->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTemporalConfigurationTest,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.InvalidConfigurations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 新动态配置错误在启动前拒绝，不夹取为另一个行为，也不开放无限权限。 */
bool FTemporalConfigurationTest::RunTest(const FString&)
{
    FString Error;
    auto* Wait = NewObject<UAbilityWaitTargetNodeConfig>();
    Wait->Radius = 0.f;
    TestFalse(TEXT("Wait rejects zero radius"), Wait->Validate(Error));
    Wait->Radius = 300.f;
    Wait->ArmDelay = -1.f;
    TestFalse(TEXT("Wait rejects negative arm delay"), Wait->Validate(Error));
    Wait->ArmDelay = 0.f;
    Wait->Duration = 0.f;
    TestFalse(TEXT("Wait rejects unbounded lifetime"), Wait->Validate(Error));
    auto* Wave = NewObject<UAbilityWavefrontNodeConfig>();
    Wave->Speed = 0.f;
    TestFalse(TEXT("Wave rejects zero speed"), Wave->Validate(Error));
    Wave->Speed = 100.f;
    Wave->Profile = static_cast<EAbilityWaveQueryProfile>(255);
    TestFalse(TEXT("Wave rejects unknown profile"), Wave->Validate(Error));
    auto* Emitter = NewObject<UAbilityEmitterNodeConfig>();
    Emitter->Count = 0;
    TestFalse(TEXT("Emitter rejects infinite/zero count"), Emitter->Validate(Error));
    Emitter->Count = 65;
    TestFalse(TEXT("Emitter rejects count outside finite admission"), Emitter->Validate(Error));
    Emitter->Count = 3;
    Emitter->Period = 0.f;
    TestFalse(TEXT("Emitter rejects zero period"), Emitter->Validate(Error));
    Emitter->Period = .5f;
    Emitter->Profile = static_cast<EAbilityEmitterProfile>(255);
    TestFalse(TEXT("Emitter rejects unknown cadence"), Emitter->Validate(Error));
    Emitter->Profile = EAbilityEmitterProfile::ActorCatchUp;
    Emitter->bIncludeActors = Emitter->bIncludeMass = false;
    TestFalse(TEXT("Emitter rejects no target representations"), Emitter->Validate(Error));
    return true;
}

#endif
