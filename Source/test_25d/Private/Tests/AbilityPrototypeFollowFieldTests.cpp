// 调用链：真实LegacyMixed开关Field/独立细图 -> GE维持费用 -> 随动Query/批伤害/Slow -> 取消状态清理。
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityArcaneShardTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFollowFieldNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeContactNodes.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityPrototypeFollowFieldTests
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
        APureMassRuntime* RootHost = nullptr;
        int32 MassId = INDEX_NONE;

        FWorld()
        {
            Target->SetActorLocation(FVector(100, 0, 0));
            auto* Component = Target->GetAbilitySystemComponent();
            Component->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
            Component->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            for (auto* Primitive : TInlineComponentArray<UPrimitiveComponent*>(Target))
            {
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->XPReward = 0;
            Type->Speed = 0.f;
            MassId = Sim->SpawnMonster(Type, FVector(0, 100, 0));
        }

        ~FWorld()
        {
            if (Run)
            {
                Run->Cancel();
            }
        }

        float Health() const
        {
            return Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        }
        float Mana() const
        {
            return ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
        }
        float MassHealth() const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(MassId, View) ? View.Health : -1.f;
        }

        UBatch6FieldPrototype* Parameters()
        {
            auto* Config = NewObject<UBatch6FieldPrototype>(World);
            Config->Kind = EAbilityPrototypeKind::Field;
            Config->bFollowCasterToggle = true;
            Config->Radius = 150.f;
            Config->Period = .25f;
            Config->Duration = .5f;
            Config->ManaPerSecond = 2.f;
            Config->Damage = 25.f;
            Config->DamagePerLevel = 0.f;
            Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Config->Cooldown = 0.f;
            Config->SlowMultiplier = .5f;
            Config->SlowDuration = 3.f;
            // 取消错误转为到期时，真正的Burst会额外扣血并增加Context逻辑计数。
            Config->OnExpire = NewObject<UAbilityPrototype>(Config);
            Config->OnExpire->Kind = EAbilityPrototypeKind::Burst;
            Config->OnExpire->Radius = 150.f;
            Config->OnExpire->DamageType = Config->DamageType;
            Config->OnExpire->Damage = 7.f;
            Config->OnExpire->DamagePerLevel = 0.f;
            Config->OnExpire->Cooldown = 0.f;
            return Config;
        }

        /** 手工组合空间/效果/状态，随动节点没有隐藏内建伤害。 */
        UAbilityGraphDefinition* Fine(UBatch6FieldPrototype& Config)
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("PrototypeFine.Mixed.v1");
            Definition->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
            auto* Scope = NewObject<UAbilityPrototypeScopeNodeConfig>(Definition);
            Scope->Parameters = &Config;
            Scope->Profile = Definition->CompatibilityProfile;
            Scope->LogicalRejectionPolicy = EAbilityPrototypeLogicalRejectionPolicy::FailExecution;
            const int32 Entry = Add(*Definition, TEXT("Scope.Prototype"), Scope);
            const int32 Follow = Add(*Definition, TEXT("Control.PrototypeFollowField"),
                                     NewObject<UAbilityPrototypeFollowFieldNodeConfig>(Definition));
            const int32 Query =
                Add(*Definition, TEXT("Query.PrototypeShape"), NewObject<UAbilityPrototypeQueryNodeConfig>(Definition));
            const int32 Damage = Add(*Definition, TEXT("Effect.PrototypeDamageBatch"),
                                     NewObject<UAbilityPrototypeDamageNodeConfig>(Definition));
            const int32 Slow =
                Add(*Definition, TEXT("Status.PrototypeSlow"), NewObject<UAbilityPrototypeSlowNodeConfig>(Definition));
            Definition->EntryNodeId = Definition->Nodes[Entry].NodeId;
            Connect(*Definition, Entry, TEXT("Ready"), Follow);
            Connect(*Definition, Follow, TEXT("Action"), Query);
            Connect(*Definition, Query, TEXT("Completed"), Damage);
            Connect(*Definition, Damage, TEXT("Contact"), Slow);
            return Definition;
        }

        bool Start(bool bFine, UBatch6FieldPrototype& Config, FString& Error)
        {
            Binding = NewObject<UPrototypeCastContext>(World);
            Binding->Caster = Source;
            Binding->CastId = FGuid::NewGuid();
            Binding->Level = 1;
            Binding->DamageParams.SourceAbilitySystemComponent = ASC;
            Binding->DamageParams.DamageGameplayEffectClass = UArcaneShardTestDamageEffect::StaticClass();
            if (!Binding->CaptureSourceLife(Error))
            {
                return false;
            }
            auto* Definition =
                bFine ? Fine(Config)
                      : UAbilityLegacyGraphCompiler::Compile(World, &Config, TEXT("LegacyMixed.v1"), Error);
            if (!Definition)
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Binding->GetSourceLife();
            Input.Context = Binding;
            Input.Direction = FVector::ForwardVector;
            Input.DamageParams = Binding->DamageParams;
            FAbilityExecutionOptions Options;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
            FAbilityNodeRegistry Registry;
            Registry.RegisterBuiltins();
            AbilityPrototypeFollowFieldNodes::Register(Registry);
            Run = NewObject<UAbilityGraphInstance>(World);
            const bool bAccepted = Run->StartWithInput(Definition, Input, Options, Registry, Error);
            for (TActorIterator<APureMassRuntime> It(World); It; ++It)
            {
                RootHost = *It;
                break;
            }
            return bAccepted && !(Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        }
    };
} // namespace AbilityPrototypeFollowFieldTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypeFollowFieldMovesAndPays,
    "test_25d.UnifiedGA.DynamicMage.PrototypeFollowField.RealFollowManaAndDurationOverride",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFollowFieldMovesAndPays::RunTest(const FString&)
{
    using namespace AbilityPrototypeFollowFieldTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        auto* Config = F.Parameters();
        FString Error;
        if (!TestTrue(TEXT("Real toggle field starts without immediate failure"), F.Start(bFine, *Config, Error)))
        {
            AddError(Error);
            continue;
        }
        F.Run->Advance(.25f);
        TestEqual(TEXT("First actual mixed field pulse applies Actor GE"), F.Health(), 90.f);
        TestEqual(TEXT("First actual mixed field pulse applies Mass damage"), F.MassHealth(), 175.f);
        TestEqual(TEXT("First frame pays full delta upkeep through real Mana GE"), F.Mana(), 99.5f);
        F.Source->SetActorLocation(FVector(1000, 0, 0));
        F.Target->SetActorLocation(FVector(1050, 0, 0));
        F.Run->Advance(.25f);
        TestEqual(TEXT("Spatial query follows caster instead of original world anchor"), F.Health(), 80.f);
        TestEqual(TEXT("Previous world area stops hitting Mass target after follow"), F.MassHealth(), 175.f);
        TestFalse(TEXT("Root follow-toggle remains live at nominal finite Duration"), F.Run->IsFinished());
        F.Run->Advance(1.25f);
        TestEqual(TEXT("Five more actual pulses run beyond Duration"), F.Health(), 30.f);
        TestEqual(TEXT("Follow field pays complete elapsed delta rather than clipped duration"), F.Mana(), 96.5f);
        TestEqual(TEXT("Repeated helper pulses do not reserve extra logical fragments"), F.Binding->Spawned, 1);
        TestEqual(TEXT("Original independently owned slow is present"), UBatch6SlowComponent::GetMultiplier(F.Target),
                  .5f);
        if (TestNotNull(TEXT("Actual field owns a presentation host"), F.RootHost))
        {
            TestTrue(TEXT("Presentation host follows the same caster anchor as actual payload"),
                     F.RootHost->GetActorLocation().Equals(F.Source->GetActorLocation()));
        }
        F.Run->Cancel();
        TestEqual(TEXT("Cancel never converts toggle to OnExpire damage"), F.Health(), 30.f);
        TestEqual(TEXT("Cancel preserves already landed Slow under target service ownership"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
        TestEqual(TEXT("Cancelled field releases every active graph invocation"), F.Run->GetActiveInvocationCount(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFollowFieldManaCancellation,
                                 "test_25d.UnifiedGA.DynamicMage.PrototypeFollowField.ManaFailureBeforePulseKeepsState",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypeFollowFieldManaCancellation::RunTest(const FString&)
{
    using namespace AbilityPrototypeFollowFieldTests;
    for (const bool bFine : {false, true})
    {
        FWorld F;
        F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), .5f);
        auto* Config = F.Parameters();
        FString Error;
        if (!TestTrue(TEXT("Low-mana toggle starts with captured original source"), F.Start(bFine, *Config, Error)))
        {
            AddError(Error);
            continue;
        }
        F.Run->Advance(.25f);
        TestEqual(TEXT("Affordable frame pays Mana before its real damage"), F.Mana(), 0.f);
        TestEqual(TEXT("Affordable frame applies exactly one real damage GE"), F.Health(), 90.f);
        TestEqual(TEXT("Affordable frame lands target-owned slow"), UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
        F.Run->Advance(.25f);
        TestTrue(TEXT("Next unaffordable full-frame upkeep closes the graph"), F.Run->IsFinished());
        TestEqual(TEXT("Upkeep rejection is cancellation, not normal expiry"), F.Run->GetEndReason(),
                  EAbilityGraphEndReason::Cancelled);
        TestEqual(TEXT("No post-cost pulse or cancellation-expiry damage occurs"), F.Health(), 90.f);
        TestEqual(TEXT("No expiry child is admitted by a failed upkeep"), F.Binding->Spawned, 1);
        TestEqual(TEXT("Failed upkeep does not refund Mana"), F.Mana(), 0.f);
        TestEqual(TEXT("Graph lifetime does not retract prior target-owned state"),
                  UBatch6SlowComponent::GetMultiplier(F.Target), .5f);
    }
    return true;
}
#endif
