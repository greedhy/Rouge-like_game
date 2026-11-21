// 测试链：真实 World/ASC -> 注册 Channel.Electrocute -> 世界定时器 -> GE上下文、费用顺序、清理及池生命隔离。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/CapsuleComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "MyGameplayTags.h"

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
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Graph->BudgetPolicy;
            return Run->StartWithInput(Graph, Input, Options, Error);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteEquipmentChain, "test_25d.Equipment.Unique.ElectrocuteChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 用真实戒指 GE 改写节点的等级预算、索敌半径和每周期的 SetByCaller，同时确认取消会解除雷链。
bool FElectrocuteEquipmentChain::RunTest(const FString&)
{
    FElectrocuteFixture F;
    const auto Ring = LoadClass<UUniqueEquipmentGameplayEffect>(
        nullptr, TEXT("/Game/Equipment/Unique/Effects/GE_UniqueForkstormRing.GE_UniqueForkstormRing_C"));
    if (!TestNotNull(TEXT("Authored chain ring"), Ring))
    {
        return false;
    }
    auto* Config = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
    Config->EquipmentAbilityTag = FMyGameplayTags::Get().Abilities_Thunder_Electrocute;
    Config->SecondaryRadius = 300.f;
    auto* Secondary = F.World->SpawnActor<AEnemyCharacter>(FVector(485.f, 0.f, 0.f), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Additional enemy"), Secondary))
    {
        return false;
    }
    Secondary->Tags.AddUnique(TEXT("Enemy"));
    Secondary->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Secondary->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    auto* TargetASC = Secondary->GetAbilitySystemComponent();
    TargetASC->InitAbilityActorInfo(Secondary, Secondary);
    TargetASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Secondary));
    const auto Applied =
        F.ASC->ApplyGameplayEffectToSelf(GetDefault<UGameplayEffect>(Ring), 1.f, F.ASC->MakeEffectContext());
    TestTrue(TEXT("Chain ring active"), Applied.IsValid());
    int32 Periods = 0;
    int32 Costs = 0;
    F.Context->CommitPeriodCost = [&Costs]()
    {
        ++Costs;
        return true;
    };
    const auto Delegate = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        {
            ++Periods;
            TestEqual(TEXT("Chained target receives reduced period damage"),
                      Spec.GetSetByCallerMagnitude(FMyGameplayTags::Get().Damage_Thunder, false), 13.f * .72f);
        });
    TestTrue(TEXT("Real shock execution starts"), F.Start());
    TestTrue(TEXT("Level budget zero now reaches a target at distance 335"),
             ICombatInterface::Execute_IsBeingShocked(Secondary));
    F.ASC->RemoveActiveGameplayEffect(Applied);
    F.Step(.01f);
    F.Step(.11f);
    TestEqual(TEXT("Ongoing channel keeps its original equipment snapshot"), Periods, 1);
    TestEqual(TEXT("Channel payment stays once per period"), Costs, 1);
    F.Run->Cancel();
    TestFalse(TEXT("Cancel releases the added target"), ICombatInterface::Execute_IsBeingShocked(Secondary));
    F.Step(1.f);
    TestEqual(TEXT("No extra damage after cancel"), Periods, 1);
    TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteSharedRelease,
                                 "test_25d.UnifiedGA.DynamicMage.NativeHost.SharedReleaseStopsEveryOwnedHost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同一施法上下文的两个实际宿主都必须响应松键；不能只保留最后一次写入的弱指针。 */
