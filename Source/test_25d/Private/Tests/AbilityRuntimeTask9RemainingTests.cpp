// 测试链：真实法师目录/BeginPreview -> 可信模板ID/准确端口 -> 生产Compile事务及私有快照。
// 本批首先验证缺失的面板轴/片段工厂；属性、费用与真实输入证据由各后续动作病例单独报告。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace AbilityRuntimeTask9RemainingTests
{
    /** 编辑身份只取服务器已经公开的当前版本，不能用任意SourceId或手工基础注册替代目录。 */
    FAbilityRuntimeDeveloperEdit Edit(FAbilityActualMageFrontendFixture& F,
                                      const FAbilityRuntimeDeveloperDefinition& Row, int32 Node, FName Modifier,
                                      float Value)
    {
        FAbilityRuntimeDeveloperEdit Result;
        Result.DefinitionId = Row.DefinitionId;
        Result.NodeIndex = Node;
        Result.ModifierId = Modifier;
        Result.Value = Value;
        Result.ExpectedBaseHash = Row.BaseHash;
        Result.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
        return Result;
    }
} // namespace AbilityRuntimeTask9RemainingTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9CompleteTypedCatalog,
                                 "test_25d.DynamicMage.Task9.Remaining.CompleteTrustedParameterCatalog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9CompleteTypedCatalog::RunTest(const FString&)
{
    struct FCase
    {
        const TCHAR* Id;
        EAbilityConfigParameter Parameter;
    };
    const FCase Cases[] = {{TEXT("Set.Count"), EAbilityConfigParameter::Count},
                           {TEXT("Set.Spread"), EAbilityConfigParameter::Spread},
                           {TEXT("Set.Period"), EAbilityConfigParameter::Period},
                           {TEXT("Set.ArmDelay"), EAbilityConfigParameter::ArmDelay},
                           {TEXT("Set.Duration"), EAbilityConfigParameter::Duration},
                           {TEXT("Set.Radius"), EAbilityConfigParameter::Radius},
                           {TEXT("Set.InnerRadius"), EAbilityConfigParameter::InnerRadius},
                           {TEXT("Set.Width"), EAbilityConfigParameter::Width},
                           {TEXT("Set.HalfHeight"), EAbilityConfigParameter::HalfHeight},
                           {TEXT("Set.Range"), EAbilityConfigParameter::Range},
                           {TEXT("Set.CollisionRadius"), EAbilityConfigParameter::CollisionRadius},
                           {TEXT("Set.Speed"), EAbilityConfigParameter::Speed},
                           {TEXT("Set.HomingTurnRate"), EAbilityConfigParameter::HomingTurnRate},
                           {TEXT("Set.Motion"), EAbilityConfigParameter::Motion},
                           {TEXT("Set.ImpactPolicy"), EAbilityConfigParameter::ImpactPolicy},
                           {TEXT("Set.ImpactLimit"), EAbilityConfigParameter::ImpactLimit},
                           {TEXT("Set.DamageFalloff"), EAbilityConfigParameter::DamageFalloff},
                           {TEXT("Set.TransferRadius"), EAbilityConfigParameter::TransferRadius},
                           {TEXT("Set.MaxTransfers"), EAbilityConfigParameter::MaxTransfers},
                           {TEXT("Set.StackThreshold"), EAbilityConfigParameter::StackThreshold},
                           {TEXT("Set.StackLifetime"), EAbilityConfigParameter::StackLifetime},
                           {TEXT("Set.DetonationDelay"), EAbilityConfigParameter::DetonationDelay},
                           {TEXT("Set.MoveDistance"), EAbilityConfigParameter::MoveDistance},
                           {TEXT("Set.SafeDistance"), EAbilityConfigParameter::SafeDistance},
                           {TEXT("Set.PelletTargetPolicy"), EAbilityConfigParameter::PelletTargetPolicy}};
    for (const auto& Case : Cases)
    {
        const auto* Metadata = AbilityRuntimeModifierTemplates::FindParameterTemplate(Case.Id);
        if (TestNotNull(FString(Case.Id) + TEXT(" has trusted panel metadata"), Metadata))
        {
            TestTrue(TEXT("Metadata has the precise typed axis rather than an arbitrary field"),
                     Metadata->Parameter == Case.Parameter);
            TestTrue(TEXT("Metadata bounds and UI step are finite and ordered"),
                     FMath::IsFinite(Metadata->Minimum) && FMath::IsFinite(Metadata->Maximum) &&
                         Metadata->Minimum <= Metadata->Maximum && FMath::IsFinite(Metadata->Step) &&
                         Metadata->Step > 0.f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9RemainingScopeParameters,
                                 "test_25d.DynamicMage.Task9.Remaining.ActualScopeTypedParameterTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9RemainingScopeParameters::RunTest(const FString&)
{
    using namespace AbilityRuntimeTask9RemainingTests;
    struct FCase
    {
        const TCHAR* Kind;
        const TCHAR* Modifier;
        float Value;
        bool bFlight;
    };
    const FCase Cases[] = {{TEXT("Field"), TEXT("Set.Duration"), 3.f, false},
                           {TEXT("Burst"), TEXT("Set.Radius"), 450.f, false},
                           {TEXT("Ring"), TEXT("Set.InnerRadius"), 50.f, false},
                           {TEXT("Line"), TEXT("Set.Width"), 200.f, false},
                           {TEXT("Ring"), TEXT("Set.HalfHeight"), 200.f, false},
                           {TEXT("Projectile"), TEXT("Set.Speed"), 1100.f, false},
                           {TEXT("Homing"), TEXT("Set.HomingTurnRate"), 90.f, false},
                           {TEXT("Piercing"), TEXT("Set.DamageFalloff"), .5f, false},
                           {TEXT("Projectile"), TEXT("Set.Motion"), 1.f, true},
                           {TEXT("Projectile"), TEXT("Set.ImpactPolicy"), 1.f, true},
                           {TEXT("Projectile"), TEXT("Set.ImpactLimit"), 3.f, true}};
    for (const auto& Case : Cases)
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const FString Path = FString::Printf(
            TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_%s.GA_Prototype_%s_C"),
            Case.Kind, Case.Kind);
        const auto* Row = F.Definition(Path, Error);
        if (!TestNotNull(TEXT("Actual selectable asset has a default definition"), Row) ||
            !TestTrue(TEXT("Actual directory preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Case.bFlight ? Node.TypeId == TEXT("Delivery.PrototypeFlight")
                                    : Node.NodeId == Row->Base.DefaultGraph->EntryNodeId;
            });
        if (!TestTrue(TEXT("The actual consumed scope or independent flight axis exists"), Index != INDEX_NONE))
        {
            return false;
        }
        const FString Before = F.Developer->GetDeveloperState().ContentHash;
        const auto Request = Edit(F, *Row, Index, Case.Modifier, Case.Value);
        const bool Accepted = F.Developer->ApplyEdit(Request, Error);
        AddInfo(Path + TEXT(" | ") + Case.Modifier + TEXT(" | ") + Error);
        TestTrue(TEXT("The trusted parser accepts this actually consumed typed parameter"), Accepted);
        if (Accepted)
        {
            TestTrue(TEXT("A new private snapshot is published"),
                     F.Developer->GetDeveloperState().ContentHash != Before);
            const auto Revision = F.Developer->GetDeveloperState().Revision;
            auto Invalid = Request;
            Invalid.ExpectedRevision = Revision;
            Invalid.Value = std::numeric_limits<float>::quiet_NaN();
            TestFalse(TEXT("Nonfinite input is rejected transactionally"), F.Developer->ApplyEdit(Invalid, Error));
            TestEqual(TEXT("Failed edit keeps the accepted revision"), F.Developer->GetDeveloperState().Revision,
                      Revision);
        }
        F.Developer->EndPreview(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9RegisteredPortFactories,
                                 "test_25d.DynamicMage.Task9.Remaining.ActualRegisteredFragmentsAndExactPorts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9RegisteredPortFactories::RunTest(const FString&)
{
    using namespace AbilityRuntimeTask9RemainingTests;
    for (FName Id : {FName(TEXT("Target.Petrify")), FName(TEXT("Area.Petrify")), FName(TEXT("Target.Slow"))})
    {
        TestTrue(TEXT("Trusted fragment metadata names a concrete server factory: ") + Id.ToString(),
                 AbilityRuntimeModifierTemplates::GetFragmentIds().Contains(Id));
    }
    struct FCase
    {
        const TCHAR* Path;
        const TCHAR* Node;
        const TCHAR* Port;
        const TCHAR* Fragment;
    };
    const FCase Cases[] = {
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Field.GA_Prototype_Field_C"),
         TEXT("Control.FieldCadence"), TEXT("Period"), TEXT("Area.Petrify")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Delay.GA_Prototype_Delay_C"),
         TEXT("Control.PrototypeDelay"), TEXT("Expired"), TEXT("Area.Petrify")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Trap.GA_Prototype_Trap_C"),
         TEXT("Control.PrototypeTrap"), TEXT("Action"), TEXT("Area.Petrify")},
        {TEXT("/Game/UnifiedAbilities/MageBatch/GA_ArcaneMeditation.GA_ArcaneMeditation_C"), TEXT("Status.OwnedEffect"),
         TEXT("Applied"), TEXT("Area.Petrify")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C"),
         TEXT("Query.ShardTargets"), TEXT("Targets"), TEXT("Target.Petrify")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"),
         TEXT("Delivery.ProjectileAwait"), TEXT("Hit"), TEXT("Target.Slow")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute.GA_Electrocute_C"),
         TEXT("Channel.ElectrocuteHost"), TEXT("Period"), TEXT("Target.Petrify")}};
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    for (const auto& Case : Cases)
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        const auto* Row = F.Definition(Case.Path, Error);
        if (!TestNotNull(TEXT("Exact actual preview row is available"), Row) ||
            !TestTrue(TEXT("Actual preview begins without manual base installation"),
                      F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.TypeId == Case.Node;
            });
        if (!TestTrue(TEXT("Actual base contains the selected semantic node"), Index != INDEX_NONE))
        {
            return false;
        }
        const auto* Descriptor = Registry.Find(Case.Node);
        TestTrue(TEXT("The exact requested port is actually declared by the registered strategy"),
                 Descriptor && Descriptor->Ports.ContainsByPredicate(
                                   [&](const FAbilityPortDescriptor& Port)
                                   {
                                       return Port.Name == Case.Port;
                                   }));
        auto Request = Edit(F, *Row, Index, TEXT("Append.Registered"), 1.5f);
        Request.FragmentId = Case.Fragment;
        Request.OutputPortId = Case.Port;
        Request.Radius = 600.f;
        const bool Accepted = F.Developer->ApplyEdit(Request, Error);
        AddInfo(FString(Case.Path) + TEXT(" | ") + Case.Node + TEXT(".") + Case.Port + TEXT(" -> ") + Case.Fragment +
                TEXT(" | ") + Error);
        TestTrue(TEXT("Registered typed fragment attaches transactionally to that exact actual port"), Accepted);
        if (Accepted)
        {
            const auto Revision = F.Developer->GetDeveloperState().Revision;
            const auto Hash = F.Developer->GetDeveloperState().ContentHash;
            Request.ExpectedRevision = Revision;
            Request.OutputPortId = TEXT("Unregistered.Port");
            TestFalse(TEXT("An arbitrary output name cannot attach a branch"), F.Developer->ApplyEdit(Request, Error));
            TestEqual(TEXT("Bad port keeps previous private recipe"), F.Developer->GetDeveloperState().ContentHash,
                      Hash);
            Request.OutputPortId = Case.Port;
            Request.FragmentId = TEXT("Unregistered.ClassOrAsset");
            TestFalse(TEXT("An arbitrary class/asset string cannot be a fragment"),
                      F.Developer->ApplyEdit(Request, Error));
            TestEqual(TEXT("Bad fragment keeps previous revision"), F.Developer->GetDeveloperState().Revision,
                      Revision);
        }
        F.Developer->EndPreview(Error);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9ExtensionTypedParameters,
                                 "test_25d.DynamicMage.Task9.Remaining.ActualExtensionScopeTypedParameterTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9ExtensionTypedParameters::RunTest(const FString&)
{
    using namespace AbilityRuntimeTask9RemainingTests;
    struct FCase
    {
        EExtensionKind Behavior;
        const TCHAR* Modifier;
        float Value;
    };
    const FCase Cases[] = {{EExtensionKind::Attach, TEXT("Set.TransferRadius"), 450.f},
                           {EExtensionKind::Attach, TEXT("Set.MaxTransfers"), 3.f},
                           {EExtensionKind::Stack, TEXT("Set.StackThreshold"), 4.f},
                           {EExtensionKind::Stack, TEXT("Set.StackLifetime"), 3.f},
                           {EExtensionKind::Stack, TEXT("Set.DetonationDelay"), .2f},
                           {EExtensionKind::Displace, TEXT("Set.MoveDistance"), 250.f},
                           {EExtensionKind::Displace, TEXT("Set.SafeDistance"), 80.f}};
    for (const auto& Case : Cases)
    {
        FAbilityActualMageFrontendFixture F;
        FString Error;
        if (!TestTrue(TEXT("The actual three mage catalogs initialize"),
                      F.Developer->InitializeCatalog(nullptr, Error)))
        {
            return false;
        }
        int32 Index = INDEX_NONE;
        const auto* Row = F.Developer->GetDefinitions().FindByPredicate(
            [&](const FAbilityRuntimeDeveloperDefinition& Candidate)
            {
                if (!Candidate.Base.bSelectable || !Candidate.Base.DefaultGraph)
                {
                    return false;
                }
                Index = Candidate.Base.DefaultGraph->Nodes.IndexOfByPredicate(
                    [&](const FAbilityGraphNode& Node)
                    {
                        const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
                        const auto* Extension = Scope ? Cast<UExtensionNode>(Scope->Parameters) : nullptr;
                        return Extension && Scope->Profile == TEXT("PrototypeFine.Extension.v1") &&
                               Extension->Behavior == Case.Behavior;
                    });
                return Index != INDEX_NONE;
            });
        if (!TestNotNull(TEXT("An exact actual Extension root/child Scope exists"), Row) ||
            !TestTrue(TEXT("The actual directory preview starts"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const FString BaseHash = AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph);
        const auto Request = Edit(F, *Row, Index, Case.Modifier, Case.Value);
        const bool Accepted = F.Developer->ApplyEdit(Request, Error);
        AddInfo(GetPathNameSafe(Row->Base.AbilityClass.Get()) + TEXT(" | Scope=") + FString::FromInt(Index) +
                TEXT(" | ") + Case.Modifier + TEXT(" | ") + Error);
        TestTrue(TEXT("Trusted parser accepts only that concrete Extension behavior's consumed axis"), Accepted);
        TestEqual(TEXT("The shared asset/default graph is never modified by the transaction"),
                  AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), BaseHash);
        if (Accepted)
        {
            const auto Revision = F.Developer->GetDeveloperState().Revision;
            auto Unsupported = Request;
            Unsupported.ExpectedRevision = Revision;
            Unsupported.ModifierId = TEXT("Set.Motion");
            Unsupported.Value = 1.f;
            TestFalse(TEXT("Extension State scope cannot masquerade as a projectile motion config"),
                      F.Developer->ApplyEdit(Unsupported, Error));
            TestEqual(TEXT("Unsupported orthogonal axis keeps accepted revision"),
                      F.Developer->GetDeveloperState().Revision, Revision);
        }
        F.Developer->EndPreview(Error);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeTask9TrustedForkedTemplate,
                                 "test_25d.DynamicMage.Task9.Remaining.ActualFireForkedTemplateHasExplicitBranchBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeTask9TrustedForkedTemplate::RunTest(const FString&)
{
    using namespace AbilityRuntimeTask9RemainingTests;
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"), Error);
    if (!TestNotNull(TEXT("Exact original FireBolt directory row exists"), Row) ||
        !TestTrue(TEXT("Actual preview begins without base substitution"),
                  F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        return false;
    }
    const int32 Index = Row->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Delivery.ProjectileAwait");
        });
    if (!TestTrue(TEXT("Actual default exposes the physical delivery axis"), Index != INDEX_NONE))
    {
        return false;
    }
    auto Request = Edit(F, *Row, Index, TEXT("Propagation.Forked"), 1.f);
    Request.Count = 6;
    Request.Interval = .1f;
    Request.Radius = 450.f;
    Request.Falloff = .8f;
    Request.MaxDepth = 3;
    Request.BranchesPerTarget = 2;
    if (!TestTrue(TEXT("Trusted Forked template accepts explicit bounded BFS axes"),
                  F.Developer->ApplyEdit(Request, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot Snapshot;
    if (!TestTrue(TEXT("Actual selected frontend receives its accepted parsed recipe"),
                  F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Snapshot, Error)))
    {
        return false;
    }
    const auto* Forked = Cast<UAbilityForkedPropagationNodeConfig>(Snapshot.Graph->Nodes[Index].Config);
    if (!TestNotNull(TEXT("The parser selected the real physical Forked strategy"), Forked))
    {
        return false;
    }
    TestEqual(TEXT("Total target budget is explicit"), Forked->MaxTargets, 6);
    TestEqual(TEXT("Maximum BFS depth is not hidden in Damage Value"), Forked->MaxDepth, 3);
    TestEqual(TEXT("Branches per target remains a separate bound"), Forked->BranchesPerTarget, 2);
    TestTrue(TEXT("Original native frame and one-time equipment preparation are retained"),
             Forked->bUseNativeLaunchParameters);
    Request.ExpectedRevision = F.Developer->GetDeveloperState().Revision;
    const auto Before = F.Developer->GetDeveloperState();
    Request.BranchesPerTarget = 99;
    TestFalse(TEXT("Invalid branch fanout is rejected before any cost or launch"),
              F.Developer->ApplyEdit(Request, Error));
    TestEqual(TEXT("Failure preserves the accepted recipe hash"), F.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    F.Developer->EndPreview(Error);
    return true;
}
#endif
