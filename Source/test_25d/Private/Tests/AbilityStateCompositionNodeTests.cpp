// 调用链：Automation -> 类型节点/ExtensionWorld 共享状态 -> 原 Actor/Mass 生命及位移 -> 任意后继效果。
// 不写资产、不创建虚假 CompleteChild；以真实石化、Mass 扣血、RootMotion 租约与 GC 回调验证拆分。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "MyGameplayTags.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Script.h"

namespace AbilityStateCompositionTests
{
    /** 所有组合在测试时创建，无预烘焙组合 GA/子资产。 */
    UAbilityGraphDefinition* Graph(UObject* Outer)
    {
        auto* Result = NewObject<UAbilityGraphDefinition>(Outer);
        Result->SchemaVersion = 2;
        Result->CompatibilityProfile = TEXT("UnifiedV2");
        return Result;
    }

    FGuid Add(UAbilityGraphDefinition* Definition, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        Definition->Nodes.Add(Node);
        if (!Definition->EntryNodeId.IsValid())
        {
            Definition->EntryNodeId = Node.NodeId;
        }
        return Node.NodeId;
    }

    void Edge(UAbilityGraphDefinition* Definition, FGuid From, FName Port, FGuid To)
    {
        FAbilityGraphEdge Link;
        Link.From = From;
        Link.OutputPort = Port;
        Link.To = To;
        Definition->Edges.Add(Link);
    }

    /** 显式注册新策略，产品内建注册由 root 接入；未知节点不能回退旧宏。 */
    bool Start(UAbilityGraphInstance* Run, UAbilityGraphDefinition* Definition, const FAbilityNodeInput& Input,
               FString& Error)
    {
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        AbilityStateCompositionNodes::Register(Registry);
        return Run->StartWithInput(Definition, Input, {}, Registry, Error);
    }

    /** 使用正式 Mass 实体，RootMotion/状态/伤害查询均解析同一原生命。 */
    int32 Mass(FAbilityGraphTestFixture& F, FVector Position, float Health = 200.f)
    {
        auto* Subsystem = F.World->GetSubsystem<UPureMassSubsystem>();
        Subsystem->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = Health;
        Type->Radius = 20.f;
        Type->HalfHeight = 40.f;
        Type->XPReward = 0;
        return Subsystem->SpawnMonster(Type, Position);
    }

