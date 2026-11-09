// 调用链：Automation -> 同一UnifiedGraphAbility -> Self/Ground目标策略 -> Query/Damage或旧节点图 -> 原Mass服务。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"

namespace
{
    struct FAuthoringFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphTestAbility* Ability = nullptr;
        FGameplayAbilitySpecHandle Handle;
        UPureMassSubsystem* Mass = nullptr;
        int32 TargetId = INDEX_NONE;
        FAuthoringFixture(FVector TargetPosition, bool bToggle = false)
        {
            Target->Destroy();
            ASC->AbilityActorInfo->PlayerController = World->SpawnActor<AAbilityGraphRemoteTestController>();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            TargetId = Mass->SpawnMonster(Type, TargetPosition);
            FGameplayAbilitySpec Spec(
                bToggle ? UAbilityGraphToggleTestAbility::StaticClass() : UAbilityGraphTestAbility::StaticClass(), 3);
            if (bToggle)
            {
                Spec.DynamicAbilityTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.LMB")));
                Spec.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            }
            Handle = ASC->GiveAbility(Spec);
            Ability =
                CastChecked<UAbilityGraphTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        }
        ~FAuthoringFixture()
        {
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
        }
        void Activate()
        {
            FPredictionKey Key;
            Key.Current = 121;
            ASC->CallServerTryActivateAbility(Handle, false, Key);
        }
        void SendGround(FVector Point, int32 Item = INDEX_NONE)
        {
            auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.ImpactPoint = Point;
            Hit->HitResult.Item = Item;
            FGameplayAbilityTargetDataHandle Data;
            Data.Add(Hit);
            ASC->ServerSetReplicatedTargetData(Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                               Data, FGameplayTag(), FPredictionKey());
        }
        float Health() const
        {
            FPureMassView View;
            return Mass->Resolve(TargetId, View) ? View.Health : -1.f;
        }
        UAbilityGraphDefinition* QueryDamage()
        {
            auto* Result = NewObject<UAbilityGraphDefinition>(World);
            Result->SchemaVersion = 2;
            Result->CompatibilityProfile = TEXT("UnifiedV2");
            FAbilityGraphNode Query;
            Query.NodeId = FGuid::NewGuid();
            Query.TypeId = TEXT("Query.Shape");
            auto* QueryConfig = NewObject<UAbilityQueryNodeConfig>(Result);
            QueryConfig->Query.Radius = 200.f;
            Query.Config = QueryConfig;
            FAbilityGraphNode Damage;
            Damage.NodeId = FGuid::NewGuid();
            Damage.TypeId = TEXT("Effect.Damage");
            auto* DamageConfig = NewObject<UAbilityDamageNodeConfig>(Result);
            DamageConfig->Damage = 25.f;
            DamageConfig->DamagePerLevel = 5.f;
            DamageConfig->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Damage.Config = DamageConfig;
            Result->Nodes = {Query, Damage};
            Result->EntryNodeId = Query.NodeId;
            FAbilityGraphEdge Edge;
            Edge.From = Query.NodeId;
            Edge.To = Damage.NodeId;
            Edge.OutputPort = TEXT("TargetFound");
            Result->Edges.Add(Edge);
            return Result;
        }
        UAbilityGraphDefinition* LegacyDelay(FString& Error)
        {
            auto* Delay = NewObject<UAbilityPrototype>(World);
            Delay->Kind = EAbilityPrototypeKind::Delay;
            Delay->ArmDelay = .2f;
            Delay->Duration = 1.f;
            Delay->Cooldown = 0.f;
            Delay->DamageType = FMyGameplayTags::Get().Damage_Magic;
            auto* Burst = NewObject<UAbilityPrototype>(Delay);
            Burst->Kind = EAbilityPrototypeKind::Burst;
            Burst->Damage = 20.f;
            Burst->DamagePerLevel = 0.f;
            Burst->Radius = 200.f;
            Burst->Duration = .3f;
            Burst->Cooldown = 0.f;
            Burst->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Delay->OnExpire = Burst;
            return UAbilityLegacyGraphCompiler::Compile(World, Delay, TEXT("LegacyMixed.v1"), Error);
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphTargetModes, "test_25d.UnifiedGA.Authoring.SelfAndGroundDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedGraphTargetModes::RunTest(const FString&)
{
    for (const auto Mode : {EUnifiedAbilityTargetMode::Self, EUnifiedAbilityTargetMode::Ground})
    {
        const bool bGround = Mode == EUnifiedAbilityTargetMode::Ground;
        FAuthoringFixture F(bGround ? FVector(600, 0, 0) : FVector(150, 0, 0));
        F.Ability->Graph = F.QueryDamage();
        F.Ability->TargetMode = Mode;
        F.Activate();
        if (bGround)
        {
            TestEqual(TEXT("Ground waits without committing"),
                      F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
            F.SendGround(FVector(600, 0, 0));
        }
        F.Ability->StepGraph(0.f);
        TestEqual(TEXT("Registered damage consumes GA level 3"), F.Health(), 65.f);
        TestEqual(TEXT("All targeting modes commit once"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        TestFalse(TEXT("Bound immediate graph ends"), F.Ability->IsActive());
        TestFalse(TEXT("No retained target or graph tasks"), F.Ability->HasPendingGraphWork());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphGroundRejects,
                                 "test_25d.UnifiedGA.Authoring.GroundRejectsInvalidBeforeCost",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedGraphGroundRejects::RunTest(const FString&)
{
    for (int32 Scenario = 0; Scenario < 3; ++Scenario)
    {
        FAuthoringFixture F(FVector(150, 0, 0));
        F.Ability->Graph = F.QueryDamage();
        F.Ability->TargetMode = EUnifiedAbilityTargetMode::Ground;
        F.Activate();
        if (Scenario == 0)
        {
            F.SendGround(FVector(5000, 0, 0));
        }
        else if (Scenario == 1)
        {
            F.SendGround(FVector(150, 0, 0), 987654321);
        }
        else
        {
            auto* Replacement = F.World->SpawnActor<AEnemyCharacter>();
            F.ASC->InitAbilityActorInfo(F.Source, Replacement);
            F.SendGround(FVector(150, 0, 0));
        }
        TestEqual(TEXT("Invalid request never damages"), F.Health(), 100.f);
        TestEqual(TEXT("Invalid request never commits"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
        TestFalse(TEXT("Invalid request ends"), F.Ability->IsActive());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphLegacyLifetime,
                                 "test_25d.UnifiedGA.Authoring.LegacyContextAndIndependentLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedGraphLegacyLifetime::RunTest(const FString&)
{
    for (bool bIndependent : {false, true})
    {
        FAuthoringFixture F(FVector(150, 0, 0));
        FString Error;
        F.Ability->Graph = F.LegacyDelay(Error);
        if (!TestNotNull(*FString::Printf(TEXT("Legacy graph compiles: %s"), *Error), F.Ability->Graph.Get()))
        {
            return false;
        }
        F.Ability->TargetMode = EUnifiedAbilityTargetMode::Self;
        F.Ability->ExecutionLifetime =
            bIndependent ? EAbilityExecutionLifetime::IndependentCastEffect : EAbilityExecutionLifetime::BoundToAbility;
        F.Activate();
        auto* WorldGraphs = F.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
        TestEqual(TEXT("Only standard GA charges root cost"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        if (bIndependent)
        {
            TestFalse(TEXT("Independent GA immediately ends"), F.Ability->IsActive());
            TestEqual(TEXT("World owns ongoing graph"), WorldGraphs->GetActiveExecutionCount(), 1);
            WorldGraphs->Tick(.21f);
            TestEqual(TEXT("Shared legacy context executes delayed child"), F.Health(), 80.f);
            WorldGraphs->Tick(.31f);
            TestEqual(TEXT("Completed independent graph released"), WorldGraphs->GetActiveExecutionCount(), 0);
        }
        else
        {
            TestTrue(TEXT("Bound GA waits"), F.Ability->IsActive());
            F.ASC->CancelAbilityHandle(F.Handle);
            F.Ability->StepGraph(1.f);
            TestEqual(TEXT("Bound cancellation prevents delayed child"), F.Health(), 100.f);
            TestEqual(TEXT("Bound execution was never adopted"), WorldGraphs->GetActiveExecutionCount(), 0);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedGraphExplicitInput, "test_25d.UnifiedGA.Authoring.ExplicitReleaseAndToggle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedGraphExplicitInput::RunTest(const FString&)
{
    for (int32 Scenario = 0; Scenario < 3; ++Scenario)
    {
        const bool bToggle = Scenario != 0;
        FAuthoringFixture F(FVector(150, 0, 0), bToggle);
        FString Error;
        F.Ability->Graph = F.LegacyDelay(Error);
        F.Ability->TargetMode = EUnifiedAbilityTargetMode::Self;
        F.Ability->bStopOnInputRelease = !bToggle;
        if (Scenario == 2)
        {
            F.Ability->ExecutionLifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        }
        F.Activate();
        if (Scenario == 2)
        {
            TestFalse(TEXT("Toggle cannot orphan an independent graph"), F.Ability->IsActive());
            TestEqual(TEXT("Invalid toggle lifetime rejected before cost"),
                      F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
            continue;
        }
        TestTrue(TEXT("Channel starts with graph pending"), F.Ability->IsActive());
        const auto Session = F.Ability->GetExecutionSession();
        TestEqual(TEXT("Channel charges only one root cost"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
        if (bToggle)
        {
            CastChecked<UMyAbilitySystemComponent>(F.ASC)->AbilityInputTagPressed(
                FGameplayTag::RequestGameplayTag(TEXT("InputTag.LMB")));
        }
        else
        {
            F.ASC->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, F.Handle,
                                            F.Ability->GetCurrentActivationInfo().GetActivationPredictionKey(),
                                            FPredictionKey());
        }
        F.Ability->StepGraph(1.f);
        TestFalse(TEXT("Actual release/toggle route ends the cast"), F.Ability->IsActive());
        TestTrue(TEXT("Original session closed"), Session && !Session->IsOpen());
        TestFalse(TEXT("Input tasks cleaned"), F.Ability->HasPendingGraphWork());
        TestEqual(TEXT("Cancellation prevents future child damage"), F.Health(), 100.f);
        TestEqual(TEXT("Stopping does not commit again"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 90.f);
    }
    return true;
}
#endif
