// 测试链：实际目录BeginPreview -> 可信typed模板/配置事务 -> 私有图/撤回版本；不手工RegisterBase。
// 此批先证明缺失模板/操作的真实RED；实际作用的HP/运动/状态测试由对应节点和后续前端场景补充。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "PureMass/PureMassSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/AutomationTest.h"

namespace AbilityRuntimeTypedTemplateTests
{
    /** 使用服务器当前所见版本；Modifier不是反射字段名。 */
    FAbilityRuntimeDeveloperEdit Edit(FAbilityActualMageFrontendFixture& F,
                                      const FAbilityRuntimeDeveloperDefinition& Row, int32 Index, FName Id, float Value)
    {
        FAbilityRuntimeDeveloperEdit Result;
        Result.DefinitionId = Row.DefinitionId;
        Result.NodeIndex = Index;
        Result.ModifierId = Id;
        Result.Value = Value;
        Result.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        Result.ExpectedBaseHash = Row.BaseHash;
        return Result;
    }
} // namespace AbilityRuntimeTypedTemplateTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTypedConfigurePrivateParameters,
                                 "test_25d.DynamicMage.Task9.TypedConfigurePrivateParametersAndConflicts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTypedConfigurePrivateParameters::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(
        TEXT(
            "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Multishot.GA_Prototype_Multishot_C"),
        Error);
    if (!TestNotNull(TEXT("Actual Multishot default exists"), Row))
    {
        return false;
    }
    auto* Base = Row->Base.DefaultGraph.Get();
    const auto* Root = Base->Nodes.FindByPredicate(
        [&](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Base->EntryNodeId;
        });
    const auto* Scope = Root ? Cast<UAbilityPrototypeScopeNodeConfig>(Root->Config) : nullptr;
    if (!TestNotNull(TEXT("Actual production exporter contains a concrete root Scope"), Scope))
    {
        return false;
    }
    FAbilityRuntimeRecipeContribution One;
    One.SourceId = TEXT("Typed.Count");
    FAbilityRuntimeRecipePatch Count;
    Count.TargetNodeId = Root->NodeId;
    Count.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Count.Parameter = EAbilityConfigParameter::Count;
    Count.Scalar = 5.f;
    One.Patches.Add(Count);
    UAbilityGraphDefinition* Result = nullptr;
    if (!TestTrue(TEXT("One consumed typed axis configures a private actual definition: ") + Error,
                  AbilityRuntimeRecipes::Compile(F.World, Base, {One}, Result, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const int32 RootIndex = Base->Nodes.IndexOfByPredicate(
        [&](const FAbilityGraphNode& Node)
        {
            return Node.NodeId == Root->NodeId;
        });
    const auto* Changed = Cast<UAbilityPrototypeScopeNodeConfig>(Result->Nodes[RootIndex].Config);
    TestEqual(TEXT("Actual private Count changes without changing original asset"), Changed->Parameters->Count, 5);
    TestEqual(TEXT("Original root Count remains unchanged"), Scope->Parameters->Count, 3);
    auto Two = One;
    Two.SourceId = TEXT("Typed.Spread");
    Two.Patches[0].Parameter = EAbilityConfigParameter::Spread;
    Two.Patches[0].Scalar = 20.f;
    TestTrue(TEXT("Orthogonal typed axes compose on the same node"),
             AbilityRuntimeRecipes::Compile(F.World, Base, {One, Two}, Result, Error));
    Two.Patches[0].Parameter = EAbilityConfigParameter::Count;
    TestFalse(TEXT("Different sources cannot silently compete for one absolute typed axis"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {One, Two}, Result, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTypedPanelMissingAxes,
                                 "test_25d.DynamicMage.Task9.ActualPanelTypedAxesAndBatchPayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTypedPanelMissingAxes::RunTest(const FString&)
{
    using namespace AbilityRuntimeTypedTemplateTests;
    struct FCase
    {
        const TCHAR* Name;
        const TCHAR* Modifier;
        const TCHAR* Type;
        float Value;
    };
    const FCase Cases[] = {
        {TEXT("Multishot"), TEXT("Set.Count"), TEXT("Scope.Prototype"), 5.f},
        {TEXT("Emitter"), TEXT("Set.Period"), TEXT("Scope.Prototype"), .2f},
        {TEXT("Delay"), TEXT("Set.ArmDelay"), TEXT("Scope.Prototype"), .2f},
        {TEXT("Line"), TEXT("Set.Range"), TEXT("Scope.Prototype"), 750.f},
        {TEXT("Projectile"), TEXT("Set.CollisionRadius"), TEXT("Scope.Prototype"), 30.f},
        {TEXT("Field"), TEXT("Replace.Status.Petrify"), TEXT("Effect.PrototypeDamageBatch"), 1.5f},
    };
    for (const auto& Case : Cases)
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const FString Path = FString::Printf(
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_%s.GA_Prototype_%s_C"),
            Case.Name, Case.Name);
        const auto* Row = F.Definition(Path, Error);
        if (!TestNotNull(TEXT("Exact actual catalog base"), Row) ||
            !TestTrue(TEXT("Actual preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            return false;
        }
        const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == Case.Type;
            });
        if (!TestTrue(TEXT("The actual requested behavior axis exists"), Index != INDEX_NONE))
        {
            return false;
        }
        AddInfo(Path + TEXT(" | ") + Case.Modifier);
        TestTrue(TEXT("Server trusted template accepts the concrete consumed axis/batch payload"),
                 F.Developer->ApplyEdit(Edit(F, *Row, Index, Case.Modifier, Case.Value), Error));
        AddInfo(Error);
        F.Developer->EndPreview(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTypedPreviewOwnedWithdrawal,
                                 "test_25d.DynamicMage.Task9.RemoveAndResetOwnedModifiersKeepGrant",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTypedPreviewOwnedWithdrawal::RunTest(const FString&)
{
    using namespace AbilityRuntimeTypedTemplateTests;
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"), Error);
    if (!TestNotNull(TEXT("Actual Petrify base"), Row) ||
        !TestTrue(TEXT("Actual preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        return false;
    }
    const auto Grant = F.Developer->GetPreviewHandle();
    auto Request = Edit(F, *Row, 0, TEXT("Scale.Radius"), 2.f);
    if (!TestTrue(TEXT("Existing supported template establishes a real owned source"),
                  F.Developer->ApplyEdit(Request, Error)))
    {
        return false;
    }
    Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
    TestTrue(TEXT("Remove exact owned edit without ending preview"), F.Developer->RemoveEdit(Request, Error));
    TestTrue(TEXT("Same temporary spec remains after individual removal"),
             F.Developer->GetPreviewHandle() == Grant && F.PlayerASC->FindAbilitySpecFromHandle(Grant));
    TestFalse(TEXT("Individual removal really removes the owned contribution"),
              F.Developer->HasContributions(Row->Base.AbilityTag));
    Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
    TestTrue(TEXT("A new edit can be applied on the same preview grant"), F.Developer->ApplyEdit(Request, Error));
    TestTrue(
        TEXT("Reset removes only owned edits while keeping preview"),
        F.Developer->ResetPreview(Row->DefinitionId, F.Developer->GetDeveloperState().Revision, Row->BaseHash, Error));
    TestTrue(TEXT("Reset keeps exact original temporary grant"),
             F.Developer->GetPreviewHandle() == Grant && F.PlayerASC->FindAbilitySpecFromHandle(Grant));
    TestFalse(TEXT("Reset actually withdraws the owned source"), F.Developer->HasContributions(Row->Base.AbilityTag));
    F.Developer->EndPreview(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTypedCountAndSpreadRealContacts,
                                 "test_25d.DynamicMage.Task9.ActualPreviewCountAndSpreadConsumeRealPellets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTypedCountAndSpreadRealContacts::RunTest(const FString&)
{
    using namespace AbilityRuntimeTypedTemplateTests;
    for (const float Spread : {0.f, 90.f})
    {
        FAbilityActualMageFrontendFixture F;
        F.Target->Destroy();
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 1000.f;
        Type->Radius = 2.f;
        Type->HalfHeight = 10.f;
        Type->XPReward = 0;
        const int32 Victim = Mass->SpawnMonster(Type, FVector(200, 0, 0));
        FString Error;
        const auto* Row =
            F.Definition(TEXT("/Game/PureMass/Abilities/GA_Prototype_Multishot.GA_Prototype_Multishot_C"), Error);
        if (!TestNotNull(TEXT("Actual mixed Multishot directory version"), Row) ||
            !TestTrue(TEXT("Real catalog preview starts"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            return false;
        }
        const int32 RootIndex = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
            });
        const auto* Original =
            CastChecked<UAbilityPrototypeScopeNodeConfig>(Row->Base.DefaultGraph->Nodes[RootIndex].Config);
        if (!TestTrue(TEXT("Accepted real Count edit requests five pellets"),
                      F.Developer->ApplyEdit(Edit(F, *Row, RootIndex, TEXT("Set.Count"), 5.f), Error)) ||
            !TestTrue(TEXT("Orthogonal real Spread edit is accepted"),
                      F.Developer->ApplyEdit(Edit(F, *Row, RootIndex, TEXT("Set.Spread"), Spread), Error)))
        {
            AddInfo(Error);
            return false;
        }
        FAbilityRuntimeRecipeSnapshot Snapshot;
        if (!TestTrue(TEXT("Use actual accepted parsed recipe without manually installing a graph"),
                      F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Snapshot, Error)))
        {
            return false;
        }
        auto* Context = NewObject<UPrototypeCastContext>(F.World);
        Context->Caster = F.Pawn;
        Context->CastId = FGuid::NewGuid();
        Context->Level = 1;
        Context->DamageParams.SourceAbilitySystemComponent = F.PlayerASC;
        if (!TestTrue(TEXT("Capture real current PlayerState/Avatar source"), Context->CaptureSourceLife(Error)))
        {
            return false;
        }
        FAbilityNodeInput Input;
        Input.Context = Context;
        Input.SourceToken = Context->GetSourceLife();
        Input.DamageParams = Context->DamageParams;
        Input.Direction = FVector::ForwardVector;
        FAbilityExecutionOptions Options;
        Options.CompatibilityProfile = Snapshot.Graph->CompatibilityProfile;
        Options.BudgetPolicy = Snapshot.Graph->BudgetPolicy;
        const TStrongObjectPtr<UAbilityGraphInstance> Run(NewObject<UAbilityGraphInstance>(F.World));
        if (!TestTrue(TEXT("The actual parsed snapshot starts in the production executor"),
                      Run->StartWithInput(Snapshot.Graph, Input, Options, Error)))
        {
            AddInfo(Error);
            return false;
        }
        TestEqual(TEXT("One real container and exactly five independent pellet scopes are admitted"), Context->Spawned,
                  6);
        for (int32 Index = 0; Index < 40; ++Index)
        {
            ++GFrameCounter;
            Run->Advance(.02f);
        }
        FPureMassView View;
        if (TestTrue(TEXT("Real target remains alive after actual sweeps"), Mass->Resolve(Victim, View)))
        {
            const float Expected = 1000.f - (Spread == 0.f ? 5.f : 1.f) * Original->Parameters->DamageAtLevel(1);
            TestTrue(TEXT("Only actual geometric pellet contacts consume the unmodified original damage once"),
                     FMath::IsNearlyEqual(View.Health, Expected, .01f));
        }
        AddInfo(TEXT(
            "Evidence=actual directory BeginPreview/typed ApplyEdit/real parsed snapshot/sweep+Mass HP; no frontend or network claim"));
        Run->Cancel();
        F.Developer->EndPreview(Error);
    }
    return true;
}
#endif
