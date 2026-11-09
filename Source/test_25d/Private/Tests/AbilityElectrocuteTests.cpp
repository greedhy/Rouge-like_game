// 测试链：真实 World/ASC -> 注册 Channel.Electrocute -> 世界定时器 -> GE上下文、费用顺序、清理及池生命隔离。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    struct FElectrocuteFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityElectrocuteContext* Context = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        FAbilityNodeInput Input;

        FElectrocuteFixture()
        {
            Graph->SchemaVersion = 2;
            Graph->CompatibilityProfile = TEXT("UnifiedV2");
            Graph->Nodes.Reset();
            Graph->Edges.Reset();
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = TEXT("Channel.Electrocute");
            auto* Config = NewObject<UAbilityElectrocuteNodeConfig>(Graph);
            Config->bTraceFromWeapon = false;
            Config->MaxAdditionalTargets = 0;
            Node.Config = Config;
            Graph->Nodes.Add(Node);
            Graph->EntryNodeId = Node.NodeId;
            Context = NewObject<UAbilityElectrocuteContext>(World);
            Context->AimActor = Target;
            Input.Context = Context;
            Input.SourceToken = SourceToken;
            Input.LiveTargetToken = TargetToken;
            Input.Origin = Target->GetActorLocation();
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
            Input.DamageParams.BaseDamage = 13.f;
            Input.DamageParams.AbilityLevel = 1.f;
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder"));
            Run = NewObject<UAbilityGraphInstance>(World);
        }

        ~FElectrocuteFixture()
        {
            Run->Cancel();
        }

        bool Start()
        {
            FString Error;
            return Run->StartWithInput(Graph, Input, {}, Error);
        }

        void Step(float Seconds)
        {
            ++GFrameCounter;
            World->GetTimerManager().Tick(Seconds);
            Run->Advance(0.f);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteCadence, "test_25d.UnifiedGA.Electrocute.CadenceCostAndFinalDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 使用真实GE申请观察上下文，不把事件计数冒充伤害扣血；周期来源与最终来源刻意保持旧蓝图差异。 */
bool FElectrocuteCadence::RunTest(const FString&)
{
    FElectrocuteFixture F;
    int32 Costs = 0;
    int32 Cooldowns = 0;
    int32 Effects = 0;
    int32 TargetAuthored = 0;
    int32 SourceAuthored = 0;
    F.Context->CommitPeriodCost = [&]()
    {
        return ++Costs <= 2;
    };
    F.Context->CommitEndCooldown = [&]()
    {
        ++Cooldowns;
    };
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    const auto Applied = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            ++Effects;
            TestEqual(TEXT("Damage magnitude preserved"),
                      Spec.GetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder")), false),
                      13.f);
            auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
            TargetAuthored += Instigator == TargetASC ? 1 : 0;
            SourceAuthored += Instigator == F.ASC ? 1 : 0;
            TestTrue(TEXT("Cost is attempted before each damage phase"), Costs >= Effects);
        });
    TestTrue(TEXT("Registered strategy starts"), F.Start());
    TestTrue(TEXT("Primary enters shock loop"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    TestEqual(TEXT("Source movement disabled"), int32(F.Source->GetCharacterMovement()->MovementMode),
              int32(MOVE_None));
    F.Step(.01f);
    TestEqual(TEXT("No synchronous cost"), Costs, 0);
    F.Step(.11f);
    TestEqual(TEXT("First period"), Effects, 1);
    F.Step(.9f);
    TestEqual(TEXT("Hitch executes at most one period"), Effects, 2);
    F.Step(.11f);
    TestEqual(TEXT("Failed third cost still ends with the legacy final hit"), Effects, 3);
    TestEqual(TEXT("Periodic GE remains target authored"), TargetAuthored, 2);
    TestEqual(TEXT("Final GE comes from source"), SourceAuthored, 1);
    TestEqual(TEXT("Cooldown committed once"), Cooldowns, 1);
    TestTrue(TEXT("Graph completes normally"), F.Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    TestFalse(TEXT("Shock cleared"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    TestEqual(TEXT("Walking restored"), int32(F.Source->GetCharacterMovement()->MovementMode), int32(MOVE_Walking));
    F.Context->RequestCompletion();
    F.Step(2.f);
    TestEqual(TEXT("No late costs"), Costs, 3);
    TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Applied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteCostCancellation,
                                 "test_25d.UnifiedGA.Electrocute.ReentrantCostCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 扣费观察者可同步关闭图；同一计时回调随后不得伤害或伪造最终命中/冷却。 */
bool FElectrocuteCostCancellation::RunTest(const FString&)
{
    FElectrocuteFixture F;
    int32 Effects = 0;
    int32 Cooldowns = 0;
    F.Context->CommitPeriodCost = [&]()
    {
        F.Run->Cancel();
        return true;
    };
    F.Context->CommitEndCooldown = [&]()
    {
        ++Cooldowns;
    };
    auto* ASC = F.Target->GetAbilitySystemComponent();
    const auto Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
        {
            ++Effects;
        });
    TestTrue(TEXT("Starts"), F.Start());
    F.Step(.01f);
    F.Step(.11f);
    F.Step(2.f);
    TestEqual(TEXT("Cancelled cost callback produces no damage"), Effects, 0);
    TestEqual(TEXT("Cancellation does not masquerade as normal finalization"), Cooldowns, 0);
    TestFalse(TEXT("Shock cleaned"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    TestTrue(TEXT("Cancellation retained"), F.Run->GetEndReason() == EAbilityGraphEndReason::Cancelled);
    ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteSourceGuard, "test_25d.UnifiedGA.Electrocute.SourceIdentityBeforeTimer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 定时器先于图 Tick 时仍要核对原 ASC Avatar；不能在旧来源上再扣费。 */
bool FElectrocuteSourceGuard::RunTest(const FString&)
{
    FElectrocuteFixture F;
    int32 Costs = 0;
    F.Context->CommitPeriodCost = [&]()
    {
        ++Costs;
        return true;
    };
    TestTrue(TEXT("Starts"), F.Start());
    F.Step(.01f);
    F.ASC->ClearActorInfo();
    F.Step(.2f);
    TestEqual(TEXT("No cost after source retirement"), Costs, 0);
    TestTrue(TEXT("SourceInvalid retained"), F.Run->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    TestFalse(TEXT("Original target shock released"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteLeaseCleanup,
                                 "test_25d.UnifiedGA.Electrocute.CleanupPreservesNewTargetLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同一 UObject 进入新池租约后，旧图不能解除新生命的状态。 */
bool FElectrocuteLeaseCleanup::RunTest(const FString&)
{
    FElectrocuteFixture F;
    TestTrue(TEXT("Starts"), F.Start());
    F.Target->EnableDungeonPooling();
    TestTrue(TEXT("Target now has a different positive lease"), F.Target->GetDungeonPoolLease() > 0);
    ICombatInterface::Execute_SetIsBeingShocked(F.Target, true);
    F.Run->Cancel();
    TestTrue(TEXT("New target life keeps its own shock state"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    return true;
}
#endif
