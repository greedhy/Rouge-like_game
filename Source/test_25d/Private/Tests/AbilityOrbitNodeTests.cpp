// 测试链：独立 Schema2 图（没有嵌套GA激活） -> Orbit真实策略/ASC事件/世界定时器 -> 资源和生命边界断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    struct FOrbitNodeFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityOrbitContext* Context = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        FAbilityNodeInput Input;
        FOrbitNodeFixture()
        {
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
            Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
            Graph->Nodes.Reset();
            Graph->Edges.Reset();
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = TEXT("Channel.OrbitingFireBolts");
            auto* Config = NewObject<UAbilityOrbitNodeConfig>(Graph);
            Config->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(Config);
            Config->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
            Config->OrbitSettings->SpawnInterval = .1f;
            Config->OrbitSettings->LaunchInterval = .1f;
            Config->OrbitSettings->MaxFireBolts = 2;
            Config->OrbitSettings->ManaPerSecond = 2.f;
            Node.Config = Config;
            Graph->Nodes.Add(Node);
            Graph->EntryNodeId = Node.NodeId;
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 3.f);
            Context = NewObject<UAbilityOrbitContext>(World);
            Input.Context = Context;
            Input.SourceToken = SourceToken;
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
            Input.DamageParams.BaseDamage = 10.f;
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Fire"));
            Run = NewObject<UAbilityGraphInstance>(World);
        }
        ~FOrbitNodeFixture()
        {
            Run->Cancel();
        }
        bool Start()
        {
            FString Error;
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
            return Run->StartWithInput(Graph, Input, Options, Error);
        }
        void Step(float Seconds)
        {
            ++GFrameCounter;
            World->GetTimerManager().Tick(Seconds);
            Run->Advance(0.f);
        }
        void Attack()
        {
            CastChecked<UMyAbilitySystemComponent>(ASC)->OffensiveAttackTargeted.Broadcast(
                NewObject<UMyDamageGameplayAbility>(World), Target);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitNodeUpkeep, "test_25d.UnifiedGA.Orbit.StandaloneStrategyUpkeepAndCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 仅图节点即可补充、扣费并拥有Actor；费用不足通过同一Stop路径清理。 */
bool FOrbitNodeUpkeep::RunTest(const FString&)
{
    FOrbitNodeFixture F;
    TestTrue(TEXT("Standalone graph starts registered strategy"), F.Start());
    auto* Execution = F.Context->Execution.Get();
    if (!TestNotNull(TEXT("Per invocation execution"), Execution))
    {
        return false;
    }
    TestEqual(TEXT("No immediate refill"), Execution->GetLiveCount(), 0);
    F.Step(.01f);
    F.Step(.11f);
    TestEqual(TEXT("First interval creates a real bolt"), Execution->GetLiveCount(), 1);
    F.Step(.11f);
    TestEqual(TEXT("Second interval reaches capacity"), Execution->GetLiveCount(), 2);
    F.Step(.8f);
    TestEqual(TEXT("Periodic GAS mana cost applied"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
              1.f);
    F.Step(1.f);
    TestTrue(TEXT("Insufficient mana closes graph"), F.Run->GetEndReason() == EAbilityGraphEndReason::Cancelled);
    TestEqual(TEXT("Owned bolts destroyed"), Execution->GetLiveCount(), 0);
    TestFalse(TEXT("Diagnostic weak execution cleared"), F.Context->Execution.IsValid());
    F.Attack();
    F.Step(1.f);
    TestEqual(TEXT("Old subscription cannot replenish"), Execution->GetLiveCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitNodeTargetLife, "test_25d.UnifiedGA.Orbit.NewAttackRequiredAfterTargetReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 异步发射不得追踪同Actor下一生命；新的攻击选择事件可以重新捕获它。 */
bool FOrbitNodeTargetLife::RunTest(const FString&)
{
    FOrbitNodeFixture F;
    TestTrue(TEXT("Starts"), F.Start());
    auto* Execution = F.Context->Execution.Get();
    if (!Execution)
    {
        return false;
    }
    F.Step(.01f);
    F.Step(.21f);
    F.Attack();
    F.Step(.01f);
    F.Step(.01f);
    TestTrue(TEXT("Original target acquired"), Execution->GetAttackTarget() == F.Target);
    TestEqual(TEXT("First bolt launched"), Execution->GetLaunchedCount(), 1);
    F.Target->EnableDungeonPooling();
    F.Step(.11f);
    TestNull(TEXT("Old target lease cleared"), Execution->GetAttackTarget());
    TestEqual(TEXT("All old flights recalled"), Execution->GetLaunchedCount(), 0);
    F.Attack();
    F.Step(.01f);
    F.Step(.01f);
    TestTrue(TEXT("New explicit attack reacquires new life"), Execution->GetAttackTarget() == F.Target);
    TestEqual(TEXT("New life can receive a fresh launch"), Execution->GetLaunchedCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitNodeSourceLife, "test_25d.UnifiedGA.Orbit.SourceRetirementClosesTimers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 补充定时器自身校验来源，早于图Tick运行也不能替新生命生成Actor或扣费。 */
bool FOrbitNodeSourceLife::RunTest(const FString&)
{
    FOrbitNodeFixture F;
    TestTrue(TEXT("Starts"), F.Start());
    auto* Execution = F.Context->Execution.Get();
    F.Step(.01f);
    F.Step(.11f);
    F.Source->EnableDungeonPooling();
    F.Step(1.1f);
    TestTrue(TEXT("Source invalidation terminates graph"),
             F.Run->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    TestEqual(TEXT("No old upkeep charged"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 3.f);
    TestEqual(TEXT("No old bolts survive"), Execution ? Execution->GetLiveCount() : -1, 0);
    return true;
}
#endif
