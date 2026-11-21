// 调用链：Automation -> Scope 原型帧 -> 连锁/光束 typed 调度 -> 既有批伤害/真实 GE -> 属性和实际回调顺序。
// 使用局部注册表，不编辑 Content；这些测试不替代前端输入、动画或网络画面验收。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "Components/CapsuleComponent.h"
#include "MyGameplayTags.h"
#include "EngineUtils.h"
#include "UObject/Script.h"

namespace AbilityPrototypeTargetNodeTests
{
    /** 两种旧 profile 进入相同节点；参数帧显式决定目标、时钟与取消差异。 */
    FName Profile(bool bActor)
    {
        return bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
    }

    int32 Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        return Definition.Nodes.Add(Node);
    }

    void Connect(UAbilityGraphDefinition& Definition, int32 From, FName Port, int32 To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = Definition.Nodes[From].NodeId;
        Edge.To = Definition.Nodes[To].NodeId;
        Edge.OutputPort = Port;
        Definition.Edges.Add(Edge);
    }

    /** 只启用宿主 BeginPlay；敌方和源 ASC 是独立真实属性，原型 Context 带实际伤害 GE。 */
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Binding = nullptr;
        TArray<FAbilityNodeInput> Contacts;
        TArray<FName> Order;
        TArray<TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>> Observers;
        int32 Expired = 0;
        int32 Periods = 0;

        FFixture()
        {
            Prepare(*Target);
            Target->SetActorLocation(FVector(100, 0, 0));
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
        }

        ~FFixture()
        {
            for (const auto& Pair : Observers)
            {
                if (auto* Component = Pair.Key.Get())
                {
                    Component->OnGameplayEffectAppliedDelegateToSelf.Remove(Pair.Value);
                }
            }
            if (Run)
            {
                Run->Cancel();
            }
        }

        static void Prepare(AEnemyCharacter& Actor)
        {
            auto* Component = Actor.GetAbilitySystemComponent();
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Actor.GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Actor.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Actor.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        }

        AEnemyCharacter* Other(FVector Position)
        {
            auto* Actor = World->SpawnActor<AEnemyCharacter>();
            Actor->Tags = {TEXT("Enemy")};
            Actor->SetActorLocation(Position);
            auto* Component = Actor->GetAbilitySystemComponent();
            Component->InitAbilityActorInfo(Actor, Actor);
            Component->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Prepare(*Actor);
            return Actor;
        }

        UAbilityPrototype* Parameters(EAbilityPrototypeKind Kind)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = Kind;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Damage = 40.f;
            Result->DamagePerLevel = 0.f;
            Result->Duration = 5.f;
            Result->Cooldown = 0.f;
            Result->Range = 1200.f;
            Result->Radius = 250.f;
            Result->Period = .1f;
            Result->Count = 3;
            Result->ManaPerSecond = 0.f;
            return Result;
        }

        float Health(AEnemyCharacter* Actor) const
        {
            return Actor->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }

        float Mana() const
        {
            return ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        }

        /** 每次生产 GE 应用后观察；费用 GE 在源 ASC，不会被当作目标伤害回调。 */
        void OnDamage(AEnemyCharacter& Actor, TFunction<void()> Callback)
        {
            auto* Component = Actor.GetAbilitySystemComponent();
            const auto Handle = Component->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [Callback = MoveTemp(Callback)](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec,
                                                FActiveGameplayEffectHandle)
                {
                    if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        Callback();
                    }
                });
            Observers.Add({Component, Handle});
        }

        /** 原型 Scope 保留真实一次逻辑准入；Action 接固定伤害证明任意 payload 可被调度。 */
        bool Start(UAbilityPrototype& InParameters, bool bActor, FString& Error)
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = Profile(bActor);
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
            auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Scope->Parameters = &InParameters;
            Scope->Profile = Definition->CompatibilityProfile;
            const int32 Entry = Add(*Definition, TEXT("Scope.Prototype"), Scope);
            const bool bChain = InParameters.Kind == EAbilityPrototypeKind::Chain;
            const int32 Schedule =
                Add(*Definition, bChain ? TEXT("Control.TargetSequence") : TEXT("Control.TargetChannel"),
                    bChain ? static_cast<UAbilityNodeConfig*>(NewObject<UAbilityTargetSequenceNodeConfig>(Definition))
                           : static_cast<UAbilityNodeConfig*>(NewObject<UAbilityTargetChannelNodeConfig>(Definition)));
            auto* Damage = NewObject<UAbilityDamageBatchNodeConfig>(Definition);
            Damage->Damage = 10.f;
            Damage->DamagePerLevel = 0.f;
            Damage->DamageType = InParameters.DamageType;
            Damage->Profile =
                bActor ? EAbilityDamageActionProfile::LegacyActor : EAbilityDamageActionProfile::LegacyMixed;
            const int32 Payload = Add(*Definition, TEXT("Effect.DamageBatch"), Damage);
            Connect(*Definition, Entry, TEXT("Ready"), Schedule);
            Connect(*Definition, Schedule, TEXT("Action"), Payload);
            Definition->EntryNodeId = Definition->Nodes[Entry].NodeId;
            Binding = NewObject<UPrototypeCastContext>(Source);
            Binding->Caster = Source;
            Binding->Level = 1;
            Binding->CastId = FGuid::NewGuid();
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            FAbilityNodeInput InInput;
            InInput.SourceToken = SourceToken;
            InInput.LiveTargetToken = TargetToken;
            InInput.Context = Binding;
            InInput.DamageParams = Binding->DamageParams;
            InInput.Direction = FVector::ForwardVector;
            Run = NewObject<UAbilityGraphInstance>(World);
            Run->OnEvent.AddLambda(
                [this](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType != TEXT("Control.TargetSequence") &&
                        Event.NodeType != TEXT("Control.TargetChannel"))
                    {
                        return;
                    }
                    Order.Add(Event.OutputPort);
                    if (Event.OutputPort == TEXT("Action"))
                    {
                        Contacts.Add(Event.Input);
                    }
                    else if (Event.OutputPort == TEXT("Expired"))
                    {
                        ++Expired;
                    }
                    else if (Event.OutputPort == TEXT("Period"))
                    {
                        ++Periods;
                    }
                });
            FAbilityNodeRegistry Nodes;
            Nodes.RegisterBuiltins();
            AbilityPrototypeTargetNodes::Register(Nodes);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            const bool bStarted = Run->StartWithInput(Definition, InInput, Options, Nodes, Error);
            if (!bStarted)
            {
                UE_LOG(LogTemp, Warning, TEXT("[PrototypeTargetFixture] %s %s graph start failed: %s"),
                       *Definition->CompatibilityProfile.ToString(), bChain ? TEXT("Chain") : TEXT("Beam"), *Error);
            }
            return bStarted;
        }

        /** 独立帧避免复用同一引擎帧掩盖 drainer 次数；不额外推进 World 子系统。 */
        void Advance(float Delta)
        {
            ++GFrameCounter;
            Run->Advance(Delta);
        }

        bool AllPresentationsFinished() const
        {
            int32 Hosts = 0;
            bool bFinished = true;
            for (TActorIterator<APrototypeRuntime> It(World); It; ++It)
            {
                ++Hosts;
                bFinished &= It->IsFinished();
            }
            for (TActorIterator<APureMassRuntime> It(World); It; ++It)
            {
                ++Hosts;
                bFinished &= It->IsFinished();
            }
            return Hosts > 0 && bFinished;
        }
    };
} // namespace AbilityPrototypeTargetNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeChainCountOneTail,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.ChainInitialContactAndProfileTail",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeChainCountOneTail::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain);
        Parameters->Count = 1;
        FString Error;
        TestTrue(TEXT("Target sequence graph starts"), F.Start(*Parameters, bActor, Error));
        AddInfo(Error);
        TestEqual(TEXT("Start does not execute a contact"), F.Health(F.Target), 100.f);
        F.Advance(0.f);
        TestEqual(TEXT("First zero-delta Advance applies actual payload once"), F.Health(F.Target), 90.f);
        TestEqual(TEXT("Actor waits for next period; Mixed expires on final contact"), F.Expired, bActor ? 0 : 1);
        TestEqual(TEXT("Only Mixed is finished on count-one contact frame"), F.Run->IsFinished(), !bActor);
        F.Advance(.11f);
        TestEqual(TEXT("Both eventually have exactly one expiry"), F.Expired, 1);
        TestEqual(TEXT("No extra tail damage"), F.Health(F.Target), 90.f);
        TestTrue(TEXT("Normal final contact cleans its actual presentation"), F.AllPresentationsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeChainFixedNext,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.ChainFixedNextRejectsMovedOrRecycledLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeChainFixedNext::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bRecycle : {false, true})
        {
            FFixture F;
            auto* Fixed = F.Other(FVector(150, 0, 0));
            auto* Replacement = F.Other(FVector(220, 0, 0));
            auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain);
            FString Error;
            TestTrue(TEXT("Sequence with next candidates starts"), F.Start(*Parameters, bActor, Error));
            F.Advance(0.f);
            TestEqual(TEXT("First actual payload reaches initial victim"), F.Health(F.Target), 90.f);
            if (bRecycle)
            {
                Fixed->EnableDungeonPooling();
            }
            else
            {
                Fixed->SetActorLocation(FVector(1500, 0, 0));
            }
            Replacement->SetActorLocation(FVector(110, 0, 0));
            F.Advance(.11f);
            TestEqual(TEXT("Fixed target's moved/reused life is not hit"), F.Health(Fixed), 100.f);
            TestEqual(TEXT("A newly nearest replacement cannot replace the already fixed next target"),
                      F.Health(Replacement), 100.f);
            TestTrue(TEXT("Invalid fixed step ends the chain normally"), F.Run->IsFinished());
            TestEqual(TEXT("Invalid fixed step still has one normal expiry"), F.Expired, 1);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeChainPayloadOrder,
    "test_25d.UnifiedGA.DynamicMage.TargetNodes.ChainSelectsAfterActualPayloadFromPreDamagePoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeChainPayloadOrder::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* FormerNearest = F.Other(FVector(150, 0, 0));
        auto* Correct = F.Other(FVector(220, 0, 0));
        F.OnDamage(*F.Target,
                   [&]()
                   {
                       F.Target->SetActorLocation(FVector(900, 0, 0));
                       FormerNearest->SetActorLocation(FVector(1500, 0, 0));
                   });
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain);
        FString Error;
        TestTrue(TEXT("Payload-sensitive sequence starts"), F.Start(*Parameters, bActor, Error));
        F.Advance(0.f);
        F.Advance(.11f);
        TestEqual(TEXT("Post-GE selection sees the moved former nearest"), F.Health(FormerNearest), 100.f);
        TestEqual(TEXT("Next selection occurs after actual GE but from original contact point"), F.Health(Correct),
                  90.f);
        if (TestTrue(TEXT("Two actual contact payloads recorded"), F.Contacts.Num() == 2))
        {
            TestTrue(TEXT("Second chain origin is first pre-damage endpoint, not moved actor position"),
                     F.Contacts[1].Origin.Equals(FVector(100, 0, 0)));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeChainHitchFalloff,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.ChainHitchAndExplicitFalloffPayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeChainHitchFalloff::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        TArray<AEnemyCharacter*> Victims = {F.Target};
        for (int32 Index = 1; Index < 12; ++Index)
        {
            Victims.Add(F.Other(FVector(100 + Index * 80, 0, 0)));
        }
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain);
        Parameters->Count = 12;
        Parameters->DamageFalloff = .5f;
        FString Error;
        TestTrue(TEXT("Finite long chain starts"), F.Start(*Parameters, bActor, Error));
        F.Advance(0.f);
        F.Advance(.95f);
        const int32 Expected = bActor ? 2 : 9;
        TestEqual(TEXT("Hitch dispatches Actor one or Mixed eight additional contacts"), F.Contacts.Num(), Expected);
        int32 Damaged = 0;
        for (auto* Victim : Victims)
        {
            Damaged += F.Health(Victim) == 90.f ? 1 : 0;
        }
        TestEqual(TEXT("All accepted actions reach the replaceable real fixed-damage payload"), Damaged, Expected);
        for (int32 Index = 0; Index < F.Contacts.Num(); ++Index)
        {
            TestTrue(TEXT("Falloff is explicit typed payload metadata, no mutation of frozen damage"),
                     FMath::IsNearlyEqual(F.Contacts[Index].DamageScale, FMath::Pow(.5f, Index)));
        }
        TestEqual(TEXT("Same logical chain owns one Scope spawn, independent of helper contacts"), F.Binding->Spawned,
                  1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeChainPayloadCancellation,
    "test_25d.UnifiedGA.DynamicMage.TargetNodes.ChainCancellationDuringActualPayloadStopsSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeChainPayloadCancellation::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* Other = F.Other(FVector(150, 0, 0));
        int32 DamageCalls = 0;
        F.OnDamage(*F.Target,
                   [&]()
                   {
                       ++DamageCalls;
                       F.Run->Cancel();
                   });
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Chain);
        FString Error;
        TestTrue(TEXT("Cancellable chain starts"), F.Start(*Parameters, bActor, Error));
        F.Advance(.95f);
        F.Advance(1.f);
        TestEqual(TEXT("Only one real payload can execute before synchronous cancellation"), DamageCalls, 1);
        TestEqual(TEXT("Next candidate is never hit after cancellation"), F.Health(Other), 100.f);
        TestEqual(TEXT("Cancellation never publishes normal expiry"), F.Expired, 0);
        TestTrue(TEXT("Cancelled chain cleans its presentation lease"), F.AllPresentationsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBeamManaAndHitch,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.BeamActualManaAndProfileHitch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeBeamManaAndHitch::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
        Parameters->ManaPerSecond = 10.f;
        FString Error;
        TestTrue(TEXT("Target channel starts"), F.Start(*Parameters, bActor, Error));
        F.Advance(1.05f);
        TestTrue(TEXT("Actor whole-second vs Mixed full-delta real Mana deduction"),
                 FMath::IsNearlyEqual(F.Mana(), bActor ? 90.f : 89.5f));
        TestEqual(TEXT("Beam hitch dispatches one Actor or eight Mixed real damage effects"), F.Health(F.Target),
                  bActor ? 90.f : 20.f);
        TestEqual(TEXT("Each actual pulse has an auxiliary Period after its payload"), F.Periods, bActor ? 1 : 8);
        TestEqual(TEXT("First action precedes its period hook"), F.Order.Num() > 1 ? F.Order[1] : NAME_None,
                  FName(TEXT("Period")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeBeamDeadlineAndLoss,
    "test_25d.UnifiedGA.DynamicMage.TargetNodes.BeamChargesBeforeDeadlineAndTargetLossProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeBeamDeadlineAndLoss::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        {
            FFixture F;
            auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
            Parameters->Duration = .5f;
            Parameters->ManaPerSecond = 10.f;
            FString Error;
            TestTrue(TEXT("Finite beam starts"), F.Start(*Parameters, bActor, Error));
            F.Advance(1.05f);
            TestTrue(TEXT("Full-frame cost precedes cutoff and is not clipped to active lifetime"),
                     FMath::IsNearlyEqual(F.Mana(), bActor ? 90.f : 89.5f));
            TestEqual(TEXT("Expired frame does not apply a final damage pulse"), F.Health(F.Target), 100.f);
            TestEqual(TEXT("Normal deadline expiry executes once"), F.Expired, 1);
            TestTrue(TEXT("Deadline cleanup ends actual presentation"), F.AllPresentationsFinished());
        }
        {
            FFixture F;
            auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
            FString Error;
            TestTrue(TEXT("Beam with an original target starts"), F.Start(*Parameters, bActor, Error));
            F.Target->Destroy();
            F.Advance(.11f);
            TestTrue(TEXT("Lost target ends channel"), F.Run->IsFinished());
            TestEqual(TEXT("Actor loss cancels without normal expiry; Mixed loss expires normally"), F.Expired,
                      bActor ? 0 : 1);
            TestTrue(TEXT("Lost target cleanup ends actual presentation"), F.AllPresentationsFinished());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBeamRangeFrame,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.BeamRangeCheckOncePerFrameBeforeCatchup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeBeamRangeFrame::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    FFixture F;
    int32 Effects = 0;
    F.OnDamage(*F.Target,
               [&]()
               {
                   ++Effects;
                   F.Target->SetActorLocation(FVector(2000, 0, 0));
               });
    auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
    FString Error;
    TestTrue(TEXT("Mixed channel starts"), F.Start(*Parameters, false, Error));
    F.Advance(.85f);
    TestEqual(TEXT("First damage moving target out of range does not add same-frame range checks"), Effects, 8);
    TestEqual(TEXT("Every same-frame live-target pulse applies actual damage"), F.Health(F.Target), 20.f);
    F.Advance(.11f);
    TestEqual(TEXT("Next frame sees target out of range before new pulse"), Effects, 8);
    TestEqual(TEXT("Mixed target loss has one normal expiry"), F.Expired, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBeamLOSProfiles,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.BeamFriendlyCombatantLOSProfiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeBeamLOSProfiles::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* Friendly = F.Other(FVector(50, 0, 30));
        Friendly->Tags = {TEXT("Player")};
        Friendly->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
        FString Error;
        TestTrue(TEXT("Beam with friendly collision blocker starts"), F.Start(*Parameters, bActor, Error));
        F.Advance(.11f);
        TestEqual(TEXT("Actor beam preserves friendly obstruction; Mixed explicitly ignores combatants"),
                  F.Health(F.Target), bActor ? 100.f : 90.f);
        TestEqual(TEXT("Actor obstruction cancels rather than normal expiry"), F.Expired, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeBeamSourceAndFmod,
                                 "test_25d.UnifiedGA.DynamicMage.TargetNodes.BeamSourceCleanupAndActorFmodRemainder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeBeamSourceAndFmod::RunTest(const FString&)
{
    using namespace AbilityPrototypeTargetNodeTests;
    float Clock = 0.f;
    int32 Pulses = 0;
    auto Pulse = [&]()
    {
        ++Pulses;
        return true;
    };
    AbilityPrototypeTargetServices::AdvanceBeamClock(Clock, .36f, .1f, EAbilityPrototypeTargetProfile::Actor, Pulse);
    AbilityPrototypeTargetServices::AdvanceBeamClock(Clock, .05f, .1f, EAbilityPrototypeTargetProfile::Actor, Pulse);
    TestEqual(TEXT("Actor one-per-frame fmod preserves enough remainder for second short frame"), Pulses, 2);
    for (const bool bActor : {true, false})
    {
        FFixture F;
        auto* Parameters = F.Parameters(EAbilityPrototypeKind::Beam);
        FString Error;
        TestTrue(TEXT("Beam starts before source replacement"), F.Start(*Parameters, bActor, Error));
        F.Advance(.11f);
        TestEqual(TEXT("First actual payload confirms live channel"), F.Health(F.Target), 90.f);
        F.ASC->InitAbilityActorInfo(F.Source, F.Target);
        F.Advance(.11f);
        TestEqual(TEXT("Replacement source cannot resume channel payload"), F.Health(F.Target), 90.f);
        TestTrue(TEXT("Graph closes stale source"), F.Run->IsFinished());
        TestEqual(TEXT("Source invalidation has no expiry branch"), F.Expired, 0);
        TestTrue(TEXT("Source invalidation closes exact presentation host"), F.AllPresentationsFinished());
    }
    return true;
}
#endif
