// 测试链：原策略和细图 -> 同一真实定时器/Actor GE/Mass -> 逐点伤害、取消与替换效果证据。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "Actor/PointCollection.h"
#include "Components/CapsuleComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

namespace AbilityArcaneShardFragmentTests
{
    /** 真实友方生命和 Mass 敌方同时存在；友方只由原碎片兼容合同授权。 */
    FAbilityNodeInput Input(FAbilityGraphTestFixture& F, int32 Count)
    {
        F.Target->Tags = {TEXT("Player")};
        F.Target->SetActorLocation(FVector(600, 0, 0));
        F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        auto* ASC = F.Target->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        FAbilityNodeInput Result;
        Result.SourceToken = F.SourceToken;
        Result.Origin = FVector(600, 0, 0);
        Result.DamageParams.BaseDamage = 10.f;
        Result.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Magic"));
        Result.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        auto* Points = NewObject<UAbilityArcaneShardContext>(F.World);
        Points->PointCollection = F.World->SpawnActor<APointCollection>();
        Points->Points.Init(Result.Origin, Count);
        Result.Context = Points;
        return Result;
    }

    /** 保留原首片/循环检查/尾等待，测试时只推进真实世界定时器和同一个执行器。 */
    void Step(FAbilityGraphTestFixture& F, UAbilityGraphInstance& Run, int32 Frames)
    {
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            ++GFrameCounter;
            F.World->GetTimerManager().Tick(.05f);
            Run.Advance(0.f);
        }
    }

    UAbilityGraphDefinition* Graph(FAbilityGraphTestFixture& F, bool bFine)
    {
        auto* Settings = NewObject<UAbilityArcaneShardNodeConfig>(F.World);
        Settings->Interval = .2f;
        Settings->bIncludeMass = true;
        if (bFine)
        {
            return AbilityArcaneShardFragments::Build(F.World, *Settings);
        }
        auto* Result = NewObject<UAbilityGraphDefinition>(F.World);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Shape.ArcaneShards");
        Node.Config = Settings;
        Result->Nodes.Add(Node);
        Result->EntryNodeId = Node.NodeId;
        return Result;
    }
} // namespace AbilityArcaneShardFragmentTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShardFineDefault,
                                 "test_25d.DynamicMage.NativeShards.FineDefaultPreservesRealCadenceAndLives",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShardFineDefault::RunTest(const FString&)
{
    using namespace AbilityArcaneShardFragmentTests;
    for (bool bFine : {false, true})
    {
        FAbilityGraphTestFixture F;
        const auto In = Input(F, 3);
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 100.f;
        Type->XPReward = 0;
        const int32 Id = Mass->SpawnMonster(Type, In.Origin);
        auto* Definition = Graph(F, bFine);
        if (!TestNotNull(bFine ? TEXT("Fine default definition exists") : TEXT("Original control exists"), Definition))
        {
            continue;
        }
        if (bFine)
        {
            TestFalse(TEXT("Fine definition contains no whole Shards macro"), Definition->Nodes.ContainsByPredicate(
                                                                                  [](const auto& Node)
                                                                                  {
                                                                                      return Node.TypeId ==
                                                                                             TEXT("Shape.ArcaneShards");
                                                                                  }));
            TestTrue(TEXT("Fine default separates time, query, payload and cue"), Definition->Nodes.Num() >= 4);
        }
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Actual graph starts"), Run->StartWithInput(Definition, In, {}, Error));
        AddInfo(Error);
        auto Health = [&F]()
        {
            return F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        };
        FPureMassView View;
        Mass->Resolve(Id, View);
        TestEqual(TEXT("First point synchronously damages actual Mass life"), View.Health, 90.f);
        TestEqual(TEXT("First point preserves actual friendly Actor GE path"), Health(), 90.f);
        TestFalse(TEXT("First point is before tail completion"), Run->IsFinished());
        Step(F, *Run, 30);
        Mass->Resolve(Id, View);
        TestEqual(TEXT("Three points damage actual Mass life once each"), View.Health, 70.f);
        TestEqual(TEXT("Three points preserve Actor damage once each"), Health(), 70.f);
        TestEqual(TEXT("Tail reaches ordinary completion"), Run->GetEndReason(), EAbilityGraphEndReason::Completed);
        TestFalse(TEXT("Point actor released at completion"),
                  IsValid(CastChecked<UAbilityArcaneShardContext>(In.Context.Get())->PointCollection));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShardFineReplacePayload,
                                 "test_25d.DynamicMage.NativeShards.PointCadenceAcceptsAlternateAreaPayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShardFineReplacePayload::RunTest(const FString&)
{
    using namespace AbilityArcaneShardFragmentTests;
    FAbilityGraphTestFixture F;
    auto In = Input(F, 3);
    F.Target->Tags = {TEXT("Enemy")};
    auto* Definition = Graph(F, true);
    if (!TestNotNull(TEXT("Fine definition exists"), Definition))
    {
        return false;
    }
    const auto* Clock = Definition->Nodes.FindByPredicate(
        [](const auto& Node)
        {
            return Node.TypeId == TEXT("Control.ShardPoints");
        });
    if (!TestNotNull(TEXT("Time ownership is an independently addressable node"), Clock))
    {
        return false;
    }
    const FGuid ClockId = Clock->NodeId;
    // 替换完整旧payload分支并移除不可达定义，不能靠放宽图校验保留悬空节点。
    Definition->Edges.Reset();
    Definition->Nodes.RemoveAll(
        [ClockId](const auto& Node)
        {
            return Node.NodeId != ClockId;
        });
    FAbilityGraphNode Area;
    Area.NodeId = FGuid::NewGuid();
    Area.TypeId = TEXT("Status.PetrifyArea");
    auto* Settings = NewObject<UAbilityAreaPetrifyNodeConfig>(Definition);
    Settings->Radius = 300.f;
    Settings->Duration = 2.f;
    Area.Config = Settings;
    Definition->Nodes.Add(Area);
    Definition->Edges.Add({ClockId, Area.NodeId, TEXT("Point")});
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Same graph accepts a different effect family"), Run->StartWithInput(Definition, In, {}, Error));
    AddInfo(Error);
    TestTrue(TEXT("First real ground point applies actual petrify"), F.IsPetrified());
    TestEqual(TEXT("Removed default damage path causes no GE"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    Run->Cancel();
    Step(F, *Run, 40);
    TestFalse(TEXT("Point actor released by clock cancellation"),
              IsValid(CastChecked<UAbilityArcaneShardContext>(In.Context.Get())->PointCollection));
    TestEqual(TEXT("Cancelled clock never returns to original damage"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShardFineDamageResultConsumer,
                                 "test_25d.DynamicMage.NativeShards.ExplicitPayloadConsumerOverridesOriginalFastPath",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShardFineDamageResultConsumer::RunTest(const FString&)
{
    using namespace AbilityArcaneShardFragmentTests;
    for (const bool bFine : {false, true})
    {
        FAbilityGraphTestFixture F;
        auto In = Input(F, 1);
        F.Target->Tags = {TEXT("Enemy")};
        auto* Definition = Graph(F, bFine);
        const auto* Clock = Definition->Nodes.FindByPredicate(
            [](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == TEXT("Control.ShardPoints") || Node.TypeId == TEXT("Shape.ArcaneShards");
            });
        CastChecked<UAbilityArcaneShardNodeConfig>(Clock->Config)->bPublishDamageResults = false;
        const auto* Damage = Definition->Nodes.FindByPredicate(
            [bFine](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == (bFine ? TEXT("Effect.ShardDamageBatch") : TEXT("Shape.ArcaneShards"));
            });
        const FGuid DamageId = Damage->NodeId;
        FAbilityGraphNode Resolve;
        Resolve.NodeId = FGuid::NewGuid();
        Resolve.TypeId = TEXT("Target.ResolveDamageTarget");
        Resolve.Config = NewObject<UAbilityResolveImpactNodeConfig>(Definition);
        FAbilityGraphNode Status;
        Status.NodeId = FGuid::NewGuid();
        Status.TypeId = TEXT("Status.Petrify");
        auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(Definition);
        Petrify->DurationSeconds = 2.f;
        Status.Config = Petrify;
        Definition->Nodes.Add(Resolve);
        Definition->Nodes.Add(Status);
        Definition->Edges.Add({DamageId, Resolve.NodeId, TEXT("DamageResolved")});
        Definition->Edges.Add({Resolve.NodeId, Status.NodeId, TEXT("TargetFound")});
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("A real typed DamageResolved consumer graph starts"),
                 Run->StartWithInput(Definition, In, {}, Error));
        AddInfo(Error);
        TestEqual(TEXT("Original default damage is still applied once"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  90.f);
        if (bFine)
        {
            TestTrue(TEXT("An explicit fine payload consumer receives the real result and petrifies"), F.IsPetrified());
        }
        else
        {
            TestFalse(TEXT("Original macro retains its explicit no-event contract"), F.IsPetrified());
        }
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShardFineIndependentDamageScale,
                                 "test_25d.DynamicMage.NativeShards.IndependentPayloadScaleConsumesCumulativeInputOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShardFineIndependentDamageScale::RunTest(const FString&)
{
    using namespace AbilityArcaneShardFragmentTests;
    FAbilityGraphTestFixture F;
    auto In = Input(F, 1);
    In.DamageScale = 1.5f;
    // 此用例必须实际消费BaseDamage；固定-10的旧控件GE无法观察倍率变化。
    In.DamageParams.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
    In.DamageParams.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
    auto* Base = Graph(F, true);
    const auto Damage = Base->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Effect.ShardDamageBatch");
        });
    Damage->Config = NewObject<UAbilityShardDamageConfig>(Base);
    FAbilityRuntimeRecipeContribution Rune;
    Rune.SourceId = TEXT("Rune.OnlyShardPayload");
    FAbilityRuntimeRecipePatch Patch;
    Patch.TargetTypeId = TEXT("Effect.ShardDamageBatch");
    Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
    Patch.Scalar = 2.f;
    Rune.Patches.Add(Patch);
    UAbilityGraphDefinition* Compiled = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Only the fine payload node supports typed damage scaling"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Rune}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Compiling a rune preserves the base payload multiplier"),
              CastChecked<UAbilityShardDamageConfig>(Damage->Config)->DamageMultiplier, 1.f);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Compiled scaled payload starts with the original point clock"),
             Run->StartWithInput(Compiled, In, {}, Error));
    AddInfo(Error);
    TestEqual(TEXT("One physical point consumes original10 times input1.5 times payload2 exactly once"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 70.f);
    TestEqual(TEXT("Original damage params stay unscaled for future contacts"), In.DamageParams.BaseDamage, 10.f);
    TestEqual(TEXT("Input cumulative scalar is not rewritten by the payload"), In.DamageScale, 1.5f);
    Run->Cancel();
    return true;
}
#endif