bool FElectrocuteSharedRelease::RunTest(const FString&)
{
    FElectrocuteFixture F;
    auto* Second = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("First real channel starts"), F.Start());
    TestTrue(TEXT("Second real channel shares release context"), Second->StartWithInput(F.Graph, F.Input, {}, Error));
    int32 Cooldowns = 0;
    F.Context->CommitEndCooldown = [&Cooldowns]()
    {
        ++Cooldowns;
    };
    F.Context->RequestCompletion();
    F.Run->Advance(0.f);
    Second->Advance(0.f);
    TestTrue(TEXT("Release ends first host"), F.Run->IsFinished());
    TestTrue(TEXT("Release ends second host"), Second->IsFinished());
    TestEqual(TEXT("Each actual host completes its own final stage once"), Cooldowns, 2);
    F.Context->RequestCompletion();
    TestEqual(TEXT("Repeated release has no completed-host callback"), Cooldowns, 2);
    Second->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteSharedPresentation,
                                 "test_25d.UnifiedGA.DynamicMage.NativeHost.IndependentCleanupKeepsOtherHost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 关闭较早的宿主不能清空较新宿主的释放入口、目标状态或角色移动占用。 */
bool FElectrocuteSharedPresentation::RunTest(const FString&)
{
    FElectrocuteFixture F;
    auto* Second = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("First real channel starts"), F.Start());
    TestTrue(TEXT("Second real channel starts"), Second->StartWithInput(F.Graph, F.Input, {}, Error));
    F.Run->Cancel();
    TestTrue(TEXT("Other host is still running"), !Second->IsFinished());
    TestTrue(TEXT("Remaining host retains release route"), F.Context->Execution.IsValid());
    TestTrue(TEXT("Remaining host retains target shock"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    TestEqual(TEXT("Remaining host retains source movement lock"),
              int32(F.Source->GetCharacterMovement()->MovementMode), int32(MOVE_None));
    F.Context->RequestCompletion();
    Second->Advance(0.f);
    TestTrue(TEXT("Remaining host can close normally"), Second->IsFinished());
    TestFalse(TEXT("Last host clears target shock"), ICombatInterface::Execute_IsBeingShocked(F.Target));
    TestEqual(TEXT("Last host returns walking"), int32(F.Source->GetCharacterMovement()->MovementMode),
              int32(MOVE_Walking));
    Second->Cancel();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteFineDefault,
                                 "test_25d.DynamicMage.NativeElectrocute.FineDefaultPreservesActualEffectsAndCostOrder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteFineDefault::RunTest(const FString&)
{
    for (const bool bFine : {false, true})
    {
        FElectrocuteFixture F;
        if (bFine)
        {
            F.Graph = AbilityElectrocuteFragments::Build(
                F.World, *CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config));
            if (!TestNotNull(TEXT("Fine native channel definition exists"), F.Graph))
            {
                continue;
            }
            TestFalse(TEXT("Default contains no whole original channel macro"),
                      F.Graph->Nodes.ContainsByPredicate(
                          [](const auto& Node)
                          {
                              return Node.TypeId == TEXT("Channel.Electrocute");
                          }));
        }
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        int32 Effects = 0;
        int32 TargetAuthored = 0;
        int32 SourceAuthored = 0;
        int32 Costs = 0;
        int32 Cooldowns = 0;
        F.Context->CommitPeriodCost = [&]()
        {
            return ++Costs <= 2;
        };
        F.Context->CommitEndCooldown = [&]()
        {
            ++Cooldowns;
            TestEqual(TEXT("Actual final GE precedes cooldown even on failed period payment"), Effects, 3);
        };
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++Effects;
                const auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                TargetAuthored += Instigator == TargetASC ? 1 : 0;
                SourceAuthored += Instigator == F.ASC ? 1 : 0;
                TestTrue(TEXT("Every real effect follows its original payment phase"), Costs >= Effects);
            });
        TestTrue(TEXT("Actual native channel starts"), F.Start());
        F.Step(.01f);
        F.Step(.11f);
        F.Step(.9f);
        TestEqual(TEXT("Hitch preserves exactly two real periodic effects"), Effects, 2);
        TestEqual(TEXT("Real periodic GEs decrease Health twice"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 80.f);
        F.Step(.11f);
        TestEqual(TEXT("One real final effect follows failed third payment"), Effects, 3);
        TestEqual(TEXT("Both periods retain target-authored context"), TargetAuthored, 2);
        TestEqual(TEXT("Final effect retains source-authored context"), SourceAuthored, 1);
        TestEqual(TEXT("All actual effects decrease Health once"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 70.f);
        TestEqual(TEXT("Cooldown attempted only once"), Cooldowns, 1);
        TestEqual(TEXT("Channel completes normally"), F.Run->GetEndReason(), EAbilityGraphEndReason::Completed);
        TestFalse(TEXT("Last host clears shock"), ICombatInterface::Execute_IsBeingShocked(F.Target));
        TestEqual(TEXT("Last host restores walking"), int32(F.Source->GetCharacterMovement()->MovementMode),
                  int32(MOVE_Walking));
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FElectrocuteFineReplacePeriod,
    "test_25d.DynamicMage.NativeElectrocute.PeriodPayloadCanBeRemovedWithoutRemovingUpkeepOrFinal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteFineReplacePeriod::RunTest(const FString&)
{
    FElectrocuteFixture F;
    F.Graph = AbilityElectrocuteFragments::Build(F.World,
                                                 *CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config));
    if (!TestNotNull(TEXT("Fine native channel definition exists"), F.Graph))
    {
        return false;
    }
    const auto* Period = F.Graph->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Effect.ElectrocutePeriod");
        });
    if (!TestNotNull(TEXT("Periodic damage has its own replaceable graph action"), Period))
    {
        return false;
    }
    const FGuid Removed = Period->NodeId;
    F.Graph->Edges.RemoveAll(
        [Removed](const auto& Edge)
        {
            return Edge.From == Removed || Edge.To == Removed;
        });
    F.Graph->Nodes.RemoveAll(
        [Removed](const auto& Node)
        {
            return Node.NodeId == Removed;
        });
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
    int32 Costs = 0;
    int32 Cooldowns = 0;
    F.Context->CommitPeriodCost = [&]()
    {
        ++Costs;
        return true;
    };
    F.Context->CommitEndCooldown = [&]()
    {
        ++Cooldowns;
    };
    TestTrue(TEXT("Edited native channel starts through its original context"), F.Start());
    F.Step(.01f);
    F.Step(.11f);
    F.Step(.11f);
    TestEqual(TEXT("Original upkeep remains independent of periodic payload"), Costs, 2);
    TestEqual(TEXT("Removed payload causes no hidden damage"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    F.Context->RequestCompletion();
    F.Run->FlushPublishedEvents(true);
    TestEqual(TEXT("Independent final payload still applies one actual GE"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestEqual(TEXT("Independent cooldown stays once"), Cooldowns, 1);
    F.Step(1.f);
    TestEqual(TEXT("Completed host has no late upkeep"), Costs, 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FElectrocuteFinePrimaryDeath,
    "test_25d.DynamicMage.NativeElectrocute.SynchronousPrimaryDeathFinishesOtherTargetsBeforeCooldown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteFinePrimaryDeath::RunTest(const FString&)
{
    for (const bool bFine : {false, true})
    {
        FElectrocuteFixture F;
        auto* Settings = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
        Settings->MaxAdditionalTargets = 1;
        if (bFine)
        {
            F.Graph = AbilityElectrocuteFragments::Build(F.World, *Settings);
            if (!TestNotNull(TEXT("Fine native channel definition exists"), F.Graph))
            {
                continue;
            }
        }
        auto* Other = F.World->SpawnActor<AEnemyCharacter>(FVector(200, 0, 0), FRotator::ZeroRotator);
        Other->Tags = {TEXT("Enemy")};
        Other->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Other->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        auto* OtherASC = Other->GetAbilitySystemComponent();
        OtherASC->InitAbilityActorInfo(Other, Other);
        OtherASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Other));
        OtherASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        OtherASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        auto* PrimaryASC = F.Target->GetAbilitySystemComponent();
        PrimaryASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        PrimaryASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        int32 PrimaryEffects = 0;
        int32 OtherEffects = 0;
        int32 OtherFinals = 0;
        int32 Cooldowns = 0;
        F.Context->CommitPeriodCost = []()
        {
            return true;
        };
        F.Context->CommitEndCooldown = [&]()
        {
            ++Cooldowns;
            TestEqual(TEXT("Secondary actual final GE completes before native cooldown"), OtherEffects, 1);
            TestEqual(TEXT("Secondary final retains source-authored context at that point"), OtherFinals, 1);
        };
        const auto PrimaryHook = PrimaryASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
            {
                if (++PrimaryEffects == 1)
                {
                    // 模拟同步伤害订阅者结束真实原生命，仍执行生产Die/死亡委托，而不是直接完成图。
                    F.Target->Die(FVector::ZeroVector);
                }
            });
        const auto OtherHook = OtherASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++OtherEffects;
                OtherFinals += Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent() == F.ASC ? 1 : 0;
            });
        TestTrue(TEXT("Channel starts with both original target lives"), F.Start());
        TestTrue(TEXT("Secondary life really participates before death"),
                 ICombatInterface::Execute_IsBeingShocked(Other));
        F.Step(.01f);
        F.Step(.11f);
        TestTrue(TEXT("Production death delegate ends the primary life"), ICombatInterface::Execute_IsDead(F.Target));
        TestEqual(TEXT("Primary receives only its one actual periodic GE"), PrimaryEffects, 1);
        TestEqual(TEXT("Secondary receives final only, never a later periodic GE"), OtherEffects, 1);
        TestEqual(TEXT("Secondary actual Health decreases once"),
                  OtherASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        TestEqual(TEXT("Death completion commits cooldown once"), Cooldowns, 1);
        TestEqual(TEXT("Death completion retains ordinary completion reason"), F.Run->GetEndReason(),
                  EAbilityGraphEndReason::Completed);
        F.Step(1.f);
        TestEqual(TEXT("No old period continues after synchronous death"), OtherEffects, 1);
        PrimaryASC->OnGameplayEffectAppliedDelegateToSelf.Remove(PrimaryHook);
        OtherASC->OnGameplayEffectAppliedDelegateToSelf.Remove(OtherHook);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteFineLongUpkeep,
                                 "test_25d.DynamicMage.NativeElectrocute.FineChannelSurvives325ActualPaidPeriods",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteFineLongUpkeep::RunTest(const FString&)
{
    for (const bool bFine : {false, true})
    {
        FElectrocuteFixture F;
        auto* Settings = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
        Settings->DamageInterval = .01f;
        if (bFine)
        {
            F.Graph = AbilityElectrocuteFragments::Build(F.World, *Settings);
            if (!TestNotNull(TEXT("Fine long-running channel definition exists"), F.Graph))
            {
                continue;
            }
            TestEqual(TEXT("Fine channel uses registered sustained admission"), F.Graph->BudgetPolicy,
                      EAbilityGraphBudgetPolicy::RegisteredSustained);
        }
        else
        {
            F.Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
            F.Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        }
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        int32 Paid = 0;
        int32 Periods = 0;
        int32 Finals = 0;
        int32 Cooldowns = 0;
        F.Context->CommitPeriodCost = [&]()
        {
            const auto Spec =
                F.ASC->MakeOutgoingSpec(UAbilityGraphTestCost::StaticClass(), 1.f, F.ASC->MakeEffectContext());
            const auto Handle = F.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            Paid += Handle.WasSuccessfullyApplied() ? 1 : 0;
            return Handle.WasSuccessfullyApplied();
        };
        F.Context->CommitEndCooldown = [&]()
        {
            ++Cooldowns;
            TestEqual(TEXT("Long-lived final effect precedes cooldown"), Finals, 1);
        };
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                Periods += Instigator == TargetASC ? 1 : 0;
                Finals += Instigator == F.ASC ? 1 : 0;
                TestEqual(TEXT("Each long-lived actual GE preserves its Thunder magnitude"),
                          Spec.GetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder")), false),
                          13.f);
            });
        TestTrue(TEXT("Long channel starts with the original source and payment interface"), F.Start());
        F.World->GetTimerManager().Tick(0.f); // 将真实待激活Timer登记，再计数325个跨越到期边界的帧。
        for (int32 Index = 0; Index < 325 && !F.Run->IsFinished(); ++Index)
        {
            ++GFrameCounter;
            F.World->GetTimerManager().Tick(.011f);
            F.Run->Advance(.011f); // 真正补充同一根速率令牌，而不是每次另起执行。
            TestTrue(TEXT("Completed payload invocations release active metadata"),
                     F.Run->GetActiveInvocationCount() <= 3);
        }
        TestFalse(TEXT("No accumulated 64-invocation or256-event ceiling ends this sustained channel"),
                  F.Run->IsFinished());
        TestEqual(TEXT("Every one of325 real periods pays once"), Paid, 325);
        TestEqual(TEXT("Every paid period applies one target-authored GE"), Periods, 325);
        TestEqual(TEXT("Real mana follows325 single payments"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 6750.f);
        TestEqual(TEXT("All325 actual periodic effects change Health"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 6750.f);
        F.Context->RequestCompletion();
        F.Run->FlushPublishedEvents(true);
        TestEqual(TEXT("One original final survives long continuation lifetime"), Finals, 1);
        TestEqual(TEXT("One cooldown follows long channel completion"), Cooldowns, 1);
        TestEqual(TEXT("Long channel completes normally"), F.Run->GetEndReason(), EAbilityGraphEndReason::Completed);
        TestEqual(TEXT("All original host/payload metadata releases"), F.Run->GetActiveInvocationCount(), 0);
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElectrocuteFineFullTargetBatch,
                                 "test_25d.DynamicMage.NativeElectrocute.Full65TargetBatchKeepsOriginalActualEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteFineFullTargetBatch::RunTest(const FString&)
{
    for (const bool bFine : {false, true})
    {
        FElectrocuteFixture F;
        auto* Settings = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
        Settings->MaxAdditionalTargets = 64;
        if (bFine)
        {
            F.Graph = AbilityElectrocuteFragments::Build(F.World, *Settings);
            if (!TestNotNull(TEXT("Fine full-target batch definition exists"), F.Graph))
            {
                continue;
            }
        }
        else
        {
            F.Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
            F.Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
        }
        TArray<UAbilitySystemComponent*> TargetASCs;
        TArray<FDelegateHandle> Hooks;
        int32 Costs = 0;
        int32 Periods = 0;
        int32 Finals = 0;
        int32 Cooldowns = 0;
        auto Configure = [&](AEnemyCharacter* Actor)
        {
            Actor->Tags = {TEXT("Enemy")};
            Actor->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Actor->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            auto* ASC = Actor->GetAbilitySystemComponent();
            ASC->InitAbilityActorInfo(Actor, Actor);
            if (!ASC->GetSet<UMyAttributeSet>())
            {
                ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            }
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            TargetASCs.Add(ASC);
            Hooks.Add(ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&, ASC](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    auto* Instigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                    Periods += Instigator == ASC ? 1 : 0;
                    Finals += Instigator == F.ASC ? 1 : 0;
                }));
        };
        Configure(F.Target);
        for (int32 Index = 0; Index < 64; ++Index)
        {
            Configure(F.World->SpawnActor<AEnemyCharacter>(FVector(200.f + Index * 4.f, 0, 0), FRotator::ZeroRotator));
        }
        F.Input.Level = 63.f;
        F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        F.Context->CommitPeriodCost = [&]()
        {
            ++Costs;
            return true;
        };
        F.Context->CommitEndCooldown = [&]()
        {
            ++Cooldowns;
            TestEqual(TEXT("All65 actual final GEs precede one cooldown"), Finals, 65);
        };
        TestTrue(TEXT("Full target set starts with original chain selection"), F.Start());
        F.Step(.01f);
        F.Step(.11f);
        TestEqual(TEXT("One original upkeep covers the whole65-target period"), Costs, 1);
        TestEqual(TEXT("All65 target-authored GEs really execute without hidden truncation"), Periods, 65);
        for (auto* ASC : TargetASCs)
        {
            TestEqual(TEXT("Each captured target loses exactly10 real Health"),
                      ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        }
        // 给旧宏尚未消费的批结果一个独立帧；不改变其真实伤害或挪动费用时点。
        ++GFrameCounter;
        F.Run->Advance(0.f);
        F.Context->RequestCompletion();
        F.Run->FlushPublishedEvents(true);
        TestEqual(TEXT("All65 source-authored final GEs retain the original full parameters"), Finals, 65);
        TestEqual(TEXT("Full batch still commits cooldown once"), Cooldowns, 1);
        TestEqual(TEXT("Full batch completes normally"), F.Run->GetEndReason(), EAbilityGraphEndReason::Completed);
        for (int32 Index = 0; Index < TargetASCs.Num(); ++Index)
        {
            TestEqual(TEXT("Each final applies one additional actual GE"),
                      TargetASCs[Index]->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 80.f);
            TargetASCs[Index]->OnGameplayEffectAppliedDelegateToSelf.Remove(Hooks[Index]);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FElectrocuteRuntimePurePayloadLongLifetime,
    "test_25d.DynamicMage.NativeElectrocute.RuntimePetrifyPeriodSurvives325PaidPeriodsAndLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteRuntimePurePayloadLongLifetime::RunTest(const FString&)
{
    for (int32 Ending = 0; Ending < 3; ++Ending)
    {
        FElectrocuteFixture F;
        auto* OriginalSettings = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
        OriginalSettings->DamageInterval = .01f;
        auto* Base = AbilityElectrocuteFragments::Build(F.World, *OriginalSettings);
        if (!TestNotNull(TEXT("Original native fine definition has its real sustained host"), Base))
        {
            return false;
        }
        const auto* Period = Base->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Effect.ElectrocutePeriod");
            });
        if (!TestNotNull(TEXT("Only the original periodic payload slot is selected"), Period))
        {
            return false;
        }
        FAbilityRuntimeRecipeContribution Rune;
        Rune.SourceId = TEXT("Rune.Channel.ReplacePeriodWithPetrify");
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
        Patch.TargetNodeId = Period->NodeId;
        Patch.ReplacementTypeId = TEXT("Status.PetrifyBatch");
        auto* Payload = NewObject<UAbilityPetrifyBatchConfig>(F.World);
        Payload->Duration = 1.f;
        Patch.ReplacementConfig = Payload;
        Rune.Patches.Add(Patch);
        FString Error;
        UAbilityGraphDefinition* Parsed = nullptr;
        if (!TestTrue(TEXT("Runtime parser accepts a pure payload at the explicit native period slot"),
                      AbilityRuntimeRecipes::Compile(F.World, Base, {Rune}, Parsed, Error)))
        {
            AddInfo(Error);
            return false;
        }
        F.Graph = Parsed;
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        const TStrongObjectPtr<UAbilitySystemComponent> TargetReferences(TargetASC);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 10000.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 10000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
        F.Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        int32 Paid = 0;
        int32 OriginalPeriods = 0;
        int32 Finals = 0;
        int32 Cooldowns = 0;
        F.Context->CommitPeriodCost = [&]()
        {
            const auto Spec =
                F.ASC->MakeOutgoingSpec(UAbilityGraphTestCost::StaticClass(), 1.f, F.ASC->MakeEffectContext());
            const auto Applied = F.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            Paid += Applied.WasSuccessfullyApplied() ? 1 : 0;
            return Applied.WasSuccessfullyApplied();
        };
        F.Context->CommitEndCooldown = [&]()
        {
            ++Cooldowns;
            if (Ending == 0)
            {
                TestEqual(TEXT("Original final settles before normal cooldown"), Finals, 1);
            }
        };
        const auto Hook = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                if (Spec.Def != GetDefault<UArcaneShardTestDamageEffect>())
                {
                    return; // 石化自身的控制GE不是旧周期伤害，仍单独检查真实HP与状态。
                }
                const auto* Author = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
                OriginalPeriods += Author == TargetASC ? 1 : 0;
                Finals += Author == F.ASC ? 1 : 0;
            });
        if (!TestTrue(TEXT("Actual parsed replacement starts the existing native host"), F.Start()))
        {
            TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
            return false;
        }
        F.World->GetTimerManager().Tick(0.f);
        for (int32 Index = 0; Index < 325 && !F.Run->IsFinished(); ++Index)
        {
            ++GFrameCounter;
            F.World->GetTimerManager().Tick(.011f);
            F.Run->Advance(.011f);
            TestTrue(TEXT("Consumed pure payload metadata is retired rather than accumulated"),
                     F.Run->GetActiveInvocationCount() <= 3);
        }
        TestFalse(TEXT("A supported pure payload replacement does not truncate the original sustained host at64"),
                  F.Run->IsFinished());
        TestEqual(TEXT("Each of325 periods still pays its real original cost exactly once"), Paid, 325);
        TestEqual(TEXT("Old periodic damage was actually replaced"), OriginalPeriods, 0);
        TestEqual(TEXT("Pure status payload causes no hidden HP damage"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 10000.f);
        TestEqual(TEXT("Real Mana reflects325 original fees"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 6750.f);
        TestTrue(TEXT("The replacement actually applies petrify through its existing owner"), F.IsPetrified());
        if (Ending == 0)
        {
            F.Context->RequestCompletion();
            F.Run->FlushPublishedEvents(true);
            TestEqual(TEXT("Original release retains one final"), Finals, 1);
            TestEqual(TEXT("Original release retains one cooldown"), Cooldowns, 1);
            TestEqual(TEXT("Original final retains its actual HP effect"),
                      TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 9990.f);
        }
        else if (Ending == 1)
        {
            F.Source->Destroy();
            F.Run->Advance(.02f);
        }
        else
        {
            F.Target->Destroy();
            F.Step(.02f);
        }
        TestTrue(TEXT("Release or exact source/target retirement closes the actual invocation"), F.Run->IsFinished());
        TestEqual(TEXT("No host/payload invocation leaks after ending"), F.Run->GetActiveInvocationCount(), 0);
        const int32 Before = Paid;
        F.Step(1.1f);
        TestEqual(TEXT("No late upkeep resumes after ending"), Paid, Before);
        TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FElectrocuteOriginalTargetRetirementControl,
    "test_25d.DynamicMage.NativeElectrocute.OriginalTargetRetirementStopsAfterOneTerminationPayment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElectrocuteOriginalTargetRetirementControl::RunTest(const FString&)
{
    FElectrocuteFixture F;
    auto* Settings = CastChecked<UAbilityElectrocuteNodeConfig>(F.Graph->Nodes[0].Config);
    Settings->DamageInterval = .01f;
    F.Graph->CompatibilityProfile = TEXT("LegacyActor.Channel.v1");
    F.Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::LegacyStreaming;
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
    int32 Paid = 0;
    F.Context->CommitPeriodCost = [&]()
    {
        const auto Spec =
            F.ASC->MakeOutgoingSpec(UAbilityGraphTestCost::StaticClass(), 1.f, F.ASC->MakeEffectContext());
        const auto Handle = F.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        Paid += Handle.WasSuccessfullyApplied() ? 1 : 0;
        return Handle.WasSuccessfullyApplied();
    };
    if (!TestTrue(TEXT("Original native macro control starts"), F.Start()))
    {
        return false;
    }
    F.World->GetTimerManager().Tick(0.f);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        F.Step(.011f);
    }
    TestEqual(TEXT("Three actual original period fees precede retirement"), Paid, 3);
    F.Target->Destroy();
    F.Step(.02f);
    TestTrue(TEXT("Original target-life walk completes after lost primary"), F.Run->IsFinished());
    TestEqual(TEXT("The original period cost happens before lost-target processing"), Paid, 4);
    TestEqual(TEXT("Original target retirement releases all invocation ownership"), F.Run->GetActiveInvocationCount(),
              0);
    F.Step(1.1f);
    TestEqual(TEXT("Original target retirement has no repeatedly late fees"), Paid, 4);
    return true;
}
#endif
