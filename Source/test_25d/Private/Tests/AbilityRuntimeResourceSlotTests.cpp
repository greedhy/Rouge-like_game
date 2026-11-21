// 调用链：真实 Scope 帧 -> Graph 弱资源索引 -> 同帧准确租约 -> Phase Stop/图结束清理。
// 索引不是资源所有者；使用实际参数帧、GC 和表现 Actor 检查隔离与生命周期。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "PureMass/PureMassRuntime.h"
#include "MyGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Script.h"

namespace AbilityRuntimeResourceSlotTests
{
    /** 通过正式 Scope 发布捕获帧，不构造绑定了伪 RootId 的参数对象。 */
    struct FWorld : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        TArray<TStrongObjectPtr<UAbilityGraphInstance>> Runs;
        TArray<TStrongObjectPtr<UAbilityPrototypeParameterFrame>> Frames;

        ~FWorld()
        {
            for (const auto& Run : Runs)
            {
                Run->OnEvent.Clear();
                Run->Cancel();
            }
        }

        UAbilityPrototype* Parameters(float Duration)
        {
            auto* Result = NewObject<UAbilityPrototype>(World);
            Result->Kind = EAbilityPrototypeKind::Burst;
            Result->Cooldown = 0.f;
            Result->Damage = Result->DamagePerLevel = 0.f;
            Result->DamageType = FMyGameplayTags::Get().Damage_Magic;
            Result->Duration = Duration;
            Result->Radius = 1.f;
            return Result;
        }