    float Health(FAbilityGraphTestFixture& F, int32 Id)
    {
        FPureMassView View;
        return F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) ? View.Health : -1.f;
    }

    bool Petrified(FAbilityGraphTestFixture& F, int32 Id)
    {
        FPureMassView View;
        return F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) && View.bPetrified;
    }

    FAbilityNodeInput Input(FAbilityGraphTestFixture& F, FPureMassCombatTarget Target)
    {
        FAbilityNodeInput Result;
        Result.SourceToken = F.SourceToken;
        Result.Direction = FVector::ForwardVector;
        FString Error;
        FAbilityTargetToken Token;
        if (AbilityExecutionIdentity::CaptureTarget(F.SourceToken, Target, Token, Error))
        {
            Result.LiveTargetToken = Token;
        }
        return Result;
    }

    /** 后继查询只决定几何；具体效果分别使用真实石化与真实伤害，验证 payload 可替换。 */
    void Area(UAbilityGraphDefinition* Definition, FGuid Parent, FName Port, float Radius, bool bPetrify,
              float Damage = 17.f)
    {
        auto* QueryConfig = NewObject<UAbilityQueryNodeConfig>(Definition);
        QueryConfig->Query.Radius = Radius;
        QueryConfig->Query.bRequireVisibility = false;
        const auto Query = Add(Definition, TEXT("Query.Shape"), QueryConfig);
        Edge(Definition, Parent, Port, Query);
        FGuid Effect;
        if (bPetrify)
        {
            auto* State = NewObject<UAbilityPetrifyNodeConfig>(Definition);
            State->DurationSeconds = 1.f;
            Effect = Add(Definition, TEXT("Status.Petrify"), State);
        }
        else
        {
            auto* State = NewObject<UAbilityDamageNodeConfig>(Definition);
            State->Damage = Damage;
            State->DamagePerLevel = 0.f;
            State->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Effect = Add(Definition, TEXT("Effect.Damage"), State);
        }
        Edge(Definition, Query, TEXT("TargetFound"), Effect);
    }

    /** Context 使用原公共状态类型和真实来源生命，不维护第二套跨施法叠层字典。 */
    UPrototypeCastContext* Context(FAbilityGraphTestFixture& F)
    {
        auto* Result = NewObject<UPrototypeCastContext>(F.World);
        Result->Caster = F.Source;
        Result->CastId = FGuid::NewGuid();
        Result->DamageParams.SourceAbilitySystemComponent = F.ASC;
        FString Error;
        Result->CaptureSourceLife(Error);
        return Result;
    }

    /** 合法位移配置的完整行为就是 Displace；没有占位子节点或固定伤害。 */
    UExtensionNode* Displace(UObject* Outer)
    {
        auto* Config = NewObject<UExtensionNode>(Outer);
        Config->Behavior = EExtensionKind::Displace;
        Config->Duration = .5f;
        Config->MoveDistance = 180.f;
        Config->SafeDistance = 70.f;
        Config->bPull = true;
        return Config;
    }

    void Floor(FAbilityGraphTestFixture& F, float Z)
    {
        auto* Actor = F.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);
        Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2000, 2000, 10));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();
        Actor->SetActorLocation(FVector(0, 0, Z));
    }
} // namespace AbilityStateCompositionTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttachArbitraryPayloadTransfer,
                                 "test_25d.UnifiedGA.DynamicMage.State.AttachPeriodPayloadAndTransfer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAttachArbitraryPayloadTransfer::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    const int32 Next = Mass(F, FVector(250, 0, 0));
    auto* Definition = Graph(F.World);
    auto* Attach = NewObject<UAbilityAttachNodeConfig>(Definition);
    Attach->Period = .1f;
    Attach->Duration = 1.f;
    Attach->TransferRadius = 250.f;
    const auto Root = Add(Definition, TEXT("State.Attach"), Attach);
    auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(Definition);
    Petrify->DurationSeconds = 1.f;
    Edge(Definition, Root, TEXT("Period"), Add(Definition, TEXT("Status.Petrify"), Petrify));
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Attach schedules arbitrary typed state without a legacy PeriodChild"),
                  Start(Run, Definition, Input(F, FPureMassCombatTarget::FromActor(F.Target)), Error)))
    {
        AddInfo(Error);
        return false;
    }
    FVector LastPeriod = FVector::ZeroVector;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("Period"))
            {
                LastPeriod = Event.Input.Origin;
            }
        });
    F.Target->SetActorLocation(FVector(180, 0, 0));
    Run->Advance(.11f);
    TestTrue(TEXT("Period payload applies actual Actor petrification"), F.Target->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Period originates at current anchor position"), LastPeriod.Equals(FVector(180, 0, 0), .01f));
    F.Target->Destroy();
    Run->Advance(.11f);
    TestTrue(TEXT("Anchor loss transfers to real Mass life and preserves state payload"), Petrified(F, Next));
    TestTrue(TEXT("New period position follows transferred anchor"), LastPeriod.Equals(FVector(250, 0, 0), .01f));
    const int64 Events = Run->GetAcceptedEventCount();
    Run->Cancel();
    Run->Advance(1.f);
    TestEqual(TEXT("Cancelled anchor never emits later normal periods"), int64(Run->GetAcceptedEventCount()), Events);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedStackNoFakeChild,
                                 "test_25d.UnifiedGA.DynamicMage.State.SharedStackLeaseAndGcLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedStackNoFakeChild::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FAbilityGraphTestFixture F;
    auto* WorldState = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    const auto Target = Input(F, FPureMassCombatTarget::FromActor(F.Target));
    if (!TestTrue(TEXT("Target life is captured"), Target.LiveTargetToken.IsSet()))
    {
        return false;
    }
    FExtensionStackSettings Settings;
    Settings.EffectId = TEXT("TypedShared");
    Settings.Threshold = 3;
    Settings.DetonationDelay = .1f;
    FString Error;
    if (!TestTrue(TEXT("Typed stack does not require a fake CompleteChild"), Settings.Validate(Error)))
    {
        AddInfo(Error);
        return false;
    }
    int32 Replaced = 0;
    int32 Triggered = 0;
    int32 Released = 0;
    FGuid FirstLease;
    FGuid LatestLease;
    UPrototypeCastContext* LatestContext = nullptr;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FExtensionStackRequest Request;
        Request.Context = Context(F);
        auto* RetainedConfig = NewObject<UAbilityStackNodeConfig>(F.World);
        RetainedConfig->Stack = Settings;
        Request.RetainedSource = RetainedConfig;
        Request.Settings = Settings;
        Request.TargetLife = Target.LiveTargetToken.GetValue();
        Request.Position = F.Target->GetActorLocation();
        Request.Direction = FVector::ForwardVector;
        Request.Depth = 2;
        Request.OnReleased = [&](EExtensionStackReleaseReason Reason)
        {
            Replaced += Reason == EExtensionStackReleaseReason::Replaced ? 1 : 0;
            Released += Reason == EExtensionStackReleaseReason::Triggered ? 1 : 0;
        };
        Request.OnThreshold = [&](const FExtensionStackTrigger& Trigger)
        {
            ++Triggered;
            CollectGarbage(RF_NoFlags);
            TestTrue(TEXT("Removed stack retains original Context through callback GC"), IsValid(Trigger.Context));
            TestTrue(TEXT("Removed stack retains source UObject through callback GC"), IsValid(Trigger.RetainedSource));
            TestTrue(TEXT("Latest application owns threshold lease"), Trigger.Lease == LatestLease);
            TestEqual(TEXT("Latest cast context supplies threshold"), Trigger.Context, LatestContext);
            TestEqual(TEXT("Original graph depth is preserved"), Trigger.Depth, 2);
            TestTrue(TEXT("Historical target life is preserved"),
                     AbilityBehaviorPrimitives::SameTargetLife(Trigger.TargetLife, Target.LiveTargetToken.GetValue()));
        };
        LatestContext = Request.Context;
        LatestLease = WorldState->AddStackShared(Request, Error);
        if (!TestTrue(TEXT("Cross-cast shared application returns an owned lease"), LatestLease.IsValid()))
        {
            AddInfo(Error);
            return false;
        }
        if (Index == 0)
        {
            FirstLease = LatestLease;
        }
    }
    TestEqual(TEXT("New applications release previous waiters"), Replaced, 2);
    TestFalse(TEXT("Old lease cannot cancel latest cross-cast state"), WorldState->CancelStack(FirstLease));
    TestEqual(TEXT("Only one shared record exists"), WorldState->StackCount(), 1);
    WorldState->Tick(.11f);
    TestEqual(TEXT("One typed threshold fires without any legacy child"), Triggered, 1);
    TestEqual(TEXT("Threshold releases newest waiter once"), Released, 1);
    TestEqual(TEXT("Consumed state is removed before callbacks"), WorldState->StackCount(), 0);
    WorldState->Tick(1.f);
    TestEqual(TEXT("Threshold cannot repeat"), Triggered, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackGraphArbitraryAreaState,
                                 "test_25d.UnifiedGA.DynamicMage.State.CrossCastThresholdToMixedAreaState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStackGraphArbitraryAreaState::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    const int32 Neighbor = Mass(F, FVector(200, 0, 0));
    auto* Definition = Graph(F.World);
    auto* Stack = NewObject<UAbilityStackNodeConfig>(Definition);
    Stack->Stack.EffectId = TEXT("RuntimeStateThreshold");
    Stack->Stack.Threshold = 3;
    Stack->Stack.DetonationDelay = .1f;
    const auto Root = Add(Definition, TEXT("State.Stack"), Stack);
    Area(Definition, Root, TEXT("Threshold"), 100.f, true);
    TArray<UAbilityGraphInstance*> Runs;
    FString Error;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        if (!TestTrue(TEXT("Separate runtime casts share one typed stack key"),
                      Start(Run, Definition, Input(F, FPureMassCombatTarget::FromActor(F.Target)), Error)))
        {
            AddInfo(Error);
            return false;
        }
        Runs.Add(Run);
    }
    Runs[0]->Advance(0.f);
    Runs[1]->Advance(0.f);
    TestTrue(TEXT("First and second callers are released by ownership transfer"),
             Runs[0]->IsFinished() && Runs[1]->IsFinished());
    Runs[0]->Cancel();
    TestEqual(TEXT("Late old graph cleanup preserves latest 3 layers"),
              F.World->GetSubsystem<UExtensionWorldSubsystem>()->LayersFor(F.Source, Stack->Stack.EffectId,
                                                                           FPureMassCombatTarget::FromActor(F.Target)),
              3);
    F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(.11f);
    Runs[2]->Advance(0.f);
    TestTrue(TEXT("Origin threshold drives arbitrary Actor status payload"), F.Target->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Same threshold geometry also affects actual Mass"), Petrified(F, Neighbor));
    TestTrue(TEXT("Latest waiting graph ends after release"), Runs[2]->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackLethalHistoricalOrigin,
                                 "test_25d.UnifiedGA.DynamicMage.State.LethalStackDamageToHistoricalArea",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStackLethalHistoricalOrigin::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    const int32 Victim = Mass(F, FVector(300, 0, 0), 5.f);
    const int32 Neighbor = Mass(F, FVector(330, 0, 0));
    auto* Definition = Graph(F.World);
    auto* Hit = NewObject<UAbilityDamageNodeConfig>(Definition);
    Hit->Damage = 10.f;
    Hit->DamagePerLevel = 0.f;
    Hit->DamageType = FMyGameplayTags::Get().Damage_Magic;
    const auto Root = Add(Definition, TEXT("Effect.Damage"), Hit);
    auto* Stack = NewObject<UAbilityStackNodeConfig>(Definition);
    Stack->Stack.EffectId = TEXT("LethalTypedThreshold");
    Stack->Stack.Threshold = 1;
    const auto Mark = Add(Definition, TEXT("State.StackDamage"), Stack);
    Edge(Definition, Root, TEXT("DamageResolved"), Mark);
    Area(Definition, Mark, TEXT("Threshold"), 80.f, false, 13.f);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Damage result can add a confirmed lethal stack using historical target life"),
                  Start(Run, Definition, Input(F, FPureMassCombatTarget(Victim)), Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Original target is actually killed"), Health(F, Victim), -1.f);
    F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(.01f);
    Run->Advance(0.f);
    TestEqual(TEXT("Original lethal position drives a distinct arbitrary damage payload"), Health(F, Neighbor), 187.f);
    F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(1.f);
    Run->Advance(1.f);
    TestEqual(TEXT("Old lethal target cannot detonate the area twice"), Health(F, Neighbor), 187.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetDisplacementComposition,
                                 "test_25d.UnifiedGA.DynamicMage.State.TargetDisplaceCompletionAreaAndReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTargetDisplacementComposition::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    Floor(F, -51.f);
    const int32 Victim = Mass(F, FVector(200, 0, 0));
    const int32 Neighbor = Mass(F, FVector(90, 0, 0));
    auto* Definition = Graph(F.World);
    auto* Move = NewObject<UAbilityTargetDisplaceNodeConfig>(Definition);
    Move->Settings = Displace(Definition);
    const auto Root = Add(Definition, TEXT("Movement.TargetDisplace"), Move);
    Area(Definition, Root, TEXT("Completed"), 40.f, false);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Target displacement uses original world movement protocol"),
                  Start(Run, Definition, Input(F, FPureMassCombatTarget(Victim)), Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    auto& Fragment =
        F.World->GetSubsystem<UMassEntitySubsystem>()
            ->GetMutableEntityManager()
            .GetFragmentDataChecked<FPureMassFragment>(F.World->GetSubsystem<UPureMassSubsystem>()->GetEntity(Victim));
    State->MoveMass(Fragment, .5f);
    // 正式处理器在位移之后发布实体查询快照；仅改 Fragment 会令后继范围仍读到旧位置。
    auto* MassState = F.World->GetSubsystem<UPureMassSubsystem>();
    if (MassState->BeginUpdate(.001f))
    {
        MassState->UpdateMonster(Fragment);
        MassState->FinishUpdate();
    }
    State->Tick(.5f);
    Run->Advance(.5f);
    TestTrue(TEXT("Original pull reaches safe distance"), FMath::IsNearlyEqual(Fragment.Location.X, 70.0, 1.));
    TestEqual(TEXT("Completion area originates at actual moved target"), Health(F, Victim), 183.f);
    TestEqual(TEXT("Completion area reaches nearby different target"), Health(F, Neighbor), 183.f);
    TestTrue(TEXT("Movement graph releases its wait"), Run->IsFinished());

    const int32 Other = Mass(F, FVector(500, 0, 0));
    auto OtherInput = Input(F, FPureMassCombatTarget(Other));
    OtherInput.Origin = FVector(300, 0, 0);
    auto* First = NewObject<UAbilityGraphInstance>(F.World);
    auto* Second = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("First displacement starts"), Start(First, Definition, OtherInput, Error));
    TestTrue(TEXT("Second displacement replaces the exact target command"),
             Start(Second, Definition, OtherInput, Error));
    First->Advance(.5f);
    First->Cancel();
    Second->Cancel();
    State->Tick(1.f);
    First->Advance(1.f);
    Second->Advance(1.f);
    TestEqual(TEXT("Replacement/cancellation cannot run a normal completion area"), Health(F, Other), 200.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetDisplacementExactRootMotion,
                                 "test_25d.UnifiedGA.DynamicMage.State.TargetDisplaceExactRootMotionCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTargetDisplacementExactRootMotion::RunTest(const FString&)
{
    using namespace AbilityStateCompositionTests;
    FAbilityGraphTestFixture F;
    Floor(F, -10.f);
    const float Half = F.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    F.Target->SetActorLocation(FVector(200, 0, Half + 2.f));
    auto* Config = Displace(F.World);
    auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    auto* Cast = Context(F);
    const auto Target = FPureMassCombatTarget::FromActor(F.Target);
    const auto First =
        State->RequestDisplacement(Cast, Config, Target, FVector(0, 0, Half + 2.f), FVector::ForwardVector);
    const auto OldRootMotion = F.Target->GetCharacterMovement()->GetRootMotionSource(TEXT("ExtensionDisplace"));
    const auto Second =
        State->RequestDisplacement(Cast, Config, Target, FVector(0, 0, Half + 2.f), FVector::ForwardVector);
    TestTrue(TEXT("Original world API creates both concrete movement commands"), First.IsValid() && Second.IsValid());
    TSharedPtr<FRootMotionSource> CurrentRootMotion;
    auto* Movement = F.Target->GetCharacterMovement();
    for (const auto& Motion : Movement->CurrentRootMotion.PendingAddRootMotionSources)
    {
        if (Motion.IsValid() && Motion->InstanceName == TEXT("ExtensionDisplace") &&
            !Motion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))
        {
            CurrentRootMotion = Motion;
        }
    }
    if (!TestTrue(TEXT("Latest exact force object exists"), CurrentRootMotion.IsValid()))
    {
        return false;
    }
    TestTrue(TEXT("Replacement already retires the original force object"),
             OldRootMotion.IsValid() && OldRootMotion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    TestFalse(TEXT("Old replaced lease is no longer active"), State->IsDisplacementActive(First));
    TestTrue(TEXT("Current exact lease is active"), State->IsDisplacementActive(Second));
    TestFalse(TEXT("Late old cleanup cannot cancel current root motion"), State->CancelDisplacement(First));
    TestTrue(TEXT("Current command remains active after wrong cancellation"), State->IsDisplacementActive(Second));
    TestFalse(TEXT("Wrong lease cannot mark the current force for removal"),
              CurrentRootMotion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    TestTrue(TEXT("Current command can release its exact root motion resource"), State->CancelDisplacement(Second));
    TestFalse(TEXT("Cancelled exact command is removed"), State->IsDisplacementActive(Second));
    TestTrue(TEXT("Exact cancellation actually retires the owned force object"),
             CurrentRootMotion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
    return true;
}
#endif
