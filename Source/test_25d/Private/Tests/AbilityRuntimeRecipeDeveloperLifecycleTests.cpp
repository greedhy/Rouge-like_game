// 调用链：真实 PlayerState ASC 重绑/Avatar 倒地/销毁 -> 开发预览生命周期 -> 精确回收临时 Spec/来源/观察订阅。
// 正式同类授予与外部配方必须保留；测试不直接调用清理函数来假装生命周期事件已经接通。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "DungeonPlayerRunTestTypes.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "Interface/CombatInterface.h"
#include "MyGameplayTags.h"
#include "UObject/Script.h"

namespace AbilityRuntimeRecipeDeveloperLifecycleTests
{
    /** 正式角色接口返回同一个 PlayerState ASC，故观察源校验与真正游戏一致。 */
    struct FPreview
    {
        FAbilityGraphTestFixture& F;
        AMyPlayerState* State = nullptr;
        ADungeonPlayerRunTestPawn* Pawn = nullptr;
        UMyAbilitySystemComponent* ASC = nullptr;
        UAbilityRuntimeRecipeDeveloperComponent* Developer = nullptr;
        FGameplayAbilitySpecHandle FormalHandle;
        FGameplayAbilitySpecHandle PreviewHandle;
        FGameplayTag AbilityTag;
        FGameplayTagContainer FormalTags;
        UAbilityGraphInstance* ObservedRun = nullptr;

        explicit FPreview(FAbilityGraphTestFixture& InFixture) : F(InFixture)
        {
            State = F.World->SpawnActor<AMyPlayerState>();
            State->Profession = EPlayerProfession::Mage;
            Pawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
            Pawn->Tags = {TEXT("Player")};
            Pawn->AttachState(State);
            ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
            ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
            Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
        }

        /** 同类正式 Spec 刻意与预览并存，以捕获按类/Tag 错误清除正式授予。 */
        bool Begin(FString& Error)
        {
            if (!Developer || !Developer->InitializeCatalog(nullptr, Error))
            {
                return false;
            }
            const auto* Definition = Developer->GetDefinitions().FindByPredicate(
                [](const FAbilityRuntimeDeveloperDefinition& Row)
                {
                    return GetPathNameSafe(Row.Base.AbilityClass.Get()) ==
                           TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C");
                });
            if (!Definition || !Definition->Base.DefaultGraph)
            {
                Error = TEXT("Real petrification default is unavailable.");
                return false;
            }
            AbilityTag = Definition->Base.AbilityTag;
            auto* Prior = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
            CastChecked<UAbilityPetrifyAreaQueryConfig>(Prior->Nodes[0].Config)->Radius = 222.f;
            if (!Developer->RegisterBase(AbilityTag, Prior, Error) ||
                !Developer->SetContribution(AbilityTag, OutsideRadius(), Error))
            {
                return false;
            }
            FGameplayAbilitySpec Formal(Definition->Base.AbilityClass, 4, INDEX_NONE, State);
            Formal.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().InputTag_LMB);
            Formal.DynamicAbilityTags.AddTag(FMyGameplayTags::Get().Abilities_Status_Equipped);
            FormalHandle = ASC->GiveAbility(Formal);
            FormalTags = ASC->FindAbilitySpecFromHandle(FormalHandle)->DynamicAbilityTags;
            if (!Developer->BeginPreview(Definition->DefinitionId, Error))
            {
                return false;
            }
            PreviewHandle = Developer->GetPreviewHandle();
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.DefinitionId = Developer->GetDeveloperState().SelectedId;
            Edit.ModifierId = TEXT("Scale.Radius");
            Edit.NodeIndex = 0;
            Edit.ExpectedRevision = Developer->GetDeveloperState().Revision;
            Edit.ExpectedBaseHash = Developer->GetDeveloperState().BaseHash;
            Edit.Value = 2.f;
            if (!Developer->ApplyEdit(Edit, Error))
            {
                return false;
            }
            FAbilitySourceToken Source;
            if (!AbilityExecutionIdentity::CaptureSource(Pawn, Source, Error))
            {
                return false;
            }
            auto* Delay = NewObject<UAbilityGraphDefinition>(F.World);
            Delay->SchemaVersion = 2;
            Delay->CompatibilityProfile = TEXT("UnifiedV2");
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = TEXT("Control.DelayOrigin");
            auto* Config = NewObject<UAbilityDelayNodeConfig>(Delay);
            Config->DelaySeconds = 5.f;
            Node.Config = Config;
            Delay->Nodes.Add(Node);
            Delay->EntryNodeId = Node.NodeId;
            ObservedRun = NewObject<UAbilityGraphInstance>(F.World);
            const auto& Snapshot = Developer->GetDeveloperState();
            if (!Developer->ObserveExecution(ObservedRun, Source, AbilityTag, Snapshot.Revision, Snapshot.ContentHash,
                                             Error))
            {
                return false;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Source;
            return ObservedRun->StartWithInput(Delay, Input, {}, Error);
        }

        /** 外部修改来源明确，不使用开发来源前缀；生命周期清理无权移除它。 */
        static FAbilityRuntimeRecipeContribution OutsideRadius()
        {
            FAbilityRuntimeRecipeContribution Result;
            Result.SourceId = TEXT("Equipment.RealOutsidePreview");
            FAbilityRuntimeRecipePatch Patch;
            Patch.Operation = EAbilityRecipePatchOperation::RadiusScale;
            Patch.TargetTypeId = TEXT("Query.PetrifyArea");
            Patch.Scalar = 1.5f;
            Result.Patches.Add(Patch);
            return Result;
        }