        UAbilityGraphInstance* Start(const UAbilityPrototype& RootParameters, FString& Error)
        {
            auto* Definition =
                AbilityPrototypeFineCompiler::Compile(World, &RootParameters, TEXT("LegacyMixed.v1"), Error);
            if (!Definition)
            {
                return nullptr;
            }
            auto* Context = NewObject<UPrototypeCastContext>(World);
            Context->Caster = Source;
            Context->Level = 1;
            Context->CastId = FGuid::NewGuid();
            Context->DamageParams.SourceAbilitySystemComponent = ASC;
            if (!Context->CaptureSourceLife(Error))
            {
                return nullptr;
            }
            FAbilityNodeInput Input;
            Input.SourceToken = Context->GetSourceLife();
            Input.Context = Context;
            Input.DamageParams = Context->DamageParams;
            Input.Direction = FVector::ForwardVector;
            auto* Run = NewObject<UAbilityGraphInstance>(World);
            Runs.Emplace(Run);
            Run->OnEvent.AddLambda(
                [this](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType == TEXT("Scope.Prototype") && Event.OutputPort == TEXT("Ready"))
                    {
                        if (auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Event.Input.ParameterFrame.Get()))
                        {
                            Frames.Emplace(Frame);
                        }
                    }
                });
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            Options.CompatibilityProfile = Definition->CompatibilityProfile;
            return Run->StartWithInput(Definition, Input, Options, Error) ? Run : nullptr;
        }
    };
} // namespace AbilityRuntimeResourceSlotTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeResourceSlotsAreWeakAndIsolated,
                                 "test_25d.UnifiedGA.DynamicMage.Resources.SlotsRequireExactRootAndDoNotOwnObjects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeResourceSlotsAreWeakAndIsolated::RunTest(const FString&)
{
    using namespace AbilityRuntimeResourceSlotTests;
    FWorld F;
    FString Error;
    auto* A = F.Start(*F.Parameters(1.f), Error);
    auto* B = F.Start(*F.Parameters(1.f), Error);
    if (!TestNotNull(TEXT("First real graph starts: ") + Error, A) ||
        !TestNotNull(TEXT("Second real graph starts: ") + Error, B) ||
        !TestEqual(TEXT("Two separate real scope frames were published"), F.Frames.Num(), 2))
    {
        return false;
    }
    const auto& FrameA = *F.Frames[0];
    const auto& FrameB = *F.Frames[1];
    const FName Slot(TEXT("Test.WeakResource"));
    auto* ResourceA = NewObject<UAbilityPrototypeQueryNodeConfig>(F.World);
    TWeakObjectPtr<UObject> WeakA(ResourceA);
    const TStrongObjectPtr<UAbilityPrototypeQueryNodeConfig> ResourceB(
        NewObject<UAbilityPrototypeQueryNodeConfig>(F.World));
    TestTrue(TEXT("Exact active root registers one weak resource"),
             A->RegisterRuntimeResource(FrameA, Slot, ResourceA));
    TestEqual(TEXT("Same root resolves its own slot"), A->FindRuntimeResource(FrameA, Slot),
              static_cast<UObject*>(ResourceA));
    TestFalse(TEXT("Other root frame cannot register into this graph"),
              A->RegisterRuntimeResource(FrameB, Slot, ResourceB.Get()));
    TestNull(TEXT("Other root cannot read the first resource"), B->FindRuntimeResource(FrameA, Slot));
    TestTrue(TEXT("Other root may independently use the same slot name"),
             B->RegisterRuntimeResource(FrameB, Slot, ResourceB.Get()));
    TestFalse(TEXT("Wrong exact object cannot unregister the current resource"),
              A->UnregisterRuntimeResource(FrameA, Slot, ResourceB.Get()));
    TestEqual(TEXT("Rejected foreign cleanup leaves current resource intact"), A->FindRuntimeResource(FrameA, Slot),
              static_cast<UObject*>(ResourceA));
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("A weak slot never keeps a detached UObject alive"), WeakA.IsValid());
    TestNull(TEXT("Collected resource is not returned as a dangling slot"), A->FindRuntimeResource(FrameA, Slot));
    TestEqual(TEXT("Other root's independently owned resource survives"), B->FindRuntimeResource(FrameB, Slot),
              static_cast<UObject*>(ResourceB.Get()));
    TestTrue(TEXT("Exact live owner can unregister its own resource"),
             B->UnregisterRuntimeResource(FrameB, Slot, ResourceB.Get()));
    TestNull(TEXT("Unregistered resource is no longer published"), B->FindRuntimeResource(FrameB, Slot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPrototypePhasePublishesExactPresentationLease,
    "test_25d.UnifiedGA.DynamicMage.Resources.PhaseReleasesOwnVisualSlotWhileChildStillRuns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrototypePhasePublishesExactPresentationLease::RunTest(const FString&)
{
    using namespace AbilityRuntimeResourceSlotTests;
    FWorld F;
    auto* Root = F.Parameters(.2f);
    Root->OnExpire = F.Parameters(1.f);
    FString Error;
    auto* Run = F.Start(*Root, Error);
    if (!TestNotNull(TEXT("Real finite graph with a separate expiry child starts: ") + Error, Run) ||
        !TestEqual(TEXT("Only root scope has started initially"), F.Frames.Num(), 1))
    {
        return false;
    }
    const auto& RootFrame = *F.Frames[0];
    const FName Slot(TEXT("PrototypePresentation"));
    auto* RootHost = Cast<APureMassRuntime>(Run->FindRuntimeResource(RootFrame, Slot));
    TestNotNull(TEXT("Actual phase publishes its exact strongly-owned visual through the weak slot"), RootHost);
    // 夹具世界不BeginPlay；仅开启被观察的真实表现宿主，才能断言renderer转换计数。
    if (RootHost && !RootHost->HasActorBegunPlay())
    {
        RootHost->DispatchBeginPlay();
    }
    ++GFrameCounter;
    Run->Advance(.21f);
    if (!TestEqual(TEXT("Normal expiry starts an independent logical child frame"), F.Frames.Num(), 2))
    {
        return false;
    }
    TestFalse(TEXT("Child keeps the same root execution alive after parent phase ended"), Run->IsFinished());
    TestNull(TEXT("Ended parent unpublishes its exact slot before graph-wide cleanup"),
             Run->FindRuntimeResource(RootFrame, Slot));
    const auto& ChildFrame = *F.Frames[1];
    auto* ChildHost = Cast<APureMassRuntime>(Run->FindRuntimeResource(ChildFrame, Slot));
    TestNotNull(TEXT("Child owns a distinct presentation slot in the same running graph"), ChildHost);
    if (ChildHost && !ChildHost->HasActorBegunPlay())
    {
        ChildHost->DispatchBeginPlay();
    }
    if (RootHost && ChildHost)
    {
        TestTrue(TEXT("Parent visual closes at its own authored duration"), RootHost->IsFinished());
        TestFalse(TEXT("Parent cleanup cannot close child visual"), ChildHost->IsFinished());
    }
    Run->Cancel();
    TestNull(TEXT("Cancellation removes remaining child slot"), Run->FindRuntimeResource(ChildFrame, Slot));
    if (ChildHost)
    {
        TestTrue(TEXT("The actual child visual resource is also closed"), ChildHost->IsFinished());
        TestEqual(TEXT("Owned child visual finishes once"), ChildHost->GetPresentationStats().Finishes, 1);
    }
    return true;
}
#endif
