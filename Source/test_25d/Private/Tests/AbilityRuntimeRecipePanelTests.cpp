// 调用链：Automation -> 真实 Mage 目录 -> 开发组件可信编辑入口 -> 原 ASC Spec/配方快照。
// 覆盖目录遗漏、非法编辑污染、错误回收与玩家串配方；不把 NullRHI 测试当作 UMG/动画视觉验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Player/MyPlayerState.h"
#include "MyGameplayTags.h"
#include <limits>

namespace AbilityRuntimeRecipePanelTests
{
    /** 服务器玩家状态与 ASC 均为正式类型；不激活 AI/HUD，也不写任何目录或存档。 */
    struct FPlayer
    {
        AMyPlayerState* State = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;

        explicit FPlayer(FAbilityGraphTestFixture& F)
        {
            State = F.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->InitAbilityActorInfo(State, F.Source);
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
            if (!Developer)
            {
                Developer = NewObject<UAbilityRuntimeRecipeDeveloperComponent>(State);
                State->AddInstanceComponent(Developer);
                Developer->RegisterComponent();
            }
        }
    };

    /** 选用实际石化的查询/状态默认定义，开发工具测试不通过手工 RegisterBase 掩盖目录导出问题。 */
    const FAbilityRuntimeDeveloperDefinition* Petrify(const UAbilityRuntimeRecipeDeveloperComponent& Component)
    {
        return Component.GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Item)
            {
                return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                       TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");
            });
    }

    /** 编辑请求使用客户端实际可见的基础摘要及当前版本，服务器仍须逐项重新校验。 */
    FAbilityRuntimeDeveloperEdit RadiusEdit(const UAbilityRuntimeRecipeDeveloperComponent& Component, float Scale)
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.DefinitionId = Component.GetDeveloperState().SelectedId;
        Edit.ModifierId = TEXT("Scale.Radius");
        Edit.NodeIndex = 0;
        Edit.Value = Scale;
        Edit.ExpectedRevision = Component.GetDeveloperState().Revision;
        Edit.ExpectedBaseHash = Component.GetDeveloperState().BaseHash;
        return Edit;
    }

    /** 读取真正下游配置，不以 UI 的 Value 字段代替编译结果。 */
    float Radius(const UAbilityGraphDefinition* Graph)
    {
        if (Graph)
        {
            for (const auto& Node : Graph->Nodes)
            {
                if (const auto* Config = Cast<UAbilityPetrifyAreaQueryConfig>(Node.Config))
                {
                    return Config->Radius;
                }
            }
        }
        return -1.f;
    }

    /** 外部来源走同一正式配方 API；用于确认关闭面板不会回滚别的系统后续写入。 */
    FAbilityRuntimeRecipeContribution ExternalRadius(FName Source, float Scale)
    {
        FAbilityRuntimeRecipeContribution Result;
        Result.SourceId = Source;
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation = EAbilityRecipePatchOperation::RadiusScale;
        Patch.TargetTypeId = TEXT("Query.PetrifyArea");
        Patch.Scalar = Scale;
        Result.Patches.Add(Patch);
        return Result;
    }
} // namespace AbilityRuntimeRecipePanelTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperCatalog,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.CatalogAndStableIds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperCatalog::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer One(F);
    FPlayer Two(F);
    FString Error;
    if (!TestTrue(TEXT("Developer reads full actual mage catalog"), One.Developer->InitializeCatalog(nullptr, Error)) ||
        !TestTrue(TEXT("Second owner independently reads same immutable catalog"),
                  Two.Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TSet<FString> Expected;
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                              TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                              TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
    {
        auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!TestNotNull(TEXT("Original catalog remains loadable"), Catalog))
        {
            return false;
        }
        for (const auto& Item : Catalog->AbilityInformation)
        {
            if (Item.RequiredProfession == EPlayerProfession::Mage && Item.Ability)
            {
                Expected.Add(Item.AbilityTag.ToString() + TEXT("|") + Item.Ability->GetPathName());
            }
        }
    }
    TestEqual(TEXT("Panel retains every actual concrete mage frontend"), One.Developer->GetDefinitions().Num(),
              Expected.Num());
    TSet<FName> Ids;
    int32 Protected = 0;
    for (const auto& Item : One.Developer->GetDefinitions())
    {
        const FString Key = Item.Base.AbilityTag.ToString() + TEXT("|") + Item.Base.AbilityClass->GetPathName();
        TestTrue(TEXT("No nonmage class is injected"), Expected.Contains(Key));
        TestFalse(TEXT("Every server lookup ID is nonempty"), Item.DefinitionId.IsNone());
        TestFalse(TEXT("Variants never collide"), Ids.Contains(Item.DefinitionId));
        Ids.Add(Item.DefinitionId);
        const auto* Other = Two.Developer->GetDefinitions().FindByPredicate(
            [&Item](const FAbilityRuntimeDeveloperDefinition& Candidate)
            {
                return Candidate.DefinitionId == Item.DefinitionId;
            });
        if (TestNotNull(TEXT("IDs survive independent owner object names"), Other))
        {
            TestEqual(TEXT("Stable ID resolves exactly the same class"), Other->Base.AbilityClass.Get(),
                      Item.Base.AbilityClass.Get());
        }
        if (!Item.Base.bSelectable)
        {
            ++Protected;
            TestFalse(TEXT("Protected base cannot be temporarily granted"),
                      One.Developer->BeginPreview(Item.DefinitionId, Error));
        }
    }
    TestEqual(TEXT("Existing listener and placeholder passives stay visible but protected"), Protected, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperValidation,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.AuthoritativeValidationAndRollback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperValidation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Real petrification definition exists"), Definition) ||
        !TestTrue(TEXT("Server accepts trusted real base"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto Before = Player.Developer->GetDeveloperState();
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestFalse(TEXT("Client cannot provide arbitrary class/path as ID"),
              Player.Developer->BeginPreview(TEXT("/Game/NotRegistered/Class"), Error));
    const auto OriginalEdit = RadiusEdit(*Player.Developer, 2.f);
    for (const float Invalid :
         {-1.f, 0.f, 17.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        auto Edit = OriginalEdit;
        Edit.Value = Invalid;
        TestFalse(TEXT("Nonfinite/out-of-bounds radius is refused"), Player.Developer->ApplyEdit(Edit, Error));
        TestFalse(TEXT("Refused request includes useful diagnostic"), Error.IsEmpty());
    }
    auto Invalid = OriginalEdit;
    Invalid.ModifierId = TEXT("Reflection.ArbitraryProperty");
    TestFalse(TEXT("Unknown operation is not reflected"), Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    Invalid.NodeIndex = 99999;
    TestFalse(TEXT("Out-of-range node cannot target another object"), Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    ++Invalid.ExpectedRevision;
    TestFalse(TEXT("Version mismatch cannot edit the wrong cast definition"),
              Player.Developer->ApplyEdit(Invalid, Error));
    Invalid = OriginalEdit;
    Invalid.ExpectedBaseHash = TEXT("stale-base");
    TestFalse(TEXT("Index requires matching base hash"), Player.Developer->ApplyEdit(Invalid, Error));
    TestEqual(TEXT("Every invalid request leaves accepted revision untouched"),
              Player.Developer->GetDeveloperState().Revision, Before.Revision);
    TestEqual(TEXT("Invalid requests preserve compiled content"), Player.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    TestTrue(TEXT("Invalid requests preserve the exact temporary grant"),
             Player.Developer->GetPreviewHandle() == Handle);
    if (TestTrue(TEXT("Bounded typed edit is accepted"), Player.Developer->ApplyEdit(OriginalEdit, Error)))
    {
        FAbilityRuntimeRecipeSnapshot Snapshot;
        TestTrue(TEXT("Production snapshot is available"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
        TestEqual(TEXT("Real radius config doubles"), Radius(Snapshot.Graph),
                  Radius(Definition->Base.DefaultGraph) * 2.f);
        TestFalse(TEXT("Already consumed revision cannot be replayed"),
                  Player.Developer->ApplyEdit(OriginalEdit, Error));
    }
    TestTrue(TEXT("Preview can be cleaned up after invalid edits"), Player.Developer->EndPreview(Error));
    Player.State->Profession = EPlayerProfession::Stander;
    TestFalse(TEXT("Nonmage owner cannot bypass profession through RPC"),
              Player.Developer->BeginPreview(Definition->DefinitionId, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperRestoration,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.TemporaryGrantAndExternalSourceRestoration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperRestoration::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Original petrify variant exists"), Definition))
    {
        return false;
    }
    const auto Tag = Definition->Base.AbilityTag;
    auto* OriginalBase = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(OriginalBase->Nodes[0].Config)->Radius = 111.f;
    TestTrue(TEXT("Existing player recipe is registered"), Player.Developer->RegisterBase(Tag, OriginalBase, Error));
    TestTrue(TEXT("Existing equipment-like source is accepted"),
             Player.Developer->SetContribution(Tag, ExternalRadius(TEXT("External.Before"), 1.5f), Error));
    FGameplayAbilitySpec Original(Definition->Base.AbilityClass, 4, INDEX_NONE, F.Target);
    Original.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_LMB);
    Original.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
    const auto OriginalHandle = Player.ASC->GiveAbility(Original);
    const auto OriginalTags = Player.ASC->FindAbilitySpecFromHandle(OriginalHandle)->DynamicAbilityTags;
    const int32 Count = Player.ASC->GetActivatableAbilities().Num();
    if (!TestTrue(TEXT("Preview starts without borrowing original spec"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto PreviewHandle = Player.Developer->GetPreviewHandle();
    TestTrue(TEXT("Preview owns a new independent handle"), PreviewHandle.IsValid() && PreviewHandle != OriginalHandle);
    TestTrue(TEXT("Only new handle is owned by developer"), Player.Developer->OwnsTemporaryAbility(PreviewHandle));
    TestFalse(TEXT("Permanent handle is never claimed"), Player.Developer->OwnsTemporaryAbility(OriginalHandle));
    TestEqual(TEXT("Exactly one temporary spec is added"), Player.ASC->GetActivatableAbilities().Num(), Count + 1);
    TestTrue(TEXT("Original input/status tags stay byte-equivalent"),
             Player.ASC->FindAbilitySpecFromHandle(OriginalHandle)->DynamicAbilityTags == OriginalTags);
    TestTrue(TEXT("Preview edit is applied"), Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    TestTrue(TEXT("Another system can add a later source"),
             Player.Developer->SetContribution(Tag, ExternalRadius(TEXT("External.During"), 1.1f), Error));
    TestTrue(TEXT("Close merges outside sources while restoring its former base"), Player.Developer->EndPreview(Error));
    TestNull(TEXT("Preview grant is removed"), Player.ASC->FindAbilitySpecFromHandle(PreviewHandle));
    const auto* Restored = Player.ASC->FindAbilitySpecFromHandle(OriginalHandle);
    if (TestNotNull(TEXT("Original permanent grant remains"), Restored))
    {
        TestEqual(TEXT("Original level is preserved"), Restored->Level, 4);
        TestTrue(TEXT("Original input and status are preserved"), Restored->DynamicAbilityTags == OriginalTags);
        TestEqual(TEXT("Original source ownership is preserved"), Restored->SourceObject.Get(),
                  static_cast<UObject*>(F.Target));
    }
    TestEqual(TEXT("Spec count exactly returns to original"), Player.ASC->GetActivatableAbilities().Num(), Count);
    FAbilityRuntimeRecipeSnapshot After;
    TestTrue(TEXT("Restored production recipe remains usable"),
             Player.Developer->CreateCastSnapshot(Tag, F.World, After, Error));
    TestTrue(TEXT("Both outside sources survive and own preview multiplier is gone"),
             FMath::IsNearlyEqual(Radius(After.Graph), 111.f * 1.5f * 1.1f));
    TestTrue(TEXT("Repeated close is idempotent"), Player.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperIsolation,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.OwnerIsolationAndBaseConflict",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperIsolation::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer One(F);
    FPlayer Two(F);
    FString Error;
    if (!TestTrue(TEXT("First catalog initializes"), One.Developer->InitializeCatalog(nullptr, Error)) ||
        !TestTrue(TEXT("Second catalog initializes"), Two.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*One.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("First owner starts"), One.Developer->BeginPreview(Definition->DefinitionId, Error)) ||
        !TestTrue(TEXT("Second owner starts"), Two.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto OtherState = Two.Developer->GetDeveloperState();
    const auto OtherHandle = Two.Developer->GetPreviewHandle();
    TestTrue(TEXT("First player accepts its own change"),
             One.Developer->ApplyEdit(RadiusEdit(*One.Developer, 2.f), Error));
    TestEqual(TEXT("Other player's compiled hash does not change"), Two.Developer->GetDeveloperState().ContentHash,
              OtherState.ContentHash);
    TestEqual(TEXT("Other player's revision does not change"), Two.Developer->GetDeveloperState().Revision,
              OtherState.Revision);
    auto* ChangedBase = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(ChangedBase->Nodes[0].Config)->Radius = 777.f;
    TestTrue(TEXT("External authoritative base can change"),
             One.Developer->RegisterBase(Definition->Base.AbilityTag, ChangedBase, Error));
    TestFalse(TEXT("Closing must not overwrite a base changed by another owner"), One.Developer->EndPreview(Error));
    TestFalse(TEXT("Conflict has a visible diagnostic"), Error.IsEmpty());
    FAbilityRuntimeRecipeSnapshot Snapshot;
    TestTrue(TEXT("External base is still intact"),
             One.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
    TestTrue(TEXT("External base is not overwritten by old saved base"), Radius(Snapshot.Graph) >= 777.f);
    TestTrue(TEXT("Other player's temporary handle still exists"),
             Two.ASC->FindAbilitySpecFromHandle(OtherHandle) != nullptr);
    TestTrue(TEXT("Other player closes independently"), Two.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperExternalReceipt,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.ExternalRevisionReceipt",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperExternalReceipt::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    UAbilityRuntimeRecipeComponent* ProductionAPI = Player.Developer;
    TestTrue(TEXT("Other subsystem edits through the production base API"),
             ProductionAPI->SetContribution(Definition->Base.AbilityTag, ExternalRadius(TEXT("Outside.Event"), 1.5f),
                                            Error));
    TestEqual(TEXT("Owner receipt immediately reflects outside successful revision"),
              Player.Developer->GetDeveloperState().Revision, ProductionAPI->GetRevision(Definition->Base.AbilityTag));
    FAbilityRuntimeRecipeSnapshot Snapshot;
    TestTrue(TEXT("Authoritative compiled snapshot is available"),
             ProductionAPI->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
    TestEqual(TEXT("Owner receipt immediately reflects outside compiled content"),
              Player.Developer->GetDeveloperState().ContentHash, Snapshot.ContentHash);
    TestTrue(TEXT("Next panel edit can use its refreshed revision"),
             Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    TestTrue(TEXT("Close restores around outside edit"), Player.Developer->EndPreview(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperAbandon,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.AbandonConflictKeepsExternalBase",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperAbandon::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestTrue(TEXT("Own preview source is accepted"),
             Player.Developer->ApplyEdit(RadiusEdit(*Player.Developer, 2.f), Error));
    auto* External = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(External->Nodes[0].Config)->Radius = 333.f;
    TestTrue(TEXT("Another system replaces base"),
             Player.Developer->RegisterBase(Definition->Base.AbilityTag, External, Error));
    TestTrue(TEXT("Another system owns its later modifier"),
             Player.Developer->SetContribution(Definition->Base.AbilityTag, ExternalRadius(TEXT("Outside.Keep"), 1.5f),
                                               Error));
    TestFalse(TEXT("Normal restore explicitly refuses overwrite"), Player.Developer->EndPreview(Error));
    if (TestTrue(TEXT("Explicit abandonment permits exit after conflict"), Player.Developer->AbandonPreview(Error)))
    {
        TestFalse(TEXT("Preview is no longer active"), Player.Developer->GetDeveloperState().bPreviewActive);
        TestNull(TEXT("Exact temporary grant is removed"), Player.ASC->FindAbilitySpecFromHandle(Handle));
        FAbilityRuntimeRecipeSnapshot Snapshot;
        TestTrue(TEXT("External recipe remains usable"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Snapshot, Error));
        TestEqual(TEXT("External base and modifier survive; preview multiplier is gone"), Radius(Snapshot.Graph),
                  499.5f);
        TestTrue(TEXT("Repeated abandon is idempotent"), Player.Developer->AbandonPreview(Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperWrongSelection,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.SelectionMustMatchActiveDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperWrongSelection::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    auto Edit = RadiusEdit(*Player.Developer, 2.f);
    Edit.DefinitionId = TEXT("A-different-visible-dropdown-row");
    const auto Before = Player.Developer->GetDeveloperState();
    TestFalse(TEXT("UI selection changed without Begin cannot edit previously active definition"),
              Player.Developer->ApplyEdit(Edit, Error));
    TestEqual(TEXT("Rejected selection preserves accepted graph"), Player.Developer->GetDeveloperState().ContentHash,
              Before.ContentHash);
    TestEqual(TEXT("Rejected selection preserves version"), Player.Developer->GetDeveloperState().Revision,
              Before.Revision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeveloperCloseAck,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.CloseAcknowledgesExactRequest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperCloseAck::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Petrify(*Player.Developer);
    if (!TestNotNull(TEXT("Actual base exists"), Definition) ||
        !TestTrue(TEXT("Preview starts"), Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        return false;
    }
    const auto Handle = Player.Developer->GetPreviewHandle();
    TestFalse(TEXT("An older request leaves a prior error"),
              Player.Developer->BeginPreview(TEXT("not-registered"), Error));
    const FGuid Request = FGuid::NewGuid();
    FGuid Acknowledged;
    bool bSucceeded = false;
    int32 Replies = 0;
    Player.Developer->OnPreviewCloseAcknowledged.AddLambda(
        [&](FGuid Id, bool bSuccess)
        {
            Acknowledged = Id;
            bSucceeded = bSuccess;
            ++Replies;
        });
    Player.Developer->ServerClosePreview(Request, false);
    TestEqual(TEXT("Exactly one reliable close result is produced"), Replies, 1);
    TestTrue(TEXT("Reply preserves the caller's exact request ID"), Acknowledged == Request);
    TestTrue(TEXT("Prior unrelated error cannot convert successful close to failure"), bSucceeded);
    TestNull(TEXT("Acknowledged success corresponds to actual grant removal"),
             Player.ASC->FindAbilitySpecFromHandle(Handle));
    TestFalse(TEXT("Acknowledged snapshot reports inactive preview"),
              Player.Developer->GetDeveloperState().bPreviewActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRuntimeRecipeDeveloperTypedTemplates,
    "test_25d.UnifiedGA.DynamicMage.Developer.TypedPropagationAndPayloadTemplatesAreTransactional",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeveloperTypedTemplates::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipePanelTests;
    FAbilityGraphTestFixture F;
    FPlayer Player(F);
    FString Error;
    if (!TestTrue(TEXT("Actual catalog initializes"), Player.Developer->InitializeCatalog(nullptr, Error)))
    {
        return false;
    }
    const auto* Definition = Player.Developer->GetDefinitions().FindByPredicate(
        [](const FAbilityRuntimeDeveloperDefinition& Item)
        {
            return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                   TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C");
        });
    if (!TestNotNull(TEXT("Original native FireBolt frontend exists"), Definition) ||
        !TestTrue(TEXT("Exact temporary preview starts"),
                  Player.Developer->BeginPreview(Definition->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeDeveloperEdit Edit;
    Edit.DefinitionId = Definition->DefinitionId;
    Edit.ExpectedBaseHash = Definition->BaseHash;
    Edit.ModifierId = TEXT("Propagation.Sequential");
    Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Delivery.ProjectileAwait");
        });
    if (!TestTrue(TEXT("Actual default has an independent real delivery axis"), Edit.NodeIndex != INDEX_NONE))
    {
        return false;
    }
    Edit.Count = 3;
    Edit.Interval = .2f;
    Edit.Radius = 420.f;
    Edit.Falloff = .75f;
    Edit.ExpectedRevision = Player.Developer->GetDeveloperState().Revision;
    if (!TestTrue(TEXT("Trusted template replaces delivery without a preauthored combined GA"),
                  Player.Developer->ApplyEdit(Edit, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityRuntimeRecipeSnapshot First;
    TestTrue(TEXT("Edited runtime graph is available to the actual frontend"),
             Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, First, Error));
    const auto* Propagation = Cast<UAbilityPropagationNodeConfig>(First.Graph->Nodes[Edit.NodeIndex].Config);
    if (TestNotNull(TEXT("Replacement uses the registered independent propagation config"), Propagation))
    {
        TestTrue(TEXT("Original immutable native launch parameters are retained"),
                 Propagation->bUseNativeLaunchParameters);
        TestEqual(TEXT("Count is explicit"), Propagation->MaxTargets, 3);
        TestEqual(TEXT("Delay is explicit"), Propagation->JumpDelay, .2f);
        TestEqual(TEXT("Radius is explicit"), Propagation->JumpRadius, 420.f);
        TestEqual(TEXT("Falloff is explicit"), Propagation->DamageFalloff, .75f);
    }
    const auto Accepted = Player.Developer->GetDeveloperState();
    for (const int32 InvalidCount : {0, 65})
    {
        Edit.Count = InvalidCount;
        Edit.ExpectedRevision = Accepted.Revision;
        TestFalse(TEXT("Out-of-bound template request is rejected"), Player.Developer->ApplyEdit(Edit, Error));
        TestEqual(TEXT("Failed edit retains accepted graph"), Player.Developer->GetDeveloperState().ContentHash,
                  Accepted.ContentHash);
    }
    Edit.Count = 3;
    Edit.ModifierId = TEXT("Append.Status.Petrify");
    Edit.NodeIndex = Definition->Base.DefaultGraph->Nodes.IndexOfByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Effect.InputDamage");
        });
    Edit.Value = 1.25f;
    Edit.ExpectedRevision = Accepted.Revision;
    if (TestTrue(TEXT("A separate payload template composes with the existing propagation axis"),
                 Player.Developer->ApplyEdit(Edit, Error)))
    {
        FAbilityRuntimeRecipeSnapshot Second;
        TestTrue(TEXT("New recipe snapshot compiles"),
                 Player.Developer->CreateCastSnapshot(Definition->Base.AbilityTag, F.World, Second, Error));
        TestTrue(TEXT("Both independent axes exist in the runtime graph"),
                 Second.Graph->Nodes.ContainsByPredicate(
                     [](const FAbilityGraphNode& Node)
                     {
                         return Node.TypeId == TEXT("Propagation.Sequential");
                     }) &&
                     Second.Graph->Nodes.ContainsByPredicate(
                         [](const FAbilityGraphNode& Node)
                         {
                             return Node.TypeId == TEXT("Status.Petrify");
                         }));
        TestFalse(TEXT("In-flight prior snapshot is immutable"), First.Graph->Nodes.ContainsByPredicate(
                                                                     [](const FAbilityGraphNode& Node)
                                                                     {
                                                                         return Node.TypeId == TEXT("Status.Petrify");
                                                                     }));
    }
    TestTrue(TEXT("Closing preview restores original recipe ownership"), Player.Developer->EndPreview(Error));
    TestFalse(TEXT("Developer template sources do not leak into the permanent skill"),
              Player.Developer->HasContributions(Definition->Base.AbilityTag));
    return true;
}
#endif