        /** 所有断言观察事件后的生产数据；没有调用 EndPreview/Abandon 代替事件接线。 */
        void AssertReleased(FAutomationTestBase& Test, float ExpectedRadius)
        {
            Test.TestFalse(TEXT("Preview flag is cleared immediately"), Developer->GetDeveloperState().bPreviewActive);
            Test.TestNull(TEXT("Only exact preview grant is removed"), ASC->FindAbilitySpecFromHandle(PreviewHandle));
            const auto* Formal = ASC->FindAbilitySpecFromHandle(FormalHandle);
            if (Test.TestNotNull(TEXT("Permanent grant of the same GA class remains"), Formal))
            {
                Test.TestEqual(TEXT("Permanent level remains unchanged"), Formal->Level, 4);
                Test.TestTrue(TEXT("Permanent slot/status remain unchanged"), Formal->DynamicAbilityTags == FormalTags);
                Test.TestEqual(TEXT("Permanent source remains PlayerState"), Formal->SourceObject.Get(),
                               static_cast<UObject*>(State));
            }
            const auto Slot = FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"));
            Test.TestNull(TEXT("Developer input has no stale granted recipient"), ASC->GetSpecWithSlot(Slot));
            Test.TestEqual(TEXT("Old source observations are detached immediately"),
                           Developer->GetObservedExecutionCount(), 0);
            FAbilityRuntimeRecipeSnapshot Current;
            FString Error;
            if (Test.TestTrue(TEXT("Outside accepted recipe is still accessible"),
                              Developer->CreateCastSnapshot(AbilityTag, F.World, Current, Error)))
            {
                const auto* Config = Cast<UAbilityPetrifyAreaQueryConfig>(Current.Graph->Nodes[0].Config);
                if (Test.TestNotNull(TEXT("Real compiled settings remain typed"), Config))
                {
                    Test.TestEqual(TEXT("Outside base/source survive; preview multiplier is removed"), Config->Radius,
                                   ExpectedRadius);
                }
            }
        }
    };
} // namespace AbilityRuntimeRecipeDeveloperLifecycleTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDeveloperAvatarReplacementCleanup,
    "test_25d.UnifiedGA.DynamicMage.DeveloperLifecycle.AvatarReplacementReleasesOnlyPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeveloperAvatarReplacementCleanup::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeDeveloperLifecycleTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    FPreview Preview(F);
    FString Error;
    if (!TestTrue(TEXT("Actual persistent ASC preview is configured"), Preview.Begin(Error)))
    {
        AddInfo(Error);
        return false;
    }
    Preview.ASC->InitAbilityActorInfo(Preview.State, Preview.Pawn);
    TestNotNull(TEXT("Idempotent same-avatar initialization does not close preview"),
                Preview.ASC->FindAbilitySpecFromHandle(Preview.PreviewHandle));
    auto* NewPawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
    NewPawn->Tags = {TEXT("Player")};
    NewPawn->AttachState(Preview.State);
    TestEqual(TEXT("Real ASC now binds the new avatar"), Preview.ASC->GetAvatarActor(), static_cast<AActor*>(NewPawn));
    Preview.AssertReleased(*this, 333.f);
    const int32 Records = Preview.Developer->GetObservations().Num();
    Preview.ObservedRun->Advance(0.f);
    TestEqual(TEXT("Old source effect fails closed after rebind"), Preview.ObservedRun->GetEndReason(),
              EAbilityGraphEndReason::SourceInvalid);
    TestEqual(TEXT("Old source emits no later subscribed observations"), Preview.Developer->GetObservations().Num(),
              Records);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDeveloperDeathConflictCleanup,
    "test_25d.UnifiedGA.DynamicMage.DeveloperLifecycle.DeathKeepsExternalBaseAndReleasesPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeveloperDeathConflictCleanup::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeDeveloperLifecycleTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    FPreview Preview(F);
    FString Error;
    if (!TestTrue(TEXT("Preview begins before actual downed lifecycle"), Preview.Begin(Error)))
    {
        AddInfo(Error);
        return false;
    }
    const auto* Definition = Preview.Developer->GetDefinitions().FindByPredicate(
        [&Preview](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            return Row.DefinitionId == Preview.Developer->GetDeveloperState().SelectedId;
        });
    auto* External = DuplicateObject<UAbilityGraphDefinition>(Definition->Base.DefaultGraph, F.World);
    CastChecked<UAbilityPetrifyAreaQueryConfig>(External->Nodes[0].Config)->Radius = 111.f;
    TestTrue(TEXT("External system replaces base during preview"),
             Preview.Developer->RegisterBase(Preview.AbilityTag, External, Error));
    Preview.Pawn->Die(FVector::ZeroVector);
    TestTrue(TEXT("Actual pawn enters production death/downed state"), ICombatInterface::Execute_IsDead(Preview.Pawn));
    Preview.AssertReleased(*this, 166.5f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDeveloperDestroyedCleanup,
    "test_25d.UnifiedGA.DynamicMage.DeveloperLifecycle.DestroyedAvatarReleasesPreviewImmediately",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeveloperDestroyedCleanup::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeDeveloperLifecycleTests;
    FEditorScriptExecutionGuard Guard;
    FAbilityGraphTestFixture F;
    FPreview Preview(F);
    FString Error;
    if (!TestTrue(TEXT("Preview begins before real Actor destruction"), Preview.Begin(Error)))
    {
        AddInfo(Error);
        return false;
    }
    Preview.Pawn->Destroy();
    Preview.AssertReleased(*this, 333.f);
    TestTrue(TEXT("Persistent owner remains alive"), IsValid(Preview.State));
    return true;
}
#endif
