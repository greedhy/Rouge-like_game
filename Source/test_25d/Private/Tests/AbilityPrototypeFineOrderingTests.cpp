// 调用链：Automation -> 原 LegacyGraph/新细节点定义 -> 真实 GE 回调与 Slow 服务 -> 接触/取消后的状态。
// 本组只比较实际动作顺序，不直接激活 GA，因此不作为费用、输入、动画或联机验收证据。
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "Components/CapsuleComponent.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeFineOrderingTests
{
    /** 同一配置可给多个调用独立状态；边只连接真实 typed port。 */
    int32 Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = Type;
        Node.Config = Config;
        return Definition.Nodes.Add(Node);
    }

    /** 对既有定义追加共享效果，旧 Driver 仍承担原来的选择、时钟和接触。 */
    void Connect(UAbilityGraphDefinition& Definition, int32 From, FName Port, int32 To)
    {
        FAbilityGraphEdge Edge;
        Edge.From = Definition.Nodes[From].NodeId;
        Edge.OutputPort = Port;
        Edge.To = Definition.Nodes[To].NodeId;
        Definition.Edges.Add(Edge);
    }

    /** 提供确定性的实际 Actor/ASC，显式开启动态查询碰撞，跳过 AI/蓝图 BeginPlay。 */
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphInstance* Run = nullptr;

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

        /** Health 用真实 AttributeSet；碰撞仅用于旧 Actor overlap，LOS 明确忽略 Pawn。 */
        static void Prepare(AEnemyCharacter& Actor)
        {
            auto* ASC = Actor.GetAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            Actor.GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Actor.GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
            Actor.GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
            Actor.GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        }

        /** 新增另一个独立原生命，不使用同地址复用来伪造第二个命中。 */
        AEnemyCharacter* Other()
        {
            auto* Actor = World->SpawnActor<AEnemyCharacter>();
            Actor->Tags = {TEXT("Enemy")};
            Actor->SetActorLocation(FVector(220, 0, 0));
            auto* TargetASC = Actor->GetAbilitySystemComponent();
            TargetASC->InitAbilityActorInfo(Actor, Actor);
            TargetASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
            Prepare(*Actor);
            return Actor;
        }

        /** 旧 Context 与细图输入使用相同源证明，测试 GE 仅在实际应用时改变 Health。 */
        UPrototypeCastContext* Context(bool bDamage)
        {
            auto* Result = NewObject<UPrototypeCastContext>(Source);
            Result->Caster = Source;
            Result->CastId = FGuid::NewGuid();
            Result->Level = 1;
            Result->DamageParams.SourceAbilitySystemComponent = ASC;
            Result->DamageParams.DamageGameplayEffectClass =
                bDamage ? UArcaneShardTestDamageEffect::StaticClass() : UGameplayEffect::StaticClass();
            return Result;
        }

        /** 启动生产图实例；由本用例显式推进一次，不同时调用 World subsystem Tick。 */
        bool Start(UAbilityGraphDefinition* Definition, UPrototypeCastContext* Binding, FString& Error)
        {
            if (!Definition || !Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Binding->GetSourceLife();
            Input.Context = Binding;
            Input.DamageParams = Binding->DamageParams;
            Input.Direction = FVector::ForwardVector;
            Input.EventCenter = Input.Origin;
            Input.Level = 1.f;
            Run = NewObject<UAbilityGraphInstance>(World);
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            return Run->StartWithInput(Definition, Input, Options, Error);
        }
    };

    /** 基础参数全部合法，零 VFX 不依赖 Content；不以改时长避开真实最后片段。 */
    UAbilityPrototype* Wave(UObject* Outer)
    {
        auto* Result = NewObject<UAbilityPrototype>(Outer);
        Result->Kind = EAbilityPrototypeKind::Wave;
        Result->Radius = 500.f;
        Result->Speed = 100.f;
        Result->Duration = .8f;
        Result->Damage = Result->DamagePerLevel = 0.f;
        Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Result->Cooldown = 0.f;
        return Result;
    }

    /** 波前选择后用真正的共享 Slow 服务落地，读组件倍率而非比较内部浮点时钟。 */
    int32 AddSlow(UAbilityGraphDefinition& Definition)
    {
        auto* Slow = NewObject<UAbilitySlowNodeConfig>(&Definition);
        Slow->Multiplier = .5f;
        Slow->Duration = 2.f;
        return Add(Definition, TEXT("Status.Slow"), Slow);
    }

    struct FObservation
    {
        float FirstSlow = 1.f;
        float LaterSlow = 1.f;
        float HealthA = 100.f;
        float HealthB = 100.f;
        int32 DamageCallbacks = 0;
        EAbilityGraphEndReason End = EAbilityGraphEndReason::Failed;
        bool bStarted = false;
    };

    /**
     * 第二次真实伤害回调取消同一根；忽略 Slow marker GE，避免把状态标记误计为伤害。
     * 以实际回调先后确定 First/Later，避免依赖 UObject 分配索引产生的 Actor 迭代顺序。
     */
    FObservation ObserveCancellation(FAutomationTestBase& Test, EAbilityPrototypeKind Kind, bool bFine)
    {
        FWorld F;
        auto* Other = F.Other();
        auto* Config = NewObject<UBatch6FieldPrototype>(F.World);
        Config->Kind = Kind;
        Config->Radius = 350.f;
        Config->Period = .1f;
        Config->ArmDelay = .1f;
        Config->Duration = 2.f;
        Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Config->Damage = 25.f;
        Config->DamagePerLevel = 0.f;
        Config->Cooldown = 0.f;
        Config->SlowMultiplier = .5f;
        Config->SlowDuration = 2.f;
        // 取消若被错误当成正常到期，附加 Burst 的真实 GE 会让 Health 小于90。
        Config->OnExpire = NewObject<UAbilityPrototype>(Config);
        Config->OnExpire->Kind = EAbilityPrototypeKind::Burst;
        Config->OnExpire->DamageType = Config->DamageType;
        Config->OnExpire->Cooldown = 0.f;
        Config->OnExpire->Radius = Config->Radius;
        auto* Binding = F.Context(true);
        FString Error;
        auto* Definition = bFine ? AbilityPrototypeFineCompiler::Compile(F.World, Config, TEXT("LegacyMixed.v1"), Error)
                                 : UAbilityLegacyGraphCompiler::Compile(F.World, Config, TEXT("LegacyMixed.v1"), Error);
        FObservation Result;
        if (!Test.TestNotNull(FString(bFine ? TEXT("Fine default definition: ") : TEXT("Legacy definition: ")) + Error,
                              Definition))
        {
            return Result;
        }
        if (bFine)
        {
            Test.TestFalse(TEXT("Fine default cannot hide complete old Driver behind a macro node"),
                           Definition->Nodes.ContainsByPredicate(
                               [](const FAbilityGraphNode& Node)
                               {
                                   return Node.TypeId.ToString().StartsWith(TEXT("Legacy."));
                               }));
        }
        AEnemyCharacter* First = nullptr;
        AEnemyCharacter* Later = nullptr;
        int32 DamageCallbacks = 0;
        auto Bind = [&](AEnemyCharacter* Victim)
        {
            return Victim->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [&, Victim](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
                {
                    if (!Spec.Def || !Spec.Def->IsA<UArcaneShardTestDamageEffect>())
                    {
                        return;
                    }
                    if (++DamageCallbacks == 1)
                    {
                        First = Victim;
                    }
                    else if (DamageCallbacks == 2)
                    {
                        Later = Victim;
                        Binding->bCancelled = true;
                        if (F.Run)
                        {
                            F.Run->Cancel();
                        }
                    }
                });
        };
        const auto A = Bind(F.Target);
        const auto B = Bind(Other);
        Result.bStarted = F.Start(Definition, Binding, Error);
        Test.TestTrue(FString(bFine ? TEXT("Fine cast starts: ") : TEXT("Legacy cast starts: ")) + Error,
                      Result.bStarted);
        if (F.Run)
        {
            F.Run->Advance(.1f);
            Result.End = F.Run->GetEndReason();
        }
        Result.DamageCallbacks = DamageCallbacks;
        Result.FirstSlow = UBatch6SlowComponent::GetMultiplier(First);
        Result.LaterSlow = UBatch6SlowComponent::GetMultiplier(Later);
        Result.HealthA =
            F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        Result.HealthB = Other->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(A);
        Other->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(B);
        return Result;
    }

    /** baseline 必须实际打到两个目标并留下第一个状态，避免两条失效路径都为空而假通过。 */
    bool CheckCancellation(FAutomationTestBase& Test, EAbilityPrototypeKind Kind)
    {
        const auto Legacy = ObserveCancellation(Test, Kind, false);
        Test.TestEqual(TEXT("Legacy reaches two real root damage actions"), Legacy.DamageCallbacks, 2);
        Test.TestEqual(TEXT("Legacy first target already has landed slow before later cancellation"), Legacy.FirstSlow,
                       .5f);
        Test.TestEqual(TEXT("Legacy later target receives no slow after cancellation"), Legacy.LaterSlow, 1.f);
        Test.TestEqual(TEXT("Legacy cancellation does not execute expiry damage A"), Legacy.HealthA, 90.f);
        Test.TestEqual(TEXT("Legacy cancellation does not execute expiry damage B"), Legacy.HealthB, 90.f);
        const auto Fine = ObserveCancellation(Test, Kind, true);
        Test.TestTrue(TEXT("Fine compiler supplies an executable independently composed default"), Fine.bStarted);
        Test.TestEqual(TEXT("Fine reaches same two real damage actions"), Fine.DamageCallbacks, Legacy.DamageCallbacks);
        Test.TestEqual(TEXT("Fine preserves first target's already-landed synchronous state"), Fine.FirstSlow,
                       Legacy.FirstSlow);
        Test.TestEqual(TEXT("Fine does not apply state to the cancelled later target"), Fine.LaterSlow,
                       Legacy.LaterSlow);
        Test.TestEqual(TEXT("Fine cancellation never adds normal expiry damage A"), Fine.HealthA, Legacy.HealthA);
        Test.TestEqual(TEXT("Fine cancellation never adds normal expiry damage B"), Fine.HealthB, Legacy.HealthB);
        Test.TestEqual(TEXT("Fine preserves cancel rather than reporting normal expiry"), Fine.End,
                       EAbilityGraphEndReason::Cancelled);
        return true;
    }
} // namespace AbilityPrototypeFineOrderingTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineWaveFinalContactTest,
                                 "test_25d.UnifiedGA.DynamicMage.FineOrdering.ActorWaveFinalContactSlow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Age+=Delta 后再减回Delta的 float 顺序会决定真实目标是否接触，不能仅统一为 Duration-AgeBefore。 */
