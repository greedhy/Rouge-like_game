// 调用链：Automation -> 正式 PlayerState/ASC 与图执行器 -> 真实伤害/费用 -> 开发观察值快照。
// 测试观察者不驱动图、不替代结算，验证同步事件、所属玩家隔离、有界记录与终态解绑。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimeRecipeObservationTests
{
    /** 真实 PlayerState ASC 绑定真实 Avatar，避免以传入伪 Token 绕过所属玩家验证。 */
    struct FPlayer
    {
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        FAbilitySourceToken Source;

        explicit FPlayer(FAbilityGraphTestFixture& F)
        {
            State = F.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->AttachState(State);
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            if (!Developer)
            {
                Developer = NewObject<UAbilityRuntimeRecipeDeveloperComponent>(State);
                State->AddInstanceComponent(Developer);
                Developer->RegisterComponent();
            }
            FString Error;
            AbilityExecutionIdentity::CaptureSource(Pawn, Source, Error);
        }
    };

    /** 原生 Effect.Damage 配置实际扣血；不在测试中模拟图事件。 */
    UAbilityGraphDefinition* DamageGraph(UObject* Outer)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        auto* Config = NewObject<UAbilityDamageNodeConfig>(Graph);
        Config->Damage = 25.f;
        Config->DamagePerLevel = 0.f;
        Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Effect.Damage");
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }

    /** 足够长的真实等待节点让测试可以同时持有 16 个仍活动的执行。 */
    UAbilityGraphDefinition* DelayGraph(UObject* Outer)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Control.DelayOrigin");
        auto* Config = NewObject<UAbilityDelayNodeConfig>(Graph);
        Config->DelaySeconds = 1.f;
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }
} // namespace AbilityRuntimeRecipeObservationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeSynchronousObservation,
                                 "test_25d.UnifiedGA.DynamicMage.Observation.SynchronousDamageAndTerminal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeSynchronousObservation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeObservationTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 200.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(250, 0, 0));
    FAbilityNodeInput Input;
    Input.SourceToken = Player.Source;
    FAbilityTargetToken Target;
    FString Error;
    if (!TestTrue(TEXT("Real source and Mass target life are captured"),
                  AbilityExecutionIdentity::CaptureTarget(Player.Source, FPureMassCombatTarget(Id), Target, Error)))
    {
        return false;
    }
    Input.LiveTargetToken = Target;
    auto* Graph = DamageGraph(F.World);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    const auto Tag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    const FGuid RootCastId = FGuid::NewGuid();
    const FString Hash = AbilityRuntimeRecipes::Fingerprint(*Graph);
    if (!TestTrue(TEXT("Observer attaches before an unstarted graph despite its initial Finished flag"),
                  Player.Developer->ObserveExecution(Run, Player.Source, Tag, 7, Hash, Error, RootCastId)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Existing executor starts and finishes synchronous actual damage"),
             Run->StartWithInput(Graph, Input, {}, Error));
    FPureMassView View;
    if (TestTrue(TEXT("Nonlethal target remains resolvable"), Mass->Resolve(Id, View)))
    {
        TestEqual(TEXT("Observer never applies additional damage"), View.Health, 175.f);
    }
    const auto* Damage = Player.Developer->GetObservations().FindByPredicate(
        [](const FAbilityRuntimeDeveloperObservation& Item)
        {
            return Item.Kind == EAbilityRuntimeObservationKind::GraphEvent && Item.Stage == TEXT("DamageResolved");
        });
    if (TestNotNull(TEXT("Synchronous damage receipt is not lost before observer binding"), Damage))
    {
        TestEqual(TEXT("Actual damage is independently known"), Damage->ActualDamage, 25.f);
        TestTrue(TEXT("Known damage and Applied are explicitly represented"),
                 Damage->bActualDamageKnown && Damage->bAppliedKnown && Damage->bApplied);
        TestEqual(TEXT("Original frozen revision is recorded"), Damage->Revision, 7);
        TestEqual(TEXT("Actual Mass identity is recorded"), Damage->MassEntityId, Id);
        TestEqual(TEXT("Registered strategy is present as a pure value"), Damage->NodeType,
                  FName(TEXT("Effect.Damage")));
        TestEqual(TEXT("Frozen recipe hash is preserved"), Damage->FrozenHash, Hash);
        TestTrue(TEXT("Cost/session correlation survives graph execution ID allocation"),
                 Damage->RootCastId == RootCastId);
    }
    int32 Terminals = 0;
    for (const auto& Item : Player.Developer->GetObservations())
    {
        Terminals += Item.Kind == EAbilityRuntimeObservationKind::GraphFinished ? 1 : 0;
    }
    TestEqual(TEXT("Exactly one graph terminal is recorded"), Terminals, 1);
    TestEqual(TEXT("Successful terminal removes observer's weak subscription"),
              Player.Developer->GetObservedExecutionCount(), 0);
    Run->Cancel();
    TestEqual(TEXT("Later repeated cancel produces no duplicate observation"),
              Player.Developer->GetObservations()
                  .FilterByPredicate(
                      [](const FAbilityRuntimeDeveloperObservation& Item)
                      {
                          return Item.Kind == EAbilityRuntimeObservationKind::GraphFinished;
                      })
                  .Num(),
              1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeObservationIsolation,
                                 "test_25d.UnifiedGA.DynamicMage.Observation.OwnerIsolationAndActiveLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeObservationIsolation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeObservationTests;
    FAbilityGraphTestFixture F;
    FPlayer One(F);
    FPlayer Two(F);
    auto* Graph = DelayGraph(F.World);
    const auto Tag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    const FString Hash = AbilityRuntimeRecipes::Fingerprint(*Graph);
    FString Error;
    auto* Foreign = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Other player's valid source is not accepted by this owner"),
              One.Developer->ObserveExecution(Foreign, Two.Source, Tag, 1, Hash, Error));
    TArray<UAbilityGraphInstance*> Runs;
    for (int32 Index = 0; Index < 16; ++Index)
    {
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!TestTrue(TEXT("Bounded observer accepts a distinct owned live run"),
                      One.Developer->ObserveExecution(Run, One.Source, Tag, 1, Hash, Error)))
        {
            return false;
        }
        FAbilityNodeInput Input;
        Input.SourceToken = One.Source;
        TestTrue(TEXT("Observed run still uses normal executor"), Run->StartWithInput(Graph, Input, {}, Error));
        Runs.Add(Run);
    }
    TestEqual(TEXT("Exactly 16 active observations are retained"), One.Developer->GetObservedExecutionCount(), 16);
    auto* Overflow = NewObject<UAbilityGraphInstance>(F.World);
    TestFalse(TEXT("Seventeenth observation is refused without changing the graph"),
              One.Developer->ObserveExecution(Overflow, One.Source, Tag, 1, Hash, Error));
    TestFalse(TEXT("The same run cannot attach duplicate callbacks"),
              One.Developer->ObserveExecution(Runs[0], One.Source, Tag, 1, Hash, Error));
    One.Developer->ForgetExecution(Runs[0]);
    TestFalse(TEXT("Detaching observation does not cancel the active effect"), Runs[0]->IsFinished());
    for (auto* Run : Runs)
    {
        Run->Cancel();
    }
    TestEqual(TEXT("Cancellation releases remaining subscriptions"), One.Developer->GetObservedExecutionCount(), 0);
    TestEqual(TEXT("Other player's log remains untouched"), Two.Developer->GetObservations().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeCostObservation,
                                 "test_25d.UnifiedGA.DynamicMage.Observation.ActualCostResultsAndRingLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeCostObservation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeObservationTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FPlayer Other(F);
    Player.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 2000.f);
    Player.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 2000.f);
    const auto Handle = Player.ASC->GiveAbility(FGameplayAbilitySpec(UAbilityGraphTestAbility::StaticClass(), 1));
    auto* Spec = Player.ASC->FindAbilitySpecFromHandle(Handle);
    auto* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
    if (!TestNotNull(TEXT("Actual cost-bearing GA instance is granted"), Ability))
    {
        return false;
    }
    const auto Tag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    const auto CastId = FGuid::NewGuid();
    FString Error;
    for (int32 Index = 0; Index < 140; ++Index)
    {
        FAbilityRuntimeDeveloperCost Cost;
        Cost.RootCastId = CastId;
        Cost.AbilityTag = Tag;
        Cost.Revision = 5;
        Cost.FrozenHash = TEXT("frozen-real-cost-example");
        Cost.Stage = TEXT("Channel.PeriodCost");
        Cost.ResourceBefore = Player.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        Cost.bSuccess = Ability->CheckCost(Handle, Player.ASC->AbilityActorInfo.Get());
        if (Cost.bSuccess)
        {
            Ability->ApplyCost(Handle, Player.ASC->AbilityActorInfo.Get(), {});
        }
        Cost.ResourceAfter = Player.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        TestEqual(TEXT("Real GE deducts exactly 10 for each recorded successful cost"), Cost.ResourceAfter,
                  Cost.ResourceBefore - 10.f);
        if (!TestTrue(TEXT("Validated original frontend cost can be recorded"),
                      Player.Developer->RecordCost(Player.Source, Cost, Error)))
        {
            AddInfo(Error);
            return false;
        }
    }
    Player.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 0.f);
    FAbilityRuntimeDeveloperCost Failed;
    Failed.RootCastId = CastId;
    Failed.AbilityTag = Tag;
    Failed.Revision = 5;
    Failed.FrozenHash = TEXT("frozen-real-cost-example");
    Failed.Stage = TEXT("Channel.PeriodCost");
    Failed.bSuccess = Ability->CheckCost(Handle, Player.ASC->AbilityActorInfo.Get());
    TestFalse(TEXT("Actual GE affordability check fails with empty mana"), Failed.bSuccess);
    TestTrue(TEXT("Failed actual cost result is still observable"),
             Player.Developer->RecordCost(Player.Source, Failed, Error));
    const auto& Records = Player.Developer->GetObservations();
    TestEqual(TEXT("Ring never keeps more than 128 records"), Records.Num(), 128);
    if (Records.Num() == 128)
    {
        TestEqual(TEXT("Oldest retained record has correct monotonic sequence"), Records[0].Sequence, int64(14));
        TestEqual(TEXT("Final failure remains distinguishable"), Records.Last().bCostSucceeded, false);
        TestEqual(TEXT("Final recorded actual mana remains zero"), Records.Last().ResourceAfter, 0.f);
        TestTrue(TEXT("Cost session correlation is preserved"), Records.Last().RootCastId == CastId);
    }
    TestFalse(TEXT("Valid other-owner source cannot write a forged observation"),
              Player.Developer->RecordCost(Other.Source, Failed, Error));
    TestEqual(TEXT("Other owner has no observational side effects"), Other.Developer->GetObservations().Num(), 0);
    return true;
}
#endif
