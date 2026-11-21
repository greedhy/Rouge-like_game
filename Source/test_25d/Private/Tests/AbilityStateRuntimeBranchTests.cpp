// 测试链：无payload的真实State策略基图 -> 可信Append.Registered工厂/配方解析 -> 实际Actor/Mass状态。
// 此处证明运行时结构组合与生命/取消门，不把直接改配置的setter测试当作跨轴组合。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "MassEntitySubsystem.h"
#include "Components/BoxComponent.h"
#include "UObject/Script.h"
#include "Misc/AutomationTest.h"

namespace AbilityStateRuntimeBranchTests
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
        void Root(FName Type, UAbilityNodeConfig* Config)
        {
            FAbilityGraphNode Node;
            Node.NodeId = Definition->EntryNodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            Node.Config = Config;
            Definition->Nodes = {Node};
        }
        UAbilityGraphDefinition* Append(FName Port, FName Fragment, FString& Error)
        {
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.ModifierId = TEXT("Append.Registered");
            Edit.OutputPortId = Port;
            Edit.FragmentId = Fragment;
            Edit.Value = 1.f;
            Edit.Radius = 100.f;
            FAbilityRuntimeRecipePatch Patch;
            if (!AbilityRuntimeModifierTemplates::BuildPatch(World, Definition->Nodes[0], Edit, Patch, Error))
            {
                return nullptr;
            }
            FAbilityRuntimeRecipeContribution Rule;
            Rule.SourceId = TEXT("Rune.StructuralStateBranch");
            Rule.Patches.Add(Patch);
            UAbilityGraphDefinition* Compiled = nullptr;
            return AbilityRuntimeRecipes::Compile(World, Definition, {Rule}, Compiled, Error) ? Compiled : nullptr;
        }
        FAbilityNodeInput InputFor(FPureMassCombatTarget Identity, FString& Error)
        {
            FAbilityNodeInput Input;
            Input.SourceToken = SourceToken;
            Input.Direction = FVector::ForwardVector;
            FAbilityTargetToken Life;
            if (AbilityExecutionIdentity::CaptureTarget(SourceToken, Identity, Life, Error))
            {
                Input.LiveTargetToken = Life;
            }
            return Input;
        }
        UAbilityGraphInstance* Start(UAbilityGraphDefinition& GraphDefinition, const FAbilityNodeInput& Input,
                                     FString& Error)
        {
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            if (!Run->StartWithInput(&GraphDefinition, Input, {}, Error) ||
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
} // namespace AbilityStateRuntimeBranchTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttachRuntimeBranch,
                                 "test_25d.DynamicMage.RuntimeBranches.AttachPeriodGetsTrustedPayloadAndRejectsNewLife",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAttachRuntimeBranch::RunTest(const FString&)
{
    using namespace AbilityStateRuntimeBranchTests;
    for (const bool bRetire : {false, true})
    {
        FFixture F;
        auto* Base = NewObject<UAbilityAttachNodeConfig>(F.Definition);
        Base->Period = .1f;
        Base->Duration = 1.f;
        Base->MaxTransfers = 0;
        F.Root(TEXT("State.Attach"), Base);
        const FString BaseHash = AbilityRuntimeRecipes::Fingerprint(*F.Definition);
        FString Error;
        auto* Compiled = F.Append(TEXT("Period"), TEXT("Target.Petrify"), Error);
        if (!TestNotNull(TEXT("Actual trusted factory/parser appends a different effect family to attachment"),
                         Compiled))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("Runtime structure has an accepted new content identity"),
                 AbilityRuntimeRecipes::Fingerprint(*Compiled) != BaseHash);
        auto* Run = F.Start(*Compiled, F.InputFor(FPureMassCombatTarget::FromActor(F.Target), Error), Error);
        if (!TestNotNull(TEXT("Actual appended attachment graph starts"), Run))
        {
            AddError(Error);
            return false;
        }
        Run->Advance(.05f);
        TestFalse(TEXT("A runtime payload does not bypass the attachment's real period"), F.IsPetrified());
        if (bRetire)
        {
            F.Target->EnableDungeonPooling();
        }
        Run->Advance(.06f);
        TestEqual(TEXT("Original anchor life alone can receive the runtime-added state"), F.IsPetrified(), !bRetire);
        Run->Cancel();
        const auto Count = Run->GetAcceptedEventCount();
        Run->Advance(2.f);
        TestEqual(TEXT("Cancellation emits no late attached payload"), Run->GetAcceptedEventCount(), Count);
        TestEqual(TEXT("The base remains a payload-free original graph"), F.Definition->Nodes.Num(), 1);
        TestEqual(TEXT("Runtime append does not mutate the original base hash"),
                  AbilityRuntimeRecipes::Fingerprint(*F.Definition), BaseHash);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackRuntimeBranch,
                                 "test_25d.DynamicMage.RuntimeBranches.SharedStackThresholdGetsMixedAreaAndSourceGate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStackRuntimeBranch::RunTest(const FString&)
{
    using namespace AbilityStateRuntimeBranchTests;
    for (const bool bRetire : {false, true})
    {
        FFixture F;
        const int32 Neighbor = F.Mass(FVector(200, 0, 0));
        auto* Base = NewObject<UAbilityStackNodeConfig>(F.Definition);
        Base->Stack.EffectId = TEXT("RuntimeBranchStack");
        Base->Stack.Threshold = 2;
        Base->Stack.DetonationDelay = .1f;
        F.Root(TEXT("State.Stack"), Base);
        FString Error;
        auto* Compiled = F.Append(TEXT("Threshold"), TEXT("Area.Petrify"), Error);
        if (!TestNotNull(TEXT("Trusted factory/parser composes a threshold position with independent area state"),
                         Compiled))
        {
            AddError(Error);
            return false;
        }
        const auto Input = F.InputFor(FPureMassCombatTarget::FromActor(F.Target), Error);
        auto* A = F.Start(*Compiled, Input, Error);
        auto* B = F.Start(*Compiled, Input, Error);
        if (!TestNotNull(TEXT("First actual shared layer starts"), A) ||
            !TestNotNull(TEXT("Second actual shared layer starts"), B))
        {
            AddError(Error);
            return false;
        }
        if (bRetire)
        {
            F.Source->EnableDungeonPooling();
        }
        F.World->GetSubsystem<UExtensionWorldSubsystem>()->Tick(.11f);
        A->Advance(0.f);
        B->Advance(0.f);
        TestEqual(TEXT("Actual threshold affects an Actor only under the original valid source life"), F.IsPetrified(),
                  !bRetire);
        TestEqual(TEXT("The same dynamically added area affects actual Mass without a second GA"),
                  F.Petrified(Neighbor), !bRetire);
        TestEqual(TEXT("Trigger/source invalidation removes the original world record"),
                  F.World->GetSubsystem<UExtensionWorldSubsystem>()->LayersFor(
                      F.Source, Base->Stack.EffectId, FPureMassCombatTarget::FromActor(F.Target)),
                  0);
        A->Cancel();
        B->Cancel();
        TestEqual(TEXT("Base stack never acquired a pre-baked effect branch"), F.Definition->Nodes.Num(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMoveRuntimeBranch, "test_25d.DynamicMage.RuntimeBranches.DisplacementCompletionGetsAreaWithoutRetiredSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMoveRuntimeBranch::RunTest(const FString&)
{
    using namespace AbilityStateRuntimeBranchTests;
    for (const bool bRetire : {false, true})
    {
        FFixture F;
        F.Target->Destroy();
        F.Floor();
        const int32 Id = F.Mass(FVector(200, 0, 0));
        auto* Base = NewObject<UAbilityTargetDisplaceNodeConfig>(F.Definition);
        Base->Settings = NewObject<UExtensionNode>(Base);
        Base->Settings->Behavior = EExtensionKind::Displace;
        Base->Settings->Duration = .5f;
        Base->Settings->MoveDistance = 180.f;
        Base->Settings->SafeDistance = 70.f;
        F.Root(TEXT("Movement.TargetDisplace"), Base);
        FString Error;
        auto* Compiled = F.Append(TEXT("Completed"), TEXT("Area.Petrify"), Error);
        if (!TestNotNull(TEXT("Trusted factory/parser appends an independent completion-area effect"), Compiled))
        {
            AddError(Error);
            return false;
        }
        auto* Run = F.Start(*Compiled, F.InputFor(FPureMassCombatTarget(Id), Error), Error);
        if (!TestNotNull(TEXT("Runtime movement/area composition starts"), Run))
        {
            AddError(Error);
            return false;
        }
        auto* Sim = F.World->GetSubsystem<UPureMassSubsystem>();
        auto* State = F.World->GetSubsystem<UExtensionWorldSubsystem>();
        auto& Fragment = F.World->GetSubsystem<UMassEntitySubsystem>()
                             ->GetMutableEntityManager()
                             .GetFragmentDataChecked<FPureMassFragment>(Sim->GetEntity(Id));
        if (bRetire)
        {
            F.Source->EnableDungeonPooling();
            State->Tick(.01f);
            Run->Advance(.01f);
        }
        State->MoveMass(Fragment, .5f);
        if (Sim->BeginUpdate(.001f))
        {
            Sim->UpdateMonster(Fragment);
            Sim->FinishUpdate();
        }
        State->Tick(.5f);
        Run->Advance(.5f);
        TestTrue(TEXT("Only the original valid movement source can change the actual destination"),
                 FMath::IsNearlyEqual(Fragment.Location.X, bRetire ? 200.0 : 70.0, 1.0));
        TestEqual(TEXT("Retired movement never executes a normal completion area"), F.Petrified(Id), !bRetire);
        TestTrue(TEXT("Completion/source failure releases its graph wait"), Run->IsFinished());
        Run->Cancel();
        TestEqual(TEXT("Base displacement still contains no baked completion effect"), F.Definition->Nodes.Num(), 1);
    }
    return true;
}
#endif
