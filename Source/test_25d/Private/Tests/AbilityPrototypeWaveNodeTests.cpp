// 调用链：真实 LegacyGraph/独立波形细图 -> 同一批伤害或 Slow 服务 -> 实际属性与状态。
// 测试手动推进独立临时世界，不激活完整 GA，不把这组证据解释为输入、费用、网络或 PIE 验收。
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilityPrototypeNiagaraTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeWaveNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "MyGameplayTags.h"
#include "EngineUtils.h"
#include "UObject/Script.h"

namespace AbilityPrototypeWaveNodeTests
{
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

    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityGraphInstance* Run = nullptr;
        UPrototypeCastContext* Binding = nullptr;

        FWorld()
        {
            Prepare(*Target);
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        /** 只让角色胶囊参与空间选择；HealthBar 的大 UI 碰撞不是波形测试的作用体。 */
        static void Prepare(AEnemyCharacter& Actor)
        {
            auto* Component = Actor.GetAbilitySystemComponent();
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(&Actor))
            {
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
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

        /** 旧路径与细图共用相同 ASC/GE；显式捕获来源，避免两条空执行路径造成假通过。 */
        bool Start(UAbilityGraphDefinition& Definition, bool bDamage, FString& Error)
        {
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->CastId = FGuid::NewGuid();
            Binding->Level = 1;
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass =
                bDamage ? UArcaneShardTestDamageEffect::StaticClass() : UGameplayEffect::StaticClass();
            if (!Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Binding->GetSourceLife();
            Input.Context = Binding;
            Input.Direction = FVector::ForwardVector;
            Input.DamageParams = Binding->DamageParams;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            AbilityPrototypeWaveNodes::Register(Registry);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition.CompatibilityProfile;
            Options.BudgetPolicy = Definition.BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            Run = NewObject<UAbilityGraphInstance>(World);
            return Run->StartWithInput(&Definition, Input, Options, Registry, Error) &&
                   !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }

        static float Health(AEnemyCharacter& Actor)
        {
            return Actor.GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
    };

    UAbilityPrototype* Parameters(UObject* Outer, bool bBatch6 = false)
    {
        UAbilityPrototype* Result =
            bBatch6 ? NewObject<UBatch6FieldPrototype>(Outer) : NewObject<UAbilityPrototype>(Outer);
        Result->Kind = EAbilityPrototypeKind::Wave;
        Result->Radius = 500.f;
        Result->Speed = 100.f;
        Result->Duration = .8f;
        Result->Damage = 25.f;
        Result->DamagePerLevel = 0.f;
        Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Result->Cooldown = 0.f;
        if (auto* Field = Cast<UBatch6FieldPrototype>(Result))
        {
            Field->bWaveFrontOnly = true;
            Field->SlowMultiplier = 1.f;
        }
        return Result;
    }

    int32 Slow(UAbilityGraphDefinition& Definition)
    {
        auto* Config = NewObject<UAbilitySlowNodeConfig>(&Definition);
        Config->Multiplier = .5f;
        Config->Duration = 3.f;
        return Add(Definition, TEXT("Status.Slow"), Config);
    }

    /** 独立空间适配器从参数帧选目标；可接批伤害，也可完全改接活目标状态。 */
    UAbilityGraphDefinition* Fine(FWorld& F, UAbilityPrototype& Data, bool bActor, bool bSlow)
    {
        auto* Definition = NewObject<UAbilityGraphDefinition>(F.World);
        Definition->SchemaVersion = 2;
        Definition->CompatibilityProfile = bActor ? TEXT("PrototypeFine.Actor.v1") : TEXT("PrototypeFine.Mixed.v1");
        Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
        auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
        Scope->Parameters = &Data;
        Scope->Profile = Definition->CompatibilityProfile;
        const int32 Entry = Add(*Definition, TEXT("Scope.Prototype"), Scope);
        const int32 Wave =
            Add(*Definition, TEXT("Spatial.PrototypeWave"), NewObject<UAbilityPrototypeWaveNodeConfig>(Definition));
        Definition->EntryNodeId = Definition->Nodes[Entry].NodeId;
        Connect(*Definition, Entry, TEXT("Ready"), Wave);
        if (bSlow)
        {
            const int32 State = Slow(*Definition);
            Connect(*Definition, Wave, TEXT("TargetFound"), State);
        }
        else
        {
            auto* Payload = NewObject<UAbilityPrototypeDamageNodeConfig>(Definition);
            Payload->bConsumeInputScales = true;
            const int32 Damage = Add(*Definition, TEXT("Effect.PrototypeDamageBatch"), Payload);
            Connect(*Definition, Wave, TEXT("Contact"), Damage);
        }
        return Definition;
    }
} // namespace AbilityPrototypeWaveNodeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeWaveTailProfiles,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeWave.ActorMixedActualTailAndMassPayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeWaveTailProfiles::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    // Actor 的 float 尾帧、普通 Mixed 的截止前退场和 Batch6 的最终边界是三个不同合同。
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        const bool bActor = Mode == 0;
        const bool bBatch6 = Mode == 2;
        float OriginalHealth = 0.f;
        float OriginalMassHealth = 0.f;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Target->SetActorLocation(bActor ? FVector(80.000005, 0, 0) : FVector(75, 0, 0));
            auto* Config = Parameters(F.World, bBatch6);
            int32 MassId = INDEX_NONE;
            if (!bActor)
            {
                auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
                Sim->Enable();
                auto* Type = NewObject<UPureMassMonsterType>(F.World);
                Type->Health = 200.f;
                Type->XPReward = 0;
                Type->Speed = 0.f;
                MassId = Sim->SpawnMonster(Type, FVector(0, 75, 0));
            }
            FString Error;
            auto* Definition =
                bFine ? Fine(F, *Config, bActor, false)
                      : UAbilityLegacyGraphCompiler::Compile(
                            F.World, Config, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!TestNotNull(TEXT("Actual wave definition"), Definition) ||
                !TestTrue(TEXT("Actual wave starts without immediate Failed"), F.Start(*Definition, true, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Run->Advance(.04f);
            TestEqual(TEXT("Outside first four-centimeter front"), FWorld::Health(*F.Target), 100.f);
            F.Run->Advance(1.f);
            FPureMassView View;
            const float MassHealth =
                !bActor && F.World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) ? View.Health : 0.f;
            if (!bFine)
            {
                OriginalHealth = FWorld::Health(*F.Target);
                OriginalMassHealth = MassHealth;
                TestEqual(TEXT("Original actual Actor GE proves profile tail"), OriginalHealth,
                          Mode == 1 ? 100.f : 90.f);
                if (!bActor)
                {
                    TestEqual(TEXT("Original Mass backend consumes actual prototype damage"), MassHealth,
                              bBatch6 ? 175.f : 200.f);
                }
            }
            else
            {
                TestEqual(TEXT("Fine wave selects same actual Actor at original tail"), FWorld::Health(*F.Target),
                          OriginalHealth);
                if (!bActor)
                {
                    TestEqual(TEXT("Fine wave sends same original Mass life to independent payload"), MassHealth,
                              OriginalMassHealth);
                }
                TestTrue(TEXT("Finite wave eventually closes at exact original deadline"), F.Run->IsFinished());
                TestEqual(TEXT("One spatial fragment consumes one logical scope"), F.Binding->Spawned, 1);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeWaveReplaceableState,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeWave.ReplaceDamageWithStatePreservesCoverage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeWaveReplaceableState::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    for (const bool bActor : {true, false})
    {
        float OriginalSlow = 1.f;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Target->SetActorLocation(FVector(250, 0, 0));
            auto* Early = F.Other(FVector(0, 80, 0));
            auto* Config = Parameters(F.World);
            Config->Duration = 4.f;
            Config->Damage = 0.f;
            FString Error;
            auto* Definition =
                bFine ? Fine(F, *Config, bActor, true)
                      : UAbilityLegacyGraphCompiler::Compile(
                            F.World, Config, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!TestNotNull(TEXT("Replaceable wave definition"), Definition))
            {
                AddError(Error);
                continue;
            }
            if (!bFine)
            {
                const int32 Resolve = Add(*Definition, TEXT("Target.ResolveImpact"),
                                          NewObject<UAbilityResolveImpactNodeConfig>(Definition));
                const int32 State = Slow(*Definition);
                Connect(*Definition, 0, TEXT("OnHit"), Resolve);
                Connect(*Definition, Resolve, TEXT("TargetFound"), State);
            }
            if (!TestTrue(TEXT("State wave starts without immediate Failed"), F.Start(*Definition, false, Error)))
            {
                AddError(Error);
                continue;
            }
            F.Run->Advance(1.f);
            TestEqual(TEXT("Real first target receives independently composed slow"),
                      UBatch6SlowComponent::GetMultiplier(Early), .5f);
            TestEqual(TEXT("Target outside first wave receives no state"),
                      UBatch6SlowComponent::GetMultiplier(F.Target), 1.f);
            F.Target->SetActorLocation(FVector(50, 0, 0));
            F.Run->Advance(.5f);
            const float Landed = UBatch6SlowComponent::GetMultiplier(F.Target);
            if (!bFine)
            {
                OriginalSlow = Landed;
                TestEqual(TEXT("Original shell/growing coverage remains observably different"), Landed,
                          bActor ? 1.f : .5f);
            }
            else
            {
                TestEqual(TEXT("Fine status-only payload keeps original coverage"), Landed, OriginalSlow);
            }
            TestEqual(TEXT("Status replacement causes no damage GE"), FWorld::Health(*F.Target), 100.f);
            F.Run->Cancel();
            TestEqual(TEXT("Cancellation preserves already landed independently owned slow"),
                      UBatch6SlowComponent::GetMultiplier(Early), .5f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeWaveRechecksAfterEarlierGE,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeWave.RechecksLaterTargetAfterActualGE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeWaveRechecksAfterEarlierGE::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    for (const bool bActor : {true, false})
    {
        float OriginalTotalHealth = 0.f;
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Target->SetActorLocation(FVector(40, 0, 0));
            auto* Other = F.Other(FVector(0, 40, 0));
            auto* Config = Parameters(F.World);
            Config->Duration = 3.f;
            FString Error;
            auto* Definition =
                bFine ? Fine(F, *Config, bActor, false)
                      : UAbilityLegacyGraphCompiler::Compile(
                            F.World, Config, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!TestNotNull(TEXT("GE-sensitive spatial definition"), Definition))
            {
                AddError(Error);
                continue;
            }
            int32 ActualDamageCallbacks = 0;
            auto Subscribe = [&](AEnemyCharacter* Victim, AEnemyCharacter* Later)
            {
                return Victim->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                    [&, Victim, Later](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec,
                                       FActiveGameplayEffectHandle)
                    {
                        if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>() && ++ActualDamageCallbacks == 1)
                        {
                            // 这是真实GE的同步观察者，不依赖Actor分配/overlap顺序。后一个仍在最大范围内，
                            // 但已离开本帧100cm波形；旧逐接触判定必须排除它。
                            Later->SetActorLocation(FVector(450, 0, 0));
                            // 自身离开本帧范围仍然是已经命中过的生命，后续波形不得再次命中。
                            Victim->SetActorLocation(FVector(200, 0, 0));
                        }
                    });
            };
            const auto HandleA = Subscribe(F.Target, Other);
            const auto HandleB = Subscribe(Other, F.Target);
            if (TestTrue(TEXT("Actual GE-sensitive wave starts"), F.Start(*Definition, true, Error)))
            {
                F.Run->Advance(1.f);
                const float TotalHealth = FWorld::Health(*F.Target) + FWorld::Health(*Other);
                if (!bFine)
                {
                    OriginalTotalHealth = TotalHealth;
                    TestEqual(TEXT("Original actual GE moves later candidate before its spatial check"),
                              ActualDamageCallbacks, 1);
                    TestEqual(TEXT("Original later candidate remains undamaged beyond current wave"),
                              OriginalTotalHealth, 190.f);
                }
                else
                {
                    TestEqual(TEXT("Fine wave must revalidate later geometry after previous real payload"),
                              ActualDamageCallbacks, 1);
                    TestEqual(TEXT("Fine batch cannot authorize a moved target from the earlier spatial snapshot"),
                              TotalHealth, OriginalTotalHealth);
                }
                // 之前没有准入的目标下一帧进入新波形应能命中；已准入后自行移动的生命不能重复。
                for (auto* Victim : {F.Target, Other})
                {
                    if (FWorld::Health(*Victim) == 100.f)
                    {
                        Victim->SetActorLocation(FVector(150, 0, 0));
                    }
                }
                F.Run->Advance(1.f);
                TestEqual(TEXT("Each original life is admitted once, including a previously excluded later target"),
                          ActualDamageCallbacks, 2);
                TestEqual(TEXT("Moved first life is not charged twice and excluded second life remains eligible"),
                          FWorld::Health(*F.Target) + FWorld::Health(*Other), 180.f);
            }
            else
            {
                AddError(Error);
            }
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(HandleA);
            Other->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(HandleB);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeWaveSameStepState,
    "test_25d.UnifiedGA.DynamicMage.PrototypeWave.SameStepContactKeepsLandedStatePermission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeWaveSameStepState::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    for (const bool bActor : {true, false})
    {
        for (const bool bFine : {false, true})
        {
            FWorld F;
            F.Target->SetActorLocation(FVector(40, 0, 0));
            auto* Config = Parameters(F.World);
            Config->Duration = 3.f;
            FString Error;
            auto* Definition =
                bFine ? Fine(F, *Config, bActor, false)
                      : UAbilityLegacyGraphCompiler::Compile(
                            F.World, Config, bActor ? TEXT("LegacyActor.v1") : TEXT("LegacyMixed.v1"), Error);
            if (!TestNotNull(TEXT("Same-step damage and state definition"), Definition))
            {
                AddError(Error);
                continue;
            }
            const int32 State = Slow(*Definition);
            if (bFine)
            {
                Connect(*Definition, 1, TEXT("TargetFound"), State);
            }
            else
            {
                const int32 Resolve = Add(*Definition, TEXT("Target.ResolveImpact"),
                                          NewObject<UAbilityResolveImpactNodeConfig>(Definition));
                Connect(*Definition, 0, TEXT("OnHit"), Resolve);
                Connect(*Definition, Resolve, TEXT("TargetFound"), State);
            }
            int32 ActualDamageCallbacks = 0;
            const auto Handle = F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        ++ActualDamageCallbacks;
                        F.Target->SetActorLocation(FVector(450, 0, 0));
                    }
                });
            if (TestTrue(TEXT("Same-step actual graph starts"), F.Start(*Definition, true, Error)))
            {
                F.Run->Advance(1.f);
                TestEqual(TEXT("Actual contact damage ran once before independent state"), ActualDamageCallbacks, 1);
                TestEqual(TEXT("GE movement cannot revoke an already admitted same-step live-state permission"),
                          UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
                F.Target->SetActorLocation(FVector(200, 0, 0));
                F.Run->Advance(1.f);
                TestEqual(TEXT("An admitted original life cannot be reused by a later wave step"),
                          ActualDamageCallbacks, 1);
                TestEqual(TEXT("Independent state does not add a second damage action"), FWorld::Health(*F.Target),
                          90.f);
                F.Run->Cancel();
                TestEqual(TEXT("Cancelled spatial owner does not revoke already landed state"),
                          UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
            }
            else
            {
                AddError(Error);
            }
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeWaveReceiptRootOwnership,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeWave.SelectionReceiptCannotEscapeItsRoot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeWaveReceiptRootOwnership::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    FWorld F;
    F.Target->SetActorLocation(FVector(40, 0, 0));
    auto* Config = Parameters(F.World);
    Config->Duration = 3.f;
    FString Error;
    auto* Definition = Fine(F, *Config, true, false);
    if (!TestTrue(TEXT("Original receipt-producing root starts"), F.Start(*Definition, true, Error)))
    {
        AddError(Error);
        return false;
    }
    FAbilityNodeInput Captured;
    bool bCapturedReceipt = false;
    F.Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.NodeType == TEXT("Spatial.PrototypeWave") && Event.OutputPort == TEXT("Contact"))
            {
                Captured = Event.Input;
                bCapturedReceipt = Captured.TargetSet.IsValid() && Captured.TargetSet->SelectionReceipt.IsValid();
            }
        });
    F.Run->Advance(1.f);
    if (!TestTrue(TEXT("Actual production Contact contains its root-bound selection receipt"), bCapturedReceipt))
    {
        return false;
    }
    TestEqual(TEXT("Receipt's original root caused one real GE"), FWorld::Health(*F.Target), 90.f);
    // 清除帧，单独检查selection receipt而非利用既有跨root ParameterFrame拒绝来假通过。
    Captured.ParameterFrame.Reset();
    Captured.Context.Reset();
    auto* OtherDefinition = NewObject<UAbilityGraphDefinition>(F.World);
    OtherDefinition->SchemaVersion = 2;
    OtherDefinition->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Damage = NewObject<UAbilityDamageBatchNodeConfig>(OtherDefinition);
    Damage->Damage = 50.f;
    Damage->DamagePerLevel = 0.f;
    Damage->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Damage->Profile = EAbilityDamageActionProfile::LegacyActor;
    const int32 Entry = Add(*OtherDefinition, TEXT("Effect.DamageBatch"), Damage);
    OtherDefinition->EntryNodeId = OtherDefinition->Nodes[Entry].NodeId;
    auto* OtherRun = NewObject<UAbilityGraphInstance>(F.World);
    const bool bAccepted = OtherRun->StartWithInput(OtherDefinition, Captured, {}, Error);
    TestFalse(TEXT("The same ASC/source cannot lend a prior cast's receipt to a new graph root"), bAccepted);
    TestEqual(TEXT("Borrowed receipt cannot cause another real damage effect"), FWorld::Health(*F.Target), 90.f);
    OtherRun->Cancel();

    // 再通过仍存活的generic调用发一条合法身份/递增序号事件，单独覆盖外部入口的拒绝合同。
    auto* ActiveDefinition = NewObject<UAbilityGraphDefinition>(F.World);
    ActiveDefinition->SchemaVersion = 2;
    ActiveDefinition->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Repeat = NewObject<UAbilityRepeatNodeConfig>(ActiveDefinition);
    Repeat->Count = 3;
    Repeat->Period = .1f;
    Repeat->Duration = 1.f;
    const int32 ActiveEntry = Add(*ActiveDefinition, TEXT("Control.Repeat"), Repeat);
    ActiveDefinition->EntryNodeId = ActiveDefinition->Nodes[ActiveEntry].NodeId;
    auto* ActiveRun = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput Fresh;
    Fresh.SourceToken = F.SourceToken;
    FAbilityGraphEvent LegitimateEvent;
    bool bObservedActivePulse = false;
    ActiveRun->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("Pulse"))
            {
                LegitimateEvent = Event;
                bObservedActivePulse = true;
            }
        });
    if (TestTrue(TEXT("Independent generic root without any borrowed context/frame starts"),
                 ActiveRun->StartWithInput(ActiveDefinition, Fresh, {}, Error)))
    {
        ActiveRun->Advance(.1f);
        TestTrue(TEXT("A real active invocation supplies the external event's valid identity"), bObservedActivePulse);
        TestFalse(TEXT("Generic root is still active before the borrowed event"), ActiveRun->IsFinished());
        if (bObservedActivePulse)
        {
            auto BorrowedEvent = LegitimateEvent;
            ++BorrowedEvent.EventSequence;
            BorrowedEvent.Input.TargetSet = Captured.TargetSet;
            TestFalse(TEXT("External event rejects another root's receipt even with the same valid source"),
                      ActiveRun->EnqueueEvent(BorrowedEvent));
        }
    }
    else
    {
        AddError(Error);
    }
    ActiveRun->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeActorWaveContactNiagara,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.ActorWavePerTargetActualNiagara",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FPrototypeActorWaveContactNiagara::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    auto* Asset = AbilityPrototypeNiagaraTestFixture::System();
    TArray<FTransform> Original;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        if (!AbilityPrototypeNiagaraTestFixture::Probe(*this, *F.World, Asset))
        {
            return false;
        }
        F.Target->SetActorLocation(FVector(40, 0, 0));
        auto* Other = F.Other(FVector(0, 60, 0));
        auto* Config = Parameters(F.World);
        Config->Duration = 3.f;
        Config->LoopVFX = nullptr;
        Config->ImpactVFX = Asset;
        Config->ImpactVFXScale = FVector(1.7f, 2.1f, .9f);
        FString Error;
        auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Config, TEXT("LegacyActor.v1"), Error)
                                 : UAbilityLegacyGraphCompiler::Compile(F.World, Config, TEXT("LegacyActor.v1"), Error);
        if (!TestNotNull(TEXT("Actual default Actor Wave presentation graph"), Definition))
        {
            AddError(Error);
            return false;
        }
        int32 ActualGECount = 0;
        auto Bind = [&](AEnemyCharacter* Victim)
        {
            return Victim->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&, Victim](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        ++ActualGECount;
                        Victim->SetActorLocation(Victim->GetActorLocation() + FVector(300, 0, 0));
                    }
                });
        };
        const auto A = Bind(F.Target);
        const auto B = Bind(Other);
        if (!TestTrue(TEXT("Actual Actor Wave starts before its first spatial contact"),
                      F.Start(*Definition, true, Error)))
        {
            AddError(Error);
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(A);
            Other->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(B);
            return false;
        }
        for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        {
            if (!It->HasActorBegunPlay())
            {
                It->DispatchBeginPlay();
            }
        }
        TestEqual(TEXT("No loop resource or premature impact pollutes contact counting"),
                  AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset).Num(), 0);
        F.Run->Advance(1.f);
        TestEqual(TEXT("Both actual target GE contacts occur"), ActualGECount, 2);
        TestEqual(TEXT("First target has exactly one actual damage GE"), FWorld::Health(*F.Target), 90.f);
        TestEqual(TEXT("Second target has exactly one actual damage GE"), FWorld::Health(*Other), 90.f);
        const auto Actual = AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset);
        if (!bFine)
        {
            Original = Actual;
            TestEqual(TEXT("Original native Actor Wave creates one real Niagara per target contact"), Original.Num(),
                      2);
            TestTrue(
                TEXT("Original first actual Niagara uses target position after its synchronous GE"),
                AbilityPrototypeNiagaraTestFixture::Contains(Original, FVector(340, 0, 0), Config->ImpactVFXScale));
            TestTrue(
                TEXT("Original second actual Niagara uses its own post-GE target and authored scale"),
                AbilityPrototypeNiagaraTestFixture::Contains(Original, FVector(300, 60, 0), Config->ImpactVFXScale));
        }
        else
        {
            TestEqual(TEXT("Fine default Actor Wave preserves real per-target native impact count"), Actual.Num(),
                      Original.Num());
            for (const auto& Expected : Original)
            {
                TestTrue(TEXT("Fine actual component preserves each original contact position and authored scale"),
                         AbilityPrototypeNiagaraTestFixture::Contains(Actual, Expected.GetLocation(),
                                                                      Expected.GetScale3D()));
            }
        }
        F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(A);
        Other->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(B);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeActorPulseBeforeLogicalChild,
                                 "test_25d.UnifiedGA.DynamicMage.Presentation.ActorPulseBeforeActualOnHitChildGE",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FPrototypeActorPulseBeforeLogicalChild::RunTest(const FString&)
{
    using namespace AbilityPrototypeWaveNodeTests;
    auto* Asset = AbilityPrototypeNiagaraTestFixture::System();
    int32 OriginalParticlesAtChildGE = INDEX_NONE;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        if (!AbilityPrototypeNiagaraTestFixture::Probe(*this, *F.World, Asset))
        {
            return false;
        }
        F.Target->SetActorLocation(FVector(100, 0, 0));
        auto* Config = Parameters(F.World);
        Config->Kind = EAbilityPrototypeKind::Field;
        Config->Radius = 150.f;
        Config->Period = .25f;
        Config->Duration = 2.f;
        Config->LoopVFX = nullptr;
        Config->ImpactVFX = Asset;
        Config->ImpactVFXScale = FVector(1.2f, 1.4f, .8f);
        Config->OnHit = NewObject<UAbilityPrototype>(Config);
        Config->OnHit->Kind = EAbilityPrototypeKind::Burst;
        Config->OnHit->Radius = 150.f;
        Config->OnHit->Damage = 3.f;
        Config->OnHit->DamagePerLevel = 0.f;
        Config->OnHit->DamageType = Config->DamageType;
        Config->OnHit->Cooldown = 0.f;
        FString Error;
        auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Config, TEXT("LegacyActor.v1"), Error)
                                 : UAbilityLegacyGraphCompiler::Compile(F.World, Config, TEXT("LegacyActor.v1"), Error);
        if (!TestNotNull(TEXT("Actual field with independent logical hit child compiles"), Definition))
        {
            AddError(Error);
            return false;
        }
        int32 ActualGEs = 0;
        int32 ParticlesAtChildGE = INDEX_NONE;
        const auto Handle = F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                if (Spec.Def && Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                {
                    ++ActualGEs;
                    if (Spec.GetSetByCallerMagnitude(Config->DamageType, false) == 3.f)
                    {
                        ParticlesAtChildGE = AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset).Num();
                    }
                }
            });
        if (!TestTrue(TEXT("Actual field and logical child graph starts"), F.Start(*Definition, true, Error)))
        {
            AddError(Error);
            F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
            return false;
        }
        for (TActorIterator<APrototypeRuntime> It(F.World); It; ++It)
        {
            if (!It->HasActorBegunPlay())
            {
                It->DispatchBeginPlay();
            }
        }
        F.Run->Advance(.25f);
        const auto Actual = AbilityPrototypeNiagaraTestFixture::Impacts(*F.World, Asset);
        TestEqual(TEXT("Both root and independent child apply actual GE"), ActualGEs, 2);
        TestEqual(TEXT("No duplicate pulse damage is hidden in presentation"), FWorld::Health(*F.Target), 80.f);
        TestEqual(TEXT("One real center cue is created for the one actual field pulse"), Actual.Num(), 1);
        TestTrue(TEXT("The actual center cue uses the exact host center and authored scale"),
                 AbilityPrototypeNiagaraTestFixture::Contains(Actual, FVector::ZeroVector, Config->ImpactVFXScale));
        if (!bFine)
        {
            OriginalParticlesAtChildGE = ParticlesAtChildGE;
            TestEqual(TEXT("Original center cue already exists before the logical child damage GE"),
                      OriginalParticlesAtChildGE, 1);
        }
        else
        {
            TestEqual(TEXT("Fine center presentation must settle before independent OnHit child GE"),
                      ParticlesAtChildGE, OriginalParticlesAtChildGE);
        }
        F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
    }
    return true;
}
#endif
