// 调用链：Automation -> 内存 Schema2 批查询 -> 不可变 TargetSet -> 原伤害服务 -> 同根命中后继。
// 观察真实 Mass/ASC 属性、原生命与几何边界；不改 Content，不激活另一个完整 GA。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"

namespace AbilityBatchCombatNodeTests
{
    /** 本地工厂登记真实策略，避免把全局接线时点误当作行为证据。 */
    FAbilityNodeRegistry Registry()
    {
        FAbilityNodeRegistry Result;
        Result.RegisterBuiltins();
        AbilityBatchCombatNodes::Register(Result);
        AbilityBehaviorNodes::Register(Result);
        return Result;
    }

    /** 新增配置节点，同一种 TypeId 可有多个独立参数实例和 NodeId。 */
    int32 Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        return Definition.Nodes.Add(Node);
    }

    /** 创建本次测试独享图，普通有限预算不扩展为无限模式。 */
    UAbilityGraphDefinition* Graph(UWorld* World, FName Type, UAbilityNodeConfig* Config)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(World);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        Add(*Result, Type, Config);
        Result->EntryNodeId = Result->Nodes[0].NodeId;
        return Result;
    }

    /** 通过真实声明端口连接，不绕过 GraphInstance 直接调用动作。 */
    void Connect(UAbilityGraphDefinition& Definition, int32 From, FName Port, int32 To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = Definition.Nodes[From].NodeId;
        Edge.OutputPort = Port;
        Edge.To = Definition.Nodes[To].NodeId;
        Definition.Edges.Add(Edge);
    }

    /** 只传入服务器夹具捕获的来源，伤害 GE 复用固定真实 Health 修饰测试类。 */
    FAbilityNodeInput Input(const FAbilityGraphTestFixture& Fixture)
    {
        FAbilityNodeInput Result;
        Result.SourceToken = Fixture.SourceToken;
        Result.Direction = FVector::ForwardVector;
        Result.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        return Result;
    }

    /** 构造只读集合；弱目标身份不会由这个快照保活或刷新租约。 */
    TSharedPtr<const FAbilityTargetSetSnapshot> Set(const FAbilitySourceToken& Source,
                                                    const TArray<FAbilityTargetToken>& Targets)
    {
        auto Result = MakeShared<FAbilityTargetSetSnapshot>();
        Result->Source = Source;
        Result->Targets = Targets;
        return Result;
    }

    /** 使用实际 Mass 管理器的 300HP 怪物，不以事件计数代替扣血。 */
    int32 Spawn(UPureMassSubsystem& Sim, UWorld* World, FVector Location)
    {
        auto* Type = NewObject<UPureMassMonsterType>(World);
        Type->Health = 300.f;
        Type->XPReward = 0;
        return Sim.SpawnMonster(Type, Location);
    }

    /** 读取实际查询快照，移除后的实体不伪装成零血成功。 */
    float Health(UPureMassSubsystem& Sim, int32 Id)
    {
        FPureMassView View;
        return Sim.Resolve(Id, View) ? View.Health : -1.f;
    }

    /** 为 Actor 实验建立真实可变化的 Health，保留原 ASC 和 AttributeSet。 */
    void PrepareHealth(AEnemyCharacter& Target)
    {
        auto* ASC = Target.GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    }

    /** 第二个独立敌人，用于整批先扣血与单个旧租约失效实验。 */
    AEnemyCharacter* OtherTarget(FAbilityGraphTestFixture& Fixture, FVector Location)
    {
        auto* Result = Fixture.World->SpawnActor<AEnemyCharacter>();
        Result->Tags.AddUnique(TEXT("Enemy"));
        Result->SetActorLocation(Location);
        auto* ASC = Result->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(Result, Result);
        ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Result));
        PrepareHealth(*Result);
        return Result;
    }

    /** 返回以现有单目标配置为基类的实际批效果，数值修正仍走同一继承通路。 */
    UAbilityDamageBatchNodeConfig* Damage(UObject* Outer)
    {
        auto* Result = NewObject<UAbilityDamageBatchNodeConfig>(Outer);
        Result->Damage = 25.f;
        Result->DamagePerLevel = 0.f;
        Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
        return Result;
    }

    /** 明确 fresh 证明的同步查询，用来独立验证几何而不混入图账本。 */
    bool Gather(const FAbilitySourceToken& Source, const FAbilityQueryRequest& Query, TArray<FAbilityTargetToken>& Out,
                FString& Error)
    {
        auto Resolve = [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location,
                           FString& CaptureError)
        {
            return AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Token, CaptureError) &&
                   AbilityExecutionIdentity::ResolveTarget(Source, Token, Location);
        };
        return AbilityTargetQueryService::GatherBatch(Source, Query, Resolve, Out, Error);
    }
} // namespace AbilityBatchCombatNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCombatCrowdTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.RealCrowdWithoutPerTargetInvocations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 321 个真实目标共享两个细节点，不能因为逐目标 Effect 调用达到 64 而截断原范围伤害。 */
bool FBatchCombatCrowdTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    TArray<int32> Targets;
    for (int32 Index = 0; Index < 321; ++Index)
    {
        Targets.Add(Spawn(*Sim, F.World, FVector(150 + Index % 20, Index / 20, 0)));
    }
    auto* Query = NewObject<UAbilityQueryBatchNodeConfig>(F.World);
    Query->Query.Radius = 500.f;
    Query->Query.MaxTargets = 512;
    Query->Query.bIncludeActors = false;
    Query->Query.bRequireVisibility = false;
    auto* Definition = Graph(F.World, TEXT("Query.ShapeBatch"), Query);
    Definition->Budget.MaxInvocations = 4;
    const int32 Effect = Add(*Definition, TEXT("Effect.DamageBatch"), Damage(Definition));
    Connect(*Definition, 0, TEXT("Completed"), Effect);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    const auto Nodes = Registry();
    TestTrue(TEXT("Complete crowd graph starts under four-call budget"),
             Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    int32 Damaged = 0;
    for (int32 Id : Targets)
    {
        Damaged += Health(*Sim, Id) == 275.f;
    }
    TestEqual(TEXT("Every real Mass target receives exactly one actual damage action"), Damaged, Targets.Num());
    TestTrue(TEXT("Root graph completes without silently truncating crowd"), Run->IsFinished());
    TestEqual(TEXT("Crowd is not reported as budget exhaustion"), Run->GetEndReason(),
              EAbilityGraphEndReason::Completed);
    TestTrue(TEXT("No per-target diagnostic event flood without an OnHit consumer"), Run->GetAcceptedEventCount() <= 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchEmptyContinuationTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.EmptySetContinues",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 空场仍向后继传递合法空集合，让 OnPeriod/到期链继续而不是无声停在查询节点。 */
bool FBatchEmptyContinuationTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Query = NewObject<UAbilityQueryBatchNodeConfig>(F.World);
    auto* Definition = Graph(F.World, TEXT("Query.ShapeBatch"), Query);
    const int32 Effect = Add(*Definition, TEXT("Effect.DamageBatch"), Damage(Definition));
    auto* Delay = NewObject<UAbilityDelayNodeConfig>(Definition);
    Delay->DelaySeconds = .1f;
    const int32 Tail = Add(*Definition, TEXT("Control.DelayOrigin"), Delay);
    Connect(*Definition, 0, TEXT("Completed"), Effect);
    Connect(*Definition, Effect, TEXT("Completed"), Tail);
    int32 EmptySets = 0;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            EmptySets += Event.Input.TargetSet.IsValid() && Event.Input.TargetSet->Targets.IsEmpty();
        });
    const auto Nodes = Registry();
    FString Error;
    TestTrue(TEXT("Empty-target batch starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    TestTrue(TEXT("Empty query actually publishes a typed set"), EmptySets > 0);
    TestFalse(TEXT("Downstream tail is alive, not skipped by empty query"), Run->IsFinished());
    Run->Advance(.1f);
    TestTrue(TEXT("Empty branch and its real tail finish normally"), Run->IsFinished());
    TestEqual(TEXT("Empty is not failed target permission"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchQueryCapTest, "test_25d.UnifiedGA.DynamicMage.BatchCombat.CompleteSetHardCap",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 旧 Gather 仍可取前缀；新的批服务完整成功或明确拒绝，不丢失后半个范围。 */
bool FBatchQueryCapTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
    Sim->Enable();
    for (int32 Index = 0; Index < 11; ++Index)
    {
        Spawn(*Sim, F.World, FVector(100 + Index, 0, 0));
    }
    FAbilityQueryRequest Query;
    Query.Radius = 300.f;
    Query.MaxTargets = 12;
    Query.bIncludeActors = false;
    TArray<FAbilityTargetToken> Targets;
    FString Error;
    TestTrue(TEXT("Complete batch query succeeds inside the actual cap"), Gather(F.SourceToken, Query, Targets, Error));
    TestEqual(TEXT("Complete set retains every eligible target"), Targets.Num(), 11);
    Query.MaxTargets = 10;
    TestFalse(TEXT("Batch query rejects a cap smaller than the complete result"),
              Gather(F.SourceToken, Query, Targets, Error));
    TestEqual(TEXT("Rejected batch does not return a misleading valid prefix"), Targets.Num(), 0);
    TestFalse(TEXT("Cap failure contains a real diagnostic"), Error.IsEmpty());
    TestTrue(TEXT("Original single-query API still accepts capped prefix"),
             AbilityTargetQueryService::Gather(F.SourceToken, Query, Targets, Error));
    TestEqual(TEXT("Original prefix contract is preserved"), Targets.Num(), 10);
    Query.MaxTargets = 65536;
    TestTrue(TEXT("Batch validates its explicit large hard limit"), Query.ValidateForBatch(Error));
    TestFalse(TEXT("Original validation retains its 256-target contract"), Query.Validate(Error));
    Query.MaxTargets = 65537;
    TestFalse(TEXT("Batch rejects beyond validated memory/work bound"), Query.ValidateForBatch(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchGeometryProfilesTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.LegacyGeometryProfiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 用真实查询结果区分三维、角容差、float 径向/横向和 broadphase 的精度差异。 */
bool FBatchGeometryProfilesTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    auto Check =
        [&](FAbilityQueryRequest Query, FVector Location, int32 Generic, int32 Actor, int32 Mixed, const TCHAR* Label)
    {
        FAbilityGraphTestFixture F;
        F.Target->SetActorLocation(Location);
        Query.bIncludeMass = false;
        Query.bRequireVisibility = false;
        const int32 Expected[] = {Generic, Actor, Mixed};
        int32 Index = 0;
        for (const auto Profile : {EAbilityShapeQueryProfile::Generic, EAbilityShapeQueryProfile::LegacyActor,
                                   EAbilityShapeQueryProfile::LegacyMixed})
        {
            Query.Profile = Profile;
            TArray<FAbilityTargetToken> Targets;
            FString Error;
            TestTrue(FString::Printf(TEXT("%s profile %d query succeeds"), Label, Index),
                     Gather(F.SourceToken, Query, Targets, Error));
            TestEqual(FString::Printf(TEXT("%s profile %d actual target count"), Label, Index), Targets.Num(),
                      Expected[Index]);
            ++Index;
        }
    };
    FAbilityQueryRequest Cone;
    Cone.Geometry = EAbilityQueryGeometry::Cone;
    Cone.Radius = 250.f;
    Cone.HalfHeight = 10.f;
    Check(Cone, FVector(100, 0, 200), 0, 1, 1, TEXT("Legacy cone has no height slice"));
    Cone.HalfHeight = 300.f;
    Check(Cone, FVector(100, 0, 240), 1, 0, 0, TEXT("Legacy cone radius is 3D"));
    Cone.HalfHeight = 120.f;
    const float Angle = FMath::DegreesToRadians(45.004f);
    Check(Cone, FVector(FMath::Cos(Angle) * 100.f, FMath::Sin(Angle) * 100.f, 0), 1, 1, 0,
          TEXT("Actor angular tolerance differs from Mixed"));
    Check(Cone, FVector(0, 0, 50), 1, 0, 0, TEXT("Legacy origin exception is not planar"));
    FAbilityQueryRequest Ring;
    Ring.Geometry = EAbilityQueryGeometry::Ring;
    Ring.Radius = 100.f;
    Ring.InnerRadius = 0.f;
    Ring.HalfHeight = 10.f;
    Check(Ring, FVector(100.000001, 0, 0), 1, 0, 0, TEXT("Legacy ring uses squared outer boundary"));
    Ring.Radius = 200.f;
    Ring.InnerRadius = 100.f;
    Check(Ring, FVector(99.999999, 0, 0), 1, 0, 0, TEXT("Legacy ring uses squared inner boundary"));
    FAbilityQueryRequest Line;
    Line.Geometry = EAbilityQueryGeometry::Line;
    Line.Range = 200.f;
    Line.Width = 100.f;
    Line.HalfHeight = 10.f;
    Check(Line, FVector(100, 50.000001, 0), 1, 0, 0, TEXT("Legacy line keeps double lateral boundary"));
    Line.Range = 100.15070343017578f;
    Line.Width = 70.0208969116211f;
    Line.HalfHeight = 45.03409957885742f;
    Check(Line, FVector(Line.Range, Line.Width * .5f, Line.HalfHeight), 1, 0, 0,
          TEXT("Legacy broadphase keeps float squared-sum construction"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchOriginalRootLifeTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.RootOriginalLifeAcrossQueries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 每个新 Query invocation 仍使用同根首次证明；FreshQuery 是明确的新选择合同。 */
bool FBatchOriginalRootLifeTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    const auto Nodes = Registry();
    for (const auto Policy : {EAbilityQueryLifePolicy::FreshQuery, EAbilityQueryLifePolicy::OriginalRootLife})
    {
        FAbilityGraphTestFixture F;
        PrepareHealth(*F.Target);
        auto* Repeat = NewObject<UAbilityRepeatNodeConfig>(F.World);
        Repeat->Count = 2;
        Repeat->Period = .1f;
        Repeat->Duration = 2.f;
        auto* Definition = Graph(F.World, TEXT("Control.Repeat"), Repeat);
        auto* Query = NewObject<UAbilityQueryBatchNodeConfig>(Definition);
        Query->Query.Profile = EAbilityShapeQueryProfile::LegacyMixed;
        Query->Query.bIncludeMass = false;
        Query->Query.bRequireVisibility = false;
        Query->LifePolicy = Policy;
        const int32 QueryIndex = Add(*Definition, TEXT("Query.ShapeBatch"), Query);
        const int32 Effect = Add(*Definition, TEXT("Effect.DamageBatch"), Damage(Definition));
        Connect(*Definition, 0, TEXT("Pulse"), QueryIndex);
        Connect(*Definition, QueryIndex, TEXT("Completed"), Effect);
        int32 Effects = 0;
        auto* ASC = F.Target->GetAbilitySystemComponent();
        const auto Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                ++Effects;
                F.Target->EnableDungeonPooling();
            });
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Repeated batch query starts"), Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
        Run->Advance(.1f);
        TestEqual(TEXT("First original query applies an actual GE"), Effects, 1);
        Run->Advance(.1f);
        const int32 Expected = Policy == EAbilityQueryLifePolicy::FreshQuery ? 2 : 1;
        TestEqual(TEXT("Explicit query life policy decides whether new lease is intentionally selected"), Effects,
                  Expected);
        TestEqual(TEXT("Actual Actor health matches the permitted query lives"),
                  ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f - Expected * 10.f);
        TestTrue(TEXT("Repeated batch and empty rejected-life branch complete"), Run->IsFinished());
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchObserveBeforeGeometryTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.OriginalLifeObservedBeforeGeometry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 旧 Mixed 在内圈/LOS拒绝前已经观察原生命，后续换 lease 不能绕开同根缓存。 */
bool FBatchObserveBeforeGeometryTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    FAbilityGraphTestFixture F;
    PrepareHealth(*F.Target);
    F.Target->SetActorLocation(FVector(50, 0, 0));
    auto* First = NewObject<UAbilityQueryBatchNodeConfig>(F.World);
    First->Query.Profile = EAbilityShapeQueryProfile::LegacyMixed;
    First->Query.Geometry = EAbilityQueryGeometry::Ring;
    First->Query.Radius = 200.f;
    First->Query.InnerRadius = 100.f;
    First->Query.bIncludeMass = false;
    First->Query.bRequireVisibility = false;
    First->LifePolicy = EAbilityQueryLifePolicy::OriginalRootLife;
    auto* Definition = Graph(F.World, TEXT("Query.ShapeBatch"), First);
    auto* Delay = NewObject<UAbilityDelayNodeConfig>(Definition);
    Delay->DelaySeconds = .1f;
    const int32 Wait = Add(*Definition, TEXT("Control.DelayOrigin"), Delay);
    auto* Second = NewObject<UAbilityQueryBatchNodeConfig>(Definition);
    Second->Query.Profile = EAbilityShapeQueryProfile::LegacyMixed;
    Second->Query.Radius = 200.f;
    Second->Query.bIncludeMass = false;
    Second->Query.bRequireVisibility = false;
    Second->LifePolicy = EAbilityQueryLifePolicy::OriginalRootLife;
    const int32 Query = Add(*Definition, TEXT("Query.ShapeBatch"), Second);
    const int32 Effect = Add(*Definition, TEXT("Effect.DamageBatch"), Damage(Definition));
    Connect(*Definition, 0, TEXT("Completed"), Wait);
    Connect(*Definition, Wait, TEXT("Completed"), Query);
    Connect(*Definition, Query, TEXT("Completed"), Effect);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    const auto Nodes = Registry();
    TestTrue(TEXT("First ring query starts and observes inner rejected target"),
             Run->StartWithInput(Definition, Input(F), {}, Nodes, Error));
    TestFalse(TEXT("Empty first batch continues into actual delay"), Run->IsFinished());
    F.Target->SetActorLocation(FVector(150, 0, 0));
    F.Target->EnableDungeonPooling();
    Run->Advance(.1f);
    TestEqual(TEXT("Later sphere cannot recapture a geometrically rejected old identity"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    TestTrue(TEXT("Rejected recycled identity produces a normal empty completion"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDamageLocationProfileTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.DamagePositionAndAttemptedProfiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 真实 GE 回调移动目标，Actor 保存伤后位置而 Mixed 保存伤前位置；缺 GE 的门也不同。 */
bool FBatchDamageLocationProfileTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    const auto Nodes = Registry();
    for (const auto Profile : {EAbilityDamageActionProfile::LegacyActor, EAbilityDamageActionProfile::LegacyMixed})
    {
        FAbilityGraphTestFixture F;
        PrepareHealth(*F.Target);
        auto* Config = Damage(F.World);
        Config->Profile = Profile;
        auto* Definition = Graph(F.World, TEXT("Effect.DamageBatch"), Config);
        auto* Anchor = NewObject<UAbilityAnchorNodeConfig>(Definition);
        Anchor->Anchor = EAbilitySpatialAnchor::ConfirmedImpact;
        const int32 Hook = Add(*Definition, TEXT("Spatial.Anchor"), Anchor);
        Connect(*Definition, 0, TEXT("DamageResolved"), Hook);
        auto Payload = Input(F);
        Payload.TargetSet = Set(F.SourceToken, {F.TargetToken});
        auto* ASC = F.Target->GetAbilitySystemComponent();
        const auto Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                F.Target->SetActorLocation(FVector(777, 0, 0));
            });
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Results = 0;
        FVector Origin = FVector::ZeroVector;
        FVector Impact = FVector::ZeroVector;
        bool bHistorical = false;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("DamageResolved"))
                {
                    ++Results;
                    Origin = Event.Input.Origin;
                    if (Event.Input.Impact.IsSet())
                    {
                        Impact = Event.Input.Impact->Location;
                    }
                    bHistorical = Event.Input.bDamageAttempted && !Event.Input.LiveTargetToken.IsSet();
                }
            });
        FString Error;
        TestTrue(TEXT("Profiled batch effect starts"), Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
        TestEqual(TEXT("Actor damage traverses a real GE"),
                  ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        TestEqual(TEXT("One original hit produces one admitted result hook"), Results, 1);
        const FVector Expected =
            Profile == EAbilityDamageActionProfile::LegacyActor ? FVector(777, 0, 0) : FVector(150, 0, 0);
        TestTrue(TEXT("Profile preserves the original child spawn origin"), Origin.Equals(Expected));
        TestTrue(TEXT("Historical impact agrees with the original position policy"), Impact.Equals(Expected));
        TestTrue(TEXT("Damage result does not pretend to retain live control permission"), bHistorical);
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    for (const auto Profile : {EAbilityDamageActionProfile::Unified, EAbilityDamageActionProfile::LegacyActor,
                               EAbilityDamageActionProfile::LegacyMixed})
    {
        FAbilityGraphTestFixture F;
        PrepareHealth(*F.Target);
        auto* Config = Damage(F.World);
        Config->Profile = Profile;
        auto* Definition = Graph(F.World, TEXT("Effect.DamageBatch"), Config);
        auto* Anchor = NewObject<UAbilityAnchorNodeConfig>(Definition);
        Anchor->Anchor = EAbilitySpatialAnchor::ConfirmedImpact;
        const int32 Hook = Add(*Definition, TEXT("Spatial.Anchor"), Anchor);
        Connect(*Definition, 0, TEXT("DamageResolved"), Hook);
        auto Payload = Input(F);
        Payload.DamageParams.DamageGameplayEffectClass = nullptr;
        Payload.TargetSet = Set(F.SourceToken, {F.TargetToken});
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 Results = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("DamageResolved"))
                {
                    ++Results;
                    TestFalse(TEXT("No GE is not reported as a damage attempt"), Event.Input.bDamageAttempted);
                }
            });
        FString Error;
        TestTrue(TEXT("Missing-GE result profile starts without fabricating damage"),
                 Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
        TestEqual(TEXT("Mixed rejects missing-GE OnHit; Actor/generic preserve original result contract"), Results,
                  Profile == EAbilityDamageActionProfile::LegacyMixed ? 0 : 1);
        TestEqual(TEXT("Missing GE does not alter real Actor health"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchRootBeforeChildrenTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.RootDamagePrecedesOnHitChildren",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 兼容图原先是 FIFO 子调用：根 Pulse 整批扣血后，才执行各自 OnHit 子片段。 */
bool FBatchRootBeforeChildrenTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    FAbilityGraphTestFixture F;
    PrepareHealth(*F.Target);
    auto* Other = OtherTarget(F, FVector(300, 0, 0));
    FAbilityTargetToken OtherToken;
    FString Error;
    TestTrue(TEXT("Capture second real Actor life"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget::FromActor(Other), OtherToken,
                                                     Error));
    auto* Definition = Graph(F.World, TEXT("Effect.DamageBatch"), Damage(F.World));
    auto* Query = NewObject<UAbilityQueryBatchNodeConfig>(Definition);
    Query->Query.Radius = 10.f;
    Query->Query.bIncludeMass = false;
    Query->Query.bRequireVisibility = false;
    const int32 ChildQuery = Add(*Definition, TEXT("Query.ShapeBatch"), Query);
    const int32 ChildDamage = Add(*Definition, TEXT("Effect.DamageBatch"), Damage(Definition));
    Connect(*Definition, 0, TEXT("DamageResolved"), ChildQuery);
    Connect(*Definition, ChildQuery, TEXT("Completed"), ChildDamage);
    auto Payload = Input(F);
    Payload.TargetSet = Set(F.SourceToken, {F.TargetToken, OtherToken});
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 RootHooks = 0;
    bool bBothRootsAppliedBeforeHook = true;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("DamageResolved"))
            {
                ++RootHooks;
                bBothRootsAppliedBeforeHook &=
                    F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) ==
                        90.f &&
                    Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()) ==
                        90.f;
            }
        });
    const auto Nodes = Registry();
    TestTrue(TEXT("Root batch plus independently composed OnHit fragment starts"),
             Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
    TestEqual(TEXT("Two original targets each admit one OnHit fragment"), RootHooks, 2);
    TestTrue(TEXT("All root damage completed before first child fragment executes"), bBothRootsAppliedBeforeHook);
    TestEqual(TEXT("First actual Actor receives root and its position-based child damage"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 80.f);
    TestEqual(TEXT("Second actual Actor receives root and its own child damage"),
              Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 80.f);
    TestTrue(TEXT("All admitted descendants finish in the same graph"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchIndividualLifeTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.IndividualLifeAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 单个退休目标不能否决其他合法目标；实际伤害回调取消或换 lease 后不继续作用于新生命。 */
bool FBatchIndividualLifeTest::RunTest(const FString&)
{
    using namespace AbilityBatchCombatNodeTests;
    const auto Nodes = Registry();
    {
        FAbilityGraphTestFixture F;
        PrepareHealth(*F.Target);
        auto* Other = OtherTarget(F, FVector(300, 0, 0));
        FAbilityTargetToken OtherToken;
        FString Error;
        TestTrue(TEXT("Capture valid sibling life before retiring first token"),
                 AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget::FromActor(Other),
                                                         OtherToken, Error));
        F.Target->EnableDungeonPooling();
        auto Payload = Input(F);
        Payload.TargetSet = Set(F.SourceToken, {F.TargetToken, OtherToken});
        auto* Definition = Graph(F.World, TEXT("Effect.DamageBatch"), Damage(F.World));
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TestTrue(TEXT("Partially stale target set starts as a position-owned batch"),
                 Run->StartWithInput(Definition, Payload, {}, Nodes, Error));
        TestEqual(TEXT("Retired original life remains undamaged"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
        TestEqual(TEXT("Other permitted original life still receives real GE"),
                  Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        TestTrue(TEXT("Partial stale set completes normally"), Run->IsFinished());
    }
    for (const bool bCancel : {false, true})
    {
        FAbilityGraphTestFixture F;
        PrepareHealth(*F.Target);
        auto* Other = OtherTarget(F, FVector(300, 0, 0));
        FAbilityTargetToken OtherToken;
        FString Error;
        TestTrue(TEXT("Capture second life before real callback invalidation"),
                 AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget::FromActor(Other),
                                                         OtherToken, Error));
        auto Payload = Input(F);
        Payload.TargetSet = Set(F.SourceToken, {F.TargetToken, OtherToken});
        auto* Definition = Graph(F.World, TEXT("Effect.DamageBatch"), Damage(F.World));
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        auto* ASC = F.Target->GetAbilitySystemComponent();
        const auto Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                if (bCancel)
                {
                    Run->Cancel();
                }
                else
                {
                    Other->EnableDungeonPooling();
                }
            });
        Run->StartWithInput(Definition, Payload, {}, Nodes, Error);
        TestEqual(TEXT("First real damage action occurs before invalidation callback"),
                  ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        TestEqual(TEXT("Callback cannot authorize damage to the remaining replaced/cancelled life"),
                  Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
        TestEqual(TEXT("Cancellation is distinct from normal batch completion"), Run->GetEndReason(),
                  bCancel ? EAbilityGraphEndReason::Cancelled : EAbilityGraphEndReason::Completed);
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCombatConfigurationTest,
                                 "test_25d.UnifiedGA.DynamicMage.BatchCombat.InvalidConfigurations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 不把错误枚举、负伤害或超量集合修正为另一份行为，必须在图启动前拒绝。 */
bool FBatchCombatConfigurationTest::RunTest(const FString&)
{
    FString Error;
    auto* Query = NewObject<UAbilityQueryBatchNodeConfig>();
    Query->LifePolicy = static_cast<EAbilityQueryLifePolicy>(255);
    TestFalse(TEXT("Batch query rejects unknown life policy"), Query->Validate(Error));
    Query->LifePolicy = EAbilityQueryLifePolicy::FreshQuery;
    Query->Query.Profile = static_cast<EAbilityShapeQueryProfile>(255);
    TestFalse(TEXT("Batch query rejects unknown shape precision profile"), Query->Validate(Error));
    Query->Query.Profile = EAbilityShapeQueryProfile::Generic;
    Query->Query.MaxTargets = 0;
    TestFalse(TEXT("Batch query requires a positive actual target cap"), Query->Validate(Error));
    Query->Query.MaxTargets = 65537;
    TestFalse(TEXT("Batch query rejects cap beyond validated maximum"), Query->Validate(Error));
    auto* Damage = NewObject<UAbilityDamageBatchNodeConfig>();
    Damage->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Damage->Profile = static_cast<EAbilityDamageActionProfile>(255);
    TestFalse(TEXT("Batch damage rejects unknown result profile"), Damage->Validate(Error));
    Damage->Profile = EAbilityDamageActionProfile::Unified;
    Damage->Damage = -1.f;
    TestFalse(TEXT("Batch damage retains inherited nonnegative damage contract"), Damage->Validate(Error));
    return true;
}

#endif