bool FFineWaveFinalContactTest::RunTest(const FString&)
{
    using namespace AbilityPrototypeFineOrderingTests;
    float LegacySlow = 1.f;
    float FineSlow = 1.f;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.Target->SetActorLocation(FVector(80.000005, 0, 0));
        auto* Binding = F.Context(false);
        FString Error;
        UAbilityGraphDefinition* Definition = nullptr;
        if (!bFine)
        {
            Definition = UAbilityLegacyGraphCompiler::Compile(F.World, Wave(F.World), TEXT("LegacyActor.v1"), Error);
            if (!TestNotNull(TEXT("Original Actor wave definition loads"), Definition))
            {
                return false;
            }
            const int32 Resolve =
                Add(*Definition, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(Definition));
            const int32 Slow = AddSlow(*Definition);
            Connect(*Definition, 0, TEXT("OnHit"), Resolve);
            Connect(*Definition, Resolve, TEXT("TargetFound"), Slow);
        }
        else
        {
            Definition = NewObject<UAbilityGraphDefinition>(F.World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("UnifiedV2");
            auto* Config = NewObject<UAbilityWavefrontNodeConfig>(Definition);
            Config->Radius = 500.f;
            Config->Speed = 100.f;
            Config->Duration = .8f;
            Config->Profile = EAbilityWaveQueryProfile::ActorFront3D;
            Config->bIncludeMass = false;
            Add(*Definition, TEXT("Spatial.Wavefront"), Config);
            Definition->EntryNodeId = Definition->Nodes[0].NodeId;
            const int32 Slow = AddSlow(*Definition);
            Connect(*Definition, 0, TEXT("TargetFound"), Slow);
        }
        if (!TestTrue(TEXT("Real wave comparison starts"), F.Start(Definition, Binding, Error)))
        {
            AddError(Error);
            return false;
        }
        F.Run->Advance(.04f);
        TestEqual(TEXT("Target is outside the first four-centimeter wavefront"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), 1.f);
        F.Run->Advance(1.f);
        (bFine ? FineSlow : LegacySlow) = UBatch6SlowComponent::GetMultiplier(F.Target);
    }
    TestEqual(TEXT("Original expression genuinely reaches the boundary target and applies Slow"), LegacySlow, .5f);
    TestEqual(TEXT("Fine Actor profile must reach the same actual target at final partial frame"), FineSlow,
              LegacySlow);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineFieldCancellationOrderTest,
                                 "test_25d.UnifiedGA.DynamicMage.FineOrdering.FieldLandedStateBeforeCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Field 的内建每目标 Damage→Slow 不是异步 OnHit 子调用，批化不能丢失已经落地的状态。 */
bool FFineFieldCancellationOrderTest::RunTest(const FString&)
{
    return AbilityPrototypeFineOrderingTests::CheckCancellation(*this, EAbilityPrototypeKind::Field);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFineTrapCancellationOrderTest,
                                 "test_25d.UnifiedGA.DynamicMage.FineOrdering.TrapLandedStateBeforeCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 同一共享同步 payload 合同也作用于 Trap 的实际范围 Pulse，取消不变为正常到期爆炸。 */
bool FFineTrapCancellationOrderTest::RunTest(const FString&)
{
    return AbilityPrototypeFineOrderingTests::CheckCancellation(*this, EAbilityPrototypeKind::Trap);
}

#endif
