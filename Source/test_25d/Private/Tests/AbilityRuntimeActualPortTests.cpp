// 测试链：实际火球目录/零修正预览 -> 可信端口模板 -> 服务器事务；可选声明不等于当前配置实际发布。
// 只验证可编辑契约；真实命中与可选DamageResolved不发的物理路径另有NativeFire/ServerTransport用例。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "Components/ComboBoxString.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeActualNativePortAdmission,
                                 "test_25d.DynamicMage.Task9.Ports.NativeExternalDamageIsNotAnEmittingPort",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeActualNativePortAdmission::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
    if (!TestNotNull(TEXT("Actual FireBolt catalog row exists"), Row) ||
        !TestTrue(TEXT("Zero-edit actual preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        return false;
    }
    const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Delivery.ProjectileAwait");
        });
    if (!TestTrue(TEXT("Actual default graph contains independent delivery"), Index != INDEX_NONE))
    {
        return false;
    }
    const auto* Config = CastChecked<UAbilityDeliveryNodeConfig>(Row->Base.DefaultGraph->Nodes[Index].Config);
    TestTrue(TEXT("Actual native frame owns external payload and never publishes delivery DamageResolved"),
             Config->bUseNativeLaunchParameters);
    const auto Before = F.Developer->GetDeveloperState();
    FAbilityRuntimeDeveloperEdit Edit;
    Edit.DefinitionId = Row->DefinitionId;
    Edit.NodeIndex = Index;
    Edit.ModifierId = TEXT("Append.Registered");
    Edit.FragmentId = TEXT("Target.Petrify");
    Edit.OutputPortId = TEXT("DamageResolved");
    Edit.Value = 1.5f;
    Edit.ExpectedBaseHash = Before.BaseHash;
    Edit.ExpectedRevision = Before.Revision;
    TestFalse(TEXT("A declared but non-emitting native delivery port is rejected transactionally"),
              F.Developer->ApplyEdit(Edit, Error));
    TestEqual(TEXT("Rejected no-op branch preserves revision"), F.Developer->GetDeveloperState().Revision,
              Before.Revision);
    TestEqual(TEXT("Rejected no-op branch preserves recipe hash"), F.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    TestFalse(TEXT("Rejected no-op branch owns no contribution"), F.Developer->HasContributions(Row->Base.AbilityTag));
    Edit.OutputPortId = TEXT("Hit");
    Edit.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
    TestTrue(TEXT("The same fragment accepts the actual ConfirmedImpact port"), F.Developer->ApplyEdit(Edit, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeActualPanelPortChoices,
                                 "test_25d.DynamicMage.Task9.Ports.PanelUsesActualEmitterCapability",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeActualPanelPortChoices::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
    if (!Row || !TestTrue(TEXT("Actual native preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        return false;
    }
    const TStrongObjectPtr<UAbilityRuntimeRecipePanel> Panel(
        CreateWidget<UAbilityRuntimeRecipePanel>(F.World, UAbilityRuntimeRecipePanel::StaticClass()));
    Panel->InitializePanel(F.Developer);
    const auto Slate = Panel->TakeWidget();
    (void)Slate;
    auto* Node = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipeNode")));
    auto* Modifier = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipeModifier")));
    auto* Ports = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("RecipePort")));
    if (!Node || !Modifier || !TestNotNull(TEXT("Real port selector exists"), Ports))
    {
        return false;
    }
    const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Item)
        {
            return Item.TypeId == TEXT("Delivery.ProjectileAwait");
        });
    Node->SetSelectedIndex(Index);
    Modifier->SetSelectedOption(TEXT("Append.Registered"));
    TestEqual(TEXT("Native external payload has no selectable DamageResolved no-op"),
              Ports->FindOptionIndex(TEXT("DamageResolved")), INDEX_NONE);
    TestTrue(TEXT("Actual Hit remains selectable"), Ports->FindOptionIndex(TEXT("Hit")) != INDEX_NONE);
    TestTrue(TEXT("Legitimate terminal Completed remains selectable"),
             Ports->FindOptionIndex(TEXT("Completed")) != INDEX_NONE);
    TestTrue(TEXT("Legitimate expiry remains selectable"), Ports->FindOptionIndex(TEXT("Expired")) != INDEX_NONE);
    TestEqual(TEXT("Executor cancellation is not a business branch"), Ports->FindOptionIndex(TEXT("Cancelled")),
              INDEX_NONE);
    Panel->ClosePanel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeForkedActualPortAdmission,
                                 "test_25d.DynamicMage.Task9.Ports.ForkedPureStreamCannotPublishDamageResolved",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeForkedActualPortAdmission::RunTest(const FString&)
{
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    const auto* Descriptor = Registry.Find(TEXT("Propagation.Forked"));
    if (!TestNotNull(TEXT("Actual Forked strategy is registered"), Descriptor))
    {
        return false;
    }
    TestTrue(TEXT("The old descriptor contains an optional result name but execution only publishes Hit and Completed"),
             Descriptor->Ports.ContainsByPredicate(
                 [](const FAbilityPortDescriptor& Port)
                 {
                     return Port.Name == TEXT("DamageResolved");
                 }));
    FAbilityActualMageFrontendFixture Fixture;
    auto* Owner = Fixture.Developer;
    auto* Config = NewObject<UAbilityForkedPropagationNodeConfig>(Owner);
    Config->bUseNativeLaunchParameters = true;
    FAbilityGraphNode Node{FGuid::NewGuid(), TEXT("Propagation.Forked"), Config};
    FAbilityRuntimeDeveloperEdit Edit;
    Edit.ModifierId = TEXT("Append.Registered");
    Edit.FragmentId = TEXT("Target.Petrify");
    Edit.OutputPortId = TEXT("DamageResolved");
    Edit.Value = 1.5f;
    FAbilityRuntimeRecipePatch Patch;
    FString Error;
    TestFalse(TEXT("An actual external-only Forked stream cannot accept a dead damage-result branch"),
              AbilityRuntimeModifierTemplates::BuildPatch(Owner, Node, Edit, Patch, Error));
    Edit.OutputPortId = TEXT("Hit");
    TestTrue(TEXT("The same real Forked configuration retains its actual ConfirmedImpact branch"),
             AbilityRuntimeModifierTemplates::BuildPatch(Owner, Node, Edit, Patch, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRepeatedResolverExactContract,
                                 "test_25d.DynamicMage.Task9.Ports.RepeatedResolversKeepExactHistoricalInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRepeatedResolverExactContract::RunTest(const FString&)
{
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    const auto* Config = GetDefault<UAbilityResolveImpactNodeConfig>();
    for (const bool bDamage : {false, true})
    {
        const FName Type = bDamage ? TEXT("Target.ResolveDamageTarget") : TEXT("Target.ResolveImpact");
        const auto Expected = bDamage ? EAbilityPortPayload::DamageResult : EAbilityPortPayload::ConfirmedImpact;
        const auto* Descriptor = Registry.Find(Type);
        TestTrue(TEXT("The audited resolver is explicitly synchronous and pure, not inferred from profile"),
                 Descriptor && Descriptor->bRuntimePurePayload && !Descriptor->bIsAsync);
        for (const auto Incoming : {EAbilityPortPayload::Origin, EAbilityPortPayload::LiveTarget,
                                    EAbilityPortPayload::ConfirmedImpact, EAbilityPortPayload::DamageResult,
                                    EAbilityPortPayload::TargetSet, EAbilityPortPayload::PreparedInventory})
        {
            FString Error;
            TestEqual(TEXT("Only the exact fact for that registered strategy can enter a repeated payload"),
                      Config->SupportsRuntimePurePayload(Type, Incoming, Error), Incoming == Expected);
        }
    }
    FString Error;
    TestFalse(
        TEXT("An arbitrary registered name cannot borrow the historical resolver's proof"),
        Config->SupportsRuntimePurePayload(TEXT("Query.Unreviewed"), EAbilityPortPayload::ConfirmedImpact, Error));
    return true;
}
#endif
