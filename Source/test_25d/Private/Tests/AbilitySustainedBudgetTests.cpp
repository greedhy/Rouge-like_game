// 调用链：真实引导/费用 GE -> 同一图重复后继 -> 超过256事件 -> 活动/速率/租约预算与精确清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace AbilitySustainedBudgetTests
{
    /** 使用原引导和瞬时 Mana GE，继承的伤害/最终命中规则不由测试替代。 */
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityElectrocuteContext* Binding = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        UAbilityGraphDefinition* Definition = nullptr;
        FAbilityNodeInput Input;
        int32 PaidPeriods = 0;

        FFixture()
        {
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            auto* TargetASC = Target->GetAbilitySystemComponent();
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
            TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
            Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("UnifiedV2");
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::RegisteredSustained;
            FAbilityGraphNode Channel;
            Channel.NodeId = FGuid::NewGuid();
            Channel.TypeId = TEXT("Channel.Electrocute");
            auto* Config = NewObject<UAbilityElectrocuteNodeConfig>(Definition);
            Config->DamageInterval = .01f;
            Config->MaxAdditionalTargets = 0;
            Config->bTraceFromWeapon = false;
            Channel.Config = Config;
            FAbilityGraphNode Child;
            Child.NodeId = FGuid::NewGuid();
            Child.TypeId = TEXT("Control.DelayOrigin");
            auto* Delay = NewObject<UAbilityDelayNodeConfig>(Definition);
            Delay->DelaySeconds = .001f;
            Child.Config = Delay;
            Definition->EntryNodeId = Channel.NodeId;
            Definition->Nodes = {Channel, Child};
            Definition->Edges = {{Channel.NodeId, Child.NodeId, TEXT("DamageResolved")}};
            Binding = NewObject<UAbilityElectrocuteContext>(World);
            Binding->AimActor = Target;
            Binding->CommitPeriodCost = [this]()
            {
                if (ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < 10.f)
                {
                    return false;
                }
                const auto Spec =
                    ASC->MakeOutgoingSpec(UAbilityGraphTestCost::StaticClass(), 1.f, ASC->MakeEffectContext());
                const bool bPaid = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied();
                PaidPeriods += bPaid ? 1 : 0;
                return bPaid;
            };
            Input.SourceToken = SourceToken;
            Input.LiveTargetToken = TargetToken;
            Input.Origin = Target->GetActorLocation();
            Input.Context = Binding;
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            Input.DamageParams.BaseDamage = 10.f;
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder"));
            Run = NewObject<UAbilityGraphInstance>(World);
        }
        ~FFixture()
        {
            Run->Cancel();
        }
        bool Start(FString& Error)
        {
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }
        void Step()
        {
            // 原定时器严格比较到期时间并启用每帧最多一次；用真实90FPS量级步长跨过.01秒边界。
            constexpr float FrameDelta = .011f;
            ++GFrameCounter;
            World->GetTimerManager().Tick(FrameDelta);
            Run->Advance(FrameDelta);
        }
        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };
} // namespace AbilitySustainedBudgetTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSustainedLongChannel,
                                 "test_25d.UnifiedGA.DynamicMage.Sustained.RealChannelBeyond256Events",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSustainedLongChannel::RunTest(const FString&)
{
    AbilitySustainedBudgetTests::FFixture F;
    FString Error;
    if (!TestTrue(TEXT("Registered sustained source starts"), F.Start(Error)))
    {
        AddInfo(Error);
        return false;
    }
    for (int32 Index = 0; Index < 350 && !F.Run->IsFinished(); ++Index)
    {
        F.Step();
        TestTrue(TEXT("Completed child metadata does not grow active resource count"),
                 F.Run->GetActiveInvocationCount() <= 3);
    }
    AddInfo(FString::Printf(
        TEXT("Long channel diagnostic: paid=%d accepted=%llu active=%d finished=%d health=%.3f mana=%.3f"),
        F.PaidPeriods, F.Run->GetAcceptedEventCount(), F.Run->GetActiveInvocationCount(), F.Run->IsFinished(),
        F.Health(), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute())));
    TestTrue(TEXT("Real paid periods continue beyond cumulative finite event limit"), F.PaidPeriods > 300);
    TestFalse(TEXT("Indefinite channel is not truncated by cumulative invocations/events"), F.Run->IsFinished());
    TestTrue(TEXT("One original execution accepts more than256 events"), F.Run->GetAcceptedEventCount() > 256);
    TestEqual(TEXT("Every real periodic GE changes health once"), F.Health(), 10000.f - 10.f * F.PaidPeriods);
    TestEqual(TEXT("Actual costs match successful periods"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 10000.f - 10.f * F.PaidPeriods);
    F.Binding->RequestCompletion();
    F.Run->Advance(.02f);
    TestTrue(TEXT("Original release and remaining child finish the graph"), F.Run->IsFinished());
    TestEqual(TEXT("Original final damage occurs once after release"), F.Health(),
              10000.f - 10.f * (F.PaidPeriods + 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSustainedAdmission,
                                 "test_25d.UnifiedGA.DynamicMage.Sustained.RequiresRegisteredAdmission",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSustainedAdmission::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::RegisteredSustained;
    FAbilityGraphNode Delay;
    Delay.NodeId = FGuid::NewGuid();
    Delay.TypeId = TEXT("Control.DelayOrigin");
    Delay.Config = NewObject<UAbilityDelayNodeConfig>(Graph);
    Graph->EntryNodeId = Delay.NodeId;
    Graph->Nodes.Add(Delay);
    TArray<FString> Errors;
    TestFalse(TEXT("A policy enum alone cannot turn ordinary delay into a sustained root"),
              Graph->ValidateForAuthoring(Errors));
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
    TestTrue(TEXT("Original finite delay contract remains valid"), Graph->ValidateForAuthoring(Errors));
    AbilitySustainedBudgetTests::FFixture Channel;
    auto* Changed = DuplicateObject<UAbilityGraphDefinition>(Channel.Definition, F.World);
    Changed->SustainedBudget.PhysicalStartsPerSecond = 32;
    TestNotEqual(TEXT("Sustained resource contract participates in semantic recipe identity"),
                 AbilityRuntimeRecipes::Fingerprint(*Channel.Definition), AbilityRuntimeRecipes::Fingerprint(*Changed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSustainedPhysicalLeases,
                                 "test_25d.UnifiedGA.DynamicMage.Sustained.PhysicalLeaseAndRateLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSustainedPhysicalLeases::RunTest(const FString&)
{
    AbilitySustainedBudgetTests::FFixture F;
    FString Error;
    if (!TestTrue(TEXT("Actual registered source starts"), F.Start(Error)))
    {
        return false;
    }
    for (int32 Index = 0; Index < 100; ++Index)
    {
        FGuid Lease;
        if (!TestTrue(TEXT("One root admits later physical work after owned release"),
                      F.Run->ReservePhysicalSpawnWithLease(Lease)))
        {
            break;
        }
        TestTrue(TEXT("Only the exact physical lease releases concurrency"), F.Run->ReleasePhysicalSpawnLease(Lease));
        TestFalse(TEXT("Repeated release cannot refund another live resource"),
                  F.Run->ReleasePhysicalSpawnLease(Lease));
        F.Run->Advance(.02f);
    }
    TestEqual(TEXT("More than64 cumulative starts remain within sustained concurrency/rate"),
              F.Run->GetPhysicalSpawnCountTotal(), uint64(100));
    TArray<FGuid> Concurrent;
    for (int32 Index = 0; Index < 32; ++Index)
    {
        FGuid Lease;
        if (F.Run->ReservePhysicalSpawnWithLease(Lease))
        {
            Concurrent.Add(Lease);
        }
    }
    TestEqual(TEXT("Declared active physical capacity is available"), Concurrent.Num(), 32);
    FGuid Overflow;
    TestFalse(TEXT("Thirty-third concurrent resource is refused"), F.Run->ReservePhysicalSpawnWithLease(Overflow));
    for (const auto Lease : Concurrent)
    {
        F.Run->ReleasePhysicalSpawnLease(Lease);
    }
    TestEqual(TEXT("Every resource returns its exact concurrency slot"), F.Run->GetActivePhysicalSpawnCount(), 0);
    F.Run->Cancel();
    TestFalse(TEXT("Terminated root cannot start more work"), F.Run->ReservePhysicalSpawnWithLease(Overflow));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExternalGraphDrain,
                                 "test_25d.UnifiedGA.DynamicMage.Scheduling.ExternalBoundaryUsesSameDrainer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 原引导计时器先实际结算，再于外部边界处理图后继；排空不能重入观察者或推进第二次时间。 */
bool FExternalGraphDrain::RunTest(const FString&)
{
    AbilitySustainedBudgetTests::FFixture F;
    F.Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
    auto* Cost = NewObject<UAbilityGameplayEffectNodeConfig>(F.Definition);
    Cost->EffectClass = UAbilityGraphTestCost::StaticClass();
    F.Definition->Nodes[1].TypeId = TEXT("Effect.GameplayEffect");
    F.Definition->Nodes[1].Config = Cost;
    F.Definition->Edges[0].OutputPort = TEXT("PeriodElapsed");
    int32 RealPeriods = 0;
    F.Binding->CommitPeriodCost = [&RealPeriods]()
    {
        ++RealPeriods;
        return true;
    };
    F.Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("PeriodElapsed"))
            {
                F.Run->FlushPublishedEvents();
                TestEqual(TEXT("Reentrant request does not run descendants inside observer"),
                          F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 10000.f);
            }
        });
    FString Error;
    TestTrue(TEXT("Real timer host starts"), F.Start(Error));
    // 临时世界首次 Tick 才把 PendingTimerSet 转入活动堆；先以零步长完成这个引擎初始化阶段。
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(0.f);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(.011f);
    TestEqual(TEXT("Original period executes once"), RealPeriods, 1);
    TestEqual(TEXT("Original damage precedes external event dispatch"), F.Health(), 9990.f);
    TestTrue(TEXT("External boundary drains current work without another Advance"), F.Run->FlushPublishedEvents(true));
    TestEqual(TEXT("Actual child GE is applied during same boundary"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 9990.f);
    TestEqual(TEXT("Drain cannot repeat timer or period cost"), RealPeriods, 1);
    F.Binding->RequestCompletion();
    TestTrue(TEXT("External release completion can drain without clock advancement"),
             F.Run->FlushPublishedEvents(true));
    TestTrue(TEXT("Root finishes normally"), F.Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedFrameGraphDrain,
                                 "test_25d.UnifiedGA.DynamicMage.Scheduling.RepeatedFlushSharesFrameLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 多次外部回调不能通过重复调用排空函数重置每帧额度；下一真实帧才继续剩余工作。 */
bool FSharedFrameGraphDrain::RunTest(const FString&)
{
    AbilitySustainedBudgetTests::FFixture F;
    F.Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;
    F.Definition->Budget.MaxEventsPerTick = 2;
    auto* Cost = NewObject<UAbilityGameplayEffectNodeConfig>(F.Definition);
    Cost->EffectClass = UAbilityGraphTestCost::StaticClass();
    F.Definition->Nodes[1].TypeId = TEXT("Effect.GameplayEffect");
    F.Definition->Nodes[1].Config = Cost;
    F.Definition->Edges[0].OutputPort = TEXT("PeriodElapsed");
    F.Binding->CommitPeriodCost = []()
    {
        return true;
    };
    int32 Observed = 0;
    F.Run->OnEvent.AddLambda(
        [&Observed](const FAbilityGraphEvent&)
        {
            ++Observed;
        });
    FString Error;
    TestTrue(TEXT("Bounded source starts"), F.Start(Error));
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(0.f);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(.011f);
    TestFalse(TEXT("First drain reports work still pending beyond two events"), F.Run->FlushPublishedEvents());
    TestEqual(TEXT("First boundary consumes exactly declared frame budget"), Observed, 2);
    TestFalse(TEXT("Repeated drain cannot consume another budget in same frame"), F.Run->FlushPublishedEvents());
    TestEqual(TEXT("Observers have no same-frame bypass"), Observed, 2);
    ++GFrameCounter;
    TestTrue(TEXT("Next actual frame drains remaining child completion"), F.Run->FlushPublishedEvents());
    TestEqual(TEXT("Each queued event observed once"), Observed, 3);
    TestEqual(TEXT("Each real GE applied once across partial drains"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 9990.f);
    return true;
}
#endif
