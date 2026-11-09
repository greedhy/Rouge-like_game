// 测试链：真实 World/ASC -> 注册 ArcaneShards 策略 -> 原世界定时器 -> Mass 生命值与取消资源断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "Actor/PointCollection.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "Components/CapsuleComponent.h"

namespace
{
    /** 测试直接给冻结地面点，避免随机角度、素材和地图地面对伤害时序断言造成噪声。 */
    UAbilityGraphDefinition* ShardGraph(FAbilityGraphTestFixture& F, bool bMass)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Shape.ArcaneShards");
        auto* Config = NewObject<UAbilityArcaneShardNodeConfig>(Graph);
        Config->Interval = .2f;
        Config->bIncludeMass = bMass;
        Node.Config = Config;
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Add(Node);
        return Graph;
    }
    FAbilityNodeInput ShardInput(FAbilityGraphTestFixture& F, int32 Count)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = F.SourceToken;
        Input.Origin = FVector(600, 0, 0);
        Input.DamageParams.BaseDamage = 10.f;
        Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Magic;
        auto* Context = NewObject<UAbilityArcaneShardContext>(F.World);
        Context->PointCollection = F.World->SpawnActor<APointCollection>();
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Context->Points.Add(Input.Origin);
        }
        Input.Context = Context;
        return Input;
    }
    void Step(FAbilityGraphTestFixture& F, UAbilityGraphInstance& Run, int32 Frames)
    {
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            ++GFrameCounter;
            F.World->GetTimerManager().Tick(.05f);
            Run.Advance(0.f);
        }
    }

    /** 真实同阵营 Actor 与查询碰撞；伤害使用即时 GE 修改真实属性。 */
    void PrepareFriendlyTarget(FAbilityGraphTestFixture& F)
    {
        F.Target->Tags = {FName(TEXT("Player"))};
        F.Target->SetActorLocation(FVector(600, 0, 0));
        F.Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        F.Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
        auto* ASC = F.Target->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneShardStagger, "test_25d.UnifiedGA.ArcaneShards.StaggerAndTail",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 一次同步首片、随后定时片、最后额外一次定时检查和0.5秒尾延迟。Mass 是显式新增可组合目标形式。 */
bool FArcaneShardStagger::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(600, 0, 0));
    auto Input = ShardInput(F, 3);
    auto* Context = CastChecked<UAbilityArcaneShardContext>(Input.Context.Get());
    TWeakObjectPtr<APointCollection> Owned = Context->PointCollection;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Shards = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Shards += Event.OutputPort == TEXT("PeriodElapsed") ? 1 : 0;
        });
    FString Error;
    TestTrue(TEXT("Strategy starts through registry"), Run->StartWithInput(ShardGraph(F, true), Input, {}, Error));
    FPureMassView View;
    TestTrue(TEXT("Target survives first shard"), Mass->Resolve(Id, View));
    TestEqual(TEXT("First shard happens synchronously"), View.Health, 90.f);
    TestEqual(TEXT("Initial period event delivered"), Shards, 1);
    TestFalse(TEXT("Tail is not instant"), Run->IsFinished());
    Step(F, *Run, 30);
    Mass->Resolve(Id, View);
    TestEqual(TEXT("Exactly three independent hits"), View.Health, 70.f);
    TestEqual(TEXT("Exactly three shard events"), Shards, 3);
    TestTrue(TEXT("Tail completes graph"), Run->IsFinished());
    TestFalse(TEXT("Point actor released at completion"), Owned.IsValid());
    Step(F, *Run, 10);
    Mass->Resolve(Id, View);
    TestEqual(TEXT("No timer remains after completion"), View.Health, 70.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneShardCancellation, "test_25d.UnifiedGA.ArcaneShards.CancelClosesTimers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 在首片事件取消，后续片、尾回调和本次点Actor全部清除。 */
bool FArcaneShardCancellation::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Input = ShardInput(F, 3);
    auto* Context = CastChecked<UAbilityArcaneShardContext>(Input.Context.Get());
    TWeakObjectPtr<APointCollection> Owned = Context->PointCollection;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Shards = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("PeriodElapsed"))
            {
                ++Shards;
                Run->Cancel();
            }
        });
    FString Error;
    TestTrue(TEXT("Cancellable strategy starts"), Run->StartWithInput(ShardGraph(F, false), Input, {}, Error));
    Step(F, *Run, 30);
    TestEqual(TEXT("Only first shard was accepted"), Shards, 1);
    TestFalse(TEXT("Owned point actor destroyed"), Owned.IsValid());
    TestTrue(TEXT("Cancellation reason retained"), Run->GetEndReason() == EAbilityGraphEndReason::Cancelled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneShardAuthority, "test_25d.UnifiedGA.ArcaneShards.TimerRevalidatesSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 世界定时器可能先于图Tick运行，回调自身仍拒绝换Avatar后的旧施法。 */
bool FArcaneShardAuthority::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Input = ShardInput(F, 3);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    TestTrue(TEXT("Source initially valid"), Run->StartWithInput(ShardGraph(F, false), Input, {}, Error));
    F.ASC->ClearActorInfo();
    Step(F, *Run, 4);
    TestTrue(TEXT("Old source closes timer graph"), Run->GetEndReason() == EAbilityGraphEndReason::SourceInvalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneShardFriendlyCompatibility,
                                 "test_25d.UnifiedGA.ArcaneShards.LegacyFriendlyActorDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 走真实策略、球查询和 GE，兼容原友方命中；策略显式敌对开关才拒绝同队。 */
bool FArcaneShardFriendlyCompatibility::RunTest(const FString&)
{
    for (const bool bHostileOnly : {false, true})
    {
        FAbilityGraphTestFixture F;
        PrepareFriendlyTarget(F);
        auto Input = ShardInput(F, 1);
        Input.DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
        auto* Graph = ShardGraph(F, false);
        CastChecked<UAbilityArcaneShardNodeConfig>(Graph->Nodes[0].Config)->bRequireHostileActorTargets = bHostileOnly;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        int32 DamageEvents = 0;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.OutputPort == TEXT("DamageResolved"))
                {
                    ++DamageEvents;
                    TestFalse(TEXT("Friendly damage never exports a live-target capability"),
                              Event.Input.LiveTargetToken.IsSet());
                    TestTrue(TEXT("Damage still carries a historical impact"), Event.Input.Impact.IsSet());
                }
            });
        FString Error;
        TestTrue(TEXT("Actor strategy starts"), Run->StartWithInput(Graph, Input, {}, Error));
        const float Health =
            F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        TestEqual(TEXT("Only explicit hostile filtering changes the original hit set"), Health,
                  bHostileOnly ? 100.f : 90.f);
        TestEqual(TEXT("Default standalone strategy publishes actual damage results"), DamageEvents,
                  bHostileOnly ? 0 : 1);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcaneShardCapturedActorLife,
                                 "test_25d.UnifiedGA.ArcaneShards.LegacyActorRejectsStaleLives",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 兼容友方不放宽生命权限：真实扣血后，换ASC绑定的旧来源和退休目标租约都不能继续扣血。 */
bool FArcaneShardCapturedActorLife::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    PrepareFriendlyTarget(F);
    F.Target->EnableDungeonPooling(0.f);
    FAbilitySourceToken TargetLife;
    FString Error;
    TestTrue(TEXT("Capture current friendly target life"),
             AbilityExecutionIdentity::CaptureSource(F.Target, TargetLife, Error));
    auto Params = ShardInput(F, 1).DamageParams;
    Params.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
    TestTrue(TEXT("Original friendly hit attempts real GE"),
             AbilityArcaneShardNodes::ApplyLegacyActor(F.SourceToken, TargetLife, Params).bAttempted);
    auto* TargetASC = F.Target->GetAbilitySystemComponent();
    TestEqual(TEXT("Real health decreases"), TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
              90.f);
    F.ASC->ClearActorInfo();
    TestFalse(TEXT("Retired source cannot apply"),
              AbilityArcaneShardNodes::ApplyLegacyActor(F.SourceToken, TargetLife, Params).bAttempted);
    TestEqual(TEXT("Invalid source leaves real health unchanged"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    F.ASC->InitAbilityActorInfo(F.Source, F.Source);
    F.Target->SleepForDungeonPool();
    TestFalse(TEXT("Retired target lease cannot apply"),
              AbilityArcaneShardNodes::ApplyLegacyActor(F.SourceToken, TargetLife, Params).bAttempted);
    TestTrue(TEXT("Same Actor starts a new pool life"),
             F.Target->WakeFromDungeonPool(FTransform(FVector(600, 0, 0)), NAME_None));
    const float NewLifeHealth = TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    TestFalse(TEXT("Old target lease cannot damage the new life"),
              AbilityArcaneShardNodes::ApplyLegacyActor(F.SourceToken, TargetLife, Params).bAttempted);
    TestEqual(TEXT("New life health stays unchanged"),
              TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), NewLifeHealth);
    return true;
}
#endif
