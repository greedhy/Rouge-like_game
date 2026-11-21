// 测试链：真实ConfigureParameter配方解析 -> 通用State配置 -> 原状态/位移服务 -> 实际Actor/Mass状态。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "MassEntitySubsystem.h"
#include "Components/BoxComponent.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"
#include "Misc/AutomationTest.h"

namespace AbilityStateTypedRecipeTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard ScriptGuard;
        UAbilityGraphDefinition* Definition = nullptr;
        FFixture()
        {
            Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("UnifiedV2");
        }
        FGuid Add(FName Type, UAbilityNodeConfig* Config)
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
        void Area(FGuid Parent, FName Port)
        {
            auto* Query = NewObject<UAbilityQueryNodeConfig>(Definition);
            Query->Query.Radius = 25.f;
            Query->Query.bRequireVisibility = false;
            const auto Selection = Add(TEXT("Query.Shape"), Query);
            auto* State = NewObject<UAbilityPetrifyNodeConfig>(Definition);
            State->DurationSeconds = 1.f;
            const auto Effect = Add(TEXT("Status.Petrify"), State);
            Definition->Edges.Add({Parent, Selection, Port});
            Definition->Edges.Add({Selection, Effect, TEXT("TargetFound")});
        }
        UAbilityGraphDefinition* Configure(FGuid TargetNode,
                                           std::initializer_list<TPair<EAbilityConfigParameter, float>> Values,
                                           FString& Error)
        {
            FAbilityRuntimeRecipeContribution Contribution;
            Contribution.SourceId = TEXT("Rune.TypedStateParameters");
            for (const auto& Value : Values)
            {
                FAbilityRuntimeRecipePatch Patch;
                Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
                Patch.TargetNodeId = TargetNode;
                Patch.Parameter = Value.Key;
                Patch.Scalar = Value.Value;
                Contribution.Patches.Add(Patch);
            }
            UAbilityGraphDefinition* Compiled = nullptr;
            return AbilityRuntimeRecipes::Compile(World, Definition, {Contribution}, Compiled, Error) ? Compiled
                                                                                                      : nullptr;
        }
        FAbilityNodeInput InputFor(FPureMassCombatTarget TargetIdentity, FString& Error)
        {
            FAbilityNodeInput Payload;
            Payload.SourceToken = SourceToken;
            Payload.Direction = FVector::ForwardVector;
            FAbilityTargetToken TargetLife;
            if (AbilityExecutionIdentity::CaptureTarget(SourceToken, TargetIdentity, TargetLife, Error))
            {
                Payload.LiveTargetToken = TargetLife;
            }
            return Payload;
        }
        UAbilityGraphInstance* Start(UAbilityGraphDefinition& GraphDefinition, const FAbilityNodeInput& Payload,
                                     FString& Error)
        {
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            if (!Run->StartWithInput(&GraphDefinition, Payload, {}, Error) ||
                (Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed))
            {
                Error +=
                    FString::Printf(TEXT("; finished=%d reason=%d"), Run->IsFinished(), int32(Run->GetEndReason()));
                return nullptr;
            }
            return Run;
        }
        int32 Mass(FVector Position)
        {
            auto* Sim = World->GetSubsystem<UPureMassSubsystem>();
            Sim->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 200.f;
            Type->Radius = 20.f;
            Type->HalfHeight = 40.f;
            Type->XPReward = 0;
            return Sim->SpawnMonster(Type, Position);
        }
        bool Petrified(int32 Id) const
        {
            FPureMassView View;
            return World->GetSubsystem<UPureMassSubsystem>()->Resolve(Id, View) && View.bPetrified;
        }
        void Floor()
        {
            auto* Actor = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(2000, 2000, 10));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Actor->SetActorLocation(FVector(0, 0, -51));
        }
    };
} // namespace AbilityStateTypedRecipeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttachTypedRecipeParameters,
                                 "test_25d.DynamicMage.TypedState.AttachParserChangesActualCadenceAndTransfer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAttachTypedRecipeParameters::RunTest(const FString&)
{
    using namespace AbilityStateTypedRecipeTests;
    FFixture F;
    const int32 Next = F.Mass(FVector(250, 0, 0));
    auto* Base = NewObject<UAbilityAttachNodeConfig>(F.Definition);
    Base->Duration = 1.f;
    Base->Period = .1f;
    Base->MaxTransfers = 0;
    Base->TransferRadius = 20.f;
    const auto Root = F.Add(TEXT("State.Attach"), Base);
    auto* Petrify = NewObject<UAbilityPetrifyNodeConfig>(F.Definition);
    Petrify->DurationSeconds = 1.f;
    F.Definition->Edges.Add({Root, F.Add(TEXT("Status.Petrify"), Petrify), TEXT("Period")});
    FString Error;
    auto* Compiled = F.Configure(Root,
                                 {{EAbilityConfigParameter::Period, .2f},
                                  {EAbilityConfigParameter::Duration, .5f},
                                  {EAbilityConfigParameter::MaxTransfers, 1.f},
                                  {EAbilityConfigParameter::TransferRadius, 200.f}},
                                 Error);
    if (!TestNotNull(TEXT("Runtime parser accepts actual Attach axes"), Compiled))
    {
        AddError(Error);
        return false;
    }
    auto* Run = F.Start(*Compiled, F.InputFor(FPureMassCombatTarget::FromActor(F.Target), Error), Error);
    if (!TestNotNull(TEXT("Typed Attach graph starts without a strategy failure"), Run))
    {
        AddError(Error);
        return false;
    }
    Run->Advance(.19f);
    TestFalse(TEXT("Configured longer period delays the actual state"), F.IsPetrified());
    Run->Advance(.01f);
    TestTrue(TEXT("Actual Actor state starts at configured period"), F.IsPetrified());
    F.Target->Destroy();
    Run->Advance(.2f);
    TestTrue(TEXT("Configured transfer count and radius allow actual Actor-to-Mass transfer"), F.Petrified(Next));
    Run->Advance(.11f);
    TestTrue(TEXT("Configured duration closes the actual anchor"), Run->IsFinished());
    TestEqual(TEXT("Original generic base period remains untouched"), Base->Period, .1f);
    TestEqual(TEXT("Original generic transfer policy remains untouched"), Base->MaxTransfers, 0);
    auto* FrameOnly = NewObject<UAbilityPrototypeAttachNodeConfig>();
    TestFalse(TEXT("Frame-driven helper rejects ignored local tuning; caller must edit the actual parent Scope"),
              FrameOnly->TrySetRecipeParameter(EAbilityConfigParameter::Period, .2f, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackTypedRecipeParameters,
                                 "test_25d.DynamicMage.TypedState.StackParserChangesActualSharedThresholdAndExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStackTypedRecipeParameters::RunTest(const FString&)
{
    using namespace AbilityStateTypedRecipeTests;
    FFixture F;
    auto* Base = NewObject<UAbilityStackNodeConfig>(F.Definition);
    Base->Stack.EffectId = TEXT("TypedStackActual");
    const auto Root = F.Add(TEXT("State.Stack"), Base);
    F.Area(Root, TEXT("Threshold"));
    FString Error;
    auto* Compiled = F.Configure(Root,
                                 {{EAbilityConfigParameter::StackThreshold, 2.f},
                                  {EAbilityConfigParameter::StackLifetime, .3f},
                                  {EAbilityConfigParameter::DetonationDelay, .1f}},
                                 Error);
    if (!TestNotNull(TEXT("Runtime parser accepts actual shared Stack axes"), Compiled))
    {
        AddError(Error);
        return false;
    }
    const auto Payload = F.InputFor(FPureMassCombatTarget::FromActor(F.Target), Error);
    auto* First = F.Start(*Compiled, Payload, Error);
    auto* Second = F.Start(*Compiled, Payload, Error);
    if (!TestNotNull(TEXT("First actual stack starts"), First) ||
        !TestNotNull(TEXT("Second actual stack starts"), Second))
    {
        AddError(Error);
        return false;
    }
    auto* WorldState = F.World->GetSubsystem<UExtensionWorldSubsystem>();
    WorldState->Tick(.09f);
    Second->Advance(0.f);
    TestFalse(TEXT("Configured threshold waits its actual detonation delay"), F.IsPetrified());
    WorldState->Tick(.02f);
    Second->Advance(0.f);
    TestTrue(TEXT("Two actual casts reach the configured threshold and independent area state"), F.IsPetrified());
    auto* Expiring = F.Start(*Compiled, Payload, Error);
    if (TestNotNull(TEXT("Fresh nonthreshold stack starts with its own context"), Expiring))
    {
        WorldState->Tick(.31f);
        Expiring->Advance(0.f);
        TestEqual(TEXT("Configured lifetime actually removes the remaining world stack"),
                  WorldState->LayersFor(F.Source, Base->Stack.EffectId, FPureMassCombatTarget::FromActor(F.Target)), 0);
    }
    TestEqual(TEXT("Original shared threshold was not mutated"), Base->Stack.Threshold, 3);
    TestEqual(TEXT("Original shared lifetime was not mutated"), Base->Stack.Lifetime, 4.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDisplaceTypedRecipeParameters,
                                 "test_25d.DynamicMage.TypedState.DisplaceParserUsesPrivateSettingsAndActualMovement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDisplaceTypedRecipeParameters::RunTest(const FString&)
{
    using namespace AbilityStateTypedRecipeTests;
    for (const bool bNearSafe : {false, true})
    {
        FFixture F;
        F.Target->Destroy();
        F.Floor();
        const int32 Id = F.Mass(FVector(200, 0, 0));
        auto* OriginalSettings = NewObject<UExtensionNode>(F.World);
        OriginalSettings->Behavior = EExtensionKind::Displace;
        OriginalSettings->Duration = .5f;
        OriginalSettings->MoveDistance = 180.f;
        OriginalSettings->SafeDistance = 70.f;
        auto* Base = NewObject<UAbilityTargetDisplaceNodeConfig>(F.Definition);
        Base->Settings = OriginalSettings;
        const auto Root = F.Add(TEXT("Movement.TargetDisplace"), Base);
        F.Area(Root, TEXT("Completed"));
        FString Error;
        auto* Compiled = F.Configure(Root,
                                     {{EAbilityConfigParameter::MoveDistance, 80.f},
                                      {EAbilityConfigParameter::SafeDistance, bNearSafe ? 150.f : 30.f},
                                      {EAbilityConfigParameter::Duration, 1.f}},
                                     Error);
        if (!TestNotNull(TEXT("Runtime parser accepts actual displacement axes"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, F.InputFor(FPureMassCombatTarget(Id), Error), Error);
        if (!TestNotNull(TEXT("Configured displacement starts actual world command"), Run))
        {
            AddError(Error);
            return false;
        }
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        auto& Fragment = F.World->GetSubsystem<UMassEntitySubsystem>()
                             ->GetMutableEntityManager()
                             .GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Id));
        for (int32 Step = 0; Step < 2; ++Step)
        {
            State->MoveMass(Fragment, .5f);
            if (Sim->BeginUpdate(.001f))
            {
                Sim->UpdateMonster(Fragment);
                Sim->FinishUpdate();
            }
            State->Tick(.5f);
            Run->Advance(.5f);
            if (Step == 0)
            {
                TestTrue(TEXT("Configured duration changes the actual halfway position"),
                         FMath::IsNearlyEqual(Fragment.Location.X, bNearSafe ? 175.0 : 160.0, 1.0));
                TestFalse(TEXT("A halfway command does not emit normal completion"), F.Petrified(Id));
            }
        }
        TestTrue(TEXT("Configured movement and safety geometry determine the real destination"),
                 FMath::IsNearlyEqual(Fragment.Location.X, bNearSafe ? 150.0 : 120.0, 1.0));
        TestTrue(TEXT("Actual movement completion originates the independent area state"), F.Petrified(Id));
        TestEqual(TEXT("Shared original movement duration is unchanged"), OriginalSettings->Duration, .5f);
        TestEqual(TEXT("Shared original movement distance is unchanged"), OriginalSettings->MoveDistance, 180.f);
        TestEqual(TEXT("Shared original safe distance is unchanged"), OriginalSettings->SafeDistance, 70.f);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateTypedRejectedValues,
                                 "test_25d.DynamicMage.TypedState.InvalidValuesAndSharedSettingsRemainAtomic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStateTypedRejectedValues::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FString Error;
    auto* Attach = NewObject<UAbilityAttachNodeConfig>(F.World);
    TestFalse(TEXT("A period below the real Attach contract is rejected"),
              Attach->TrySetRecipeParameter(EAbilityConfigParameter::Period, .01f, Error));
    TestEqual(TEXT("Rejected Attach period is unchanged"), Attach->Period, .25f);
    TestFalse(TEXT("Fractional transfers cannot be silently rounded"),
              Attach->TrySetRecipeParameter(EAbilityConfigParameter::MaxTransfers, 1.5f, Error));
    TestEqual(TEXT("Rejected transfer count is unchanged"), Attach->MaxTransfers, 2);
    auto* Stack = NewObject<UAbilityStackNodeConfig>(F.World);
    Stack->Stack.EffectId = TEXT("AtomicTypedStack");
    TestFalse(TEXT("Fractional shared thresholds cannot be silently rounded"),
              Stack->TrySetRecipeParameter(EAbilityConfigParameter::StackThreshold, 2.5f, Error));
    TestEqual(TEXT("Rejected threshold is unchanged"), Stack->Stack.Threshold, 3);
    TestFalse(TEXT("A zero stack lifetime is rejected by the actual service contract"),
              Stack->TrySetRecipeParameter(EAbilityConfigParameter::StackLifetime, 0.f, Error));
    TestEqual(TEXT("Rejected lifetime is unchanged"), Stack->Stack.Lifetime, 4.f);
    auto* SharedSettings = NewObject<UExtensionNode>(F.World);
    SharedSettings->Behavior = EExtensionKind::Displace;
    SharedSettings->Duration = .5f;
    SharedSettings->MoveDistance = 180.f;
    auto* Move = NewObject<UAbilityTargetDisplaceNodeConfig>(F.World);
    Move->Settings = SharedSettings;
    TestFalse(TEXT("A distance above the actual displacement contract is rejected"),
              Move->TrySetRecipeParameter(EAbilityConfigParameter::MoveDistance, 1201.f, Error));
    TestTrue(TEXT("Rejected displacement keeps the original settings reference"), Move->Settings == SharedSettings);
    TestEqual(TEXT("Rejected displacement cannot mutate a shared source asset"), SharedSettings->MoveDistance, 180.f);
    TestTrue(TEXT("A supported displacement parameter is accepted"),
             Move->TrySetRecipeParameter(EAbilityConfigParameter::MoveDistance, 80.f, Error));
    TestTrue(TEXT("Successful tuning owns a separate private settings object"), Move->Settings != SharedSettings);
    TestEqual(TEXT("Successful tuning also preserves the shared original value"), SharedSettings->MoveDistance, 180.f);
    const auto* AcceptedSettings = Move->Settings.Get();
    TestFalse(TEXT("A later invalid safety value is rejected"),
              Move->TrySetRecipeParameter(EAbilityConfigParameter::SafeDistance, -1.f, Error));
    TestTrue(TEXT("A rejected edit preserves the accepted private reference"),
             Move->Settings.Get() == AcceptedSettings);
    TestEqual(TEXT("A rejected edit preserves the accepted movement value"), Move->Settings->MoveDistance, 80.f);
    return true;
}
#endif
