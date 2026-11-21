// 调用链：真实原生雷链细图 -> 运行时配方编译 -> 同一GraphInstance/世界Timer -> 真实费用GE和目标石化。
// 本文件验证重复端口的运行时编辑权限；测试世界和图仅驻内存，析构先取消图再释放世界。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestAbility.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteFragments.h"
#include "AbilitySystem/Unified/Nodes/AbilityPetrifyFragments.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"

namespace
{
    /** 复用真实宿主与费用回调；不以手工发布Period代替实际Timer驱动的支付。 */
    struct FSustainedRecipeFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        UAbilityElectrocuteContext* Context = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        FAbilityNodeInput Input;
        int32 Paid = 0;

        /** 捕获原生命并创建原生默认细图，目标状态仍由既有目标组件负责清理。 */
        FSustainedRecipeFixture()
        {
            auto* Settings = NewObject<UAbilityElectrocuteNodeConfig>(World);
            Settings->bTraceFromWeapon = false;
            Settings->MaxAdditionalTargets = 0;
            Settings->DamageInterval = .01f;
            Graph = AbilityElectrocuteFragments::Build(World, *Settings);
            Context = NewObject<UAbilityElectrocuteContext>(World);
            Context->AimActor = Target;
            Context->CommitPeriodCost = [this]()
            {
                const auto Spec =
                    ASC->MakeOutgoingSpec(UAbilityGraphTestCost::StaticClass(), 1.f, ASC->MakeEffectContext());
                const auto Result = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
                Paid += Result.WasSuccessfullyApplied() ? 1 : 0;
                return Result.WasSuccessfullyApplied();
            };
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 10000.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 10000.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(),
                                                                         10000.f);
            Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                                         10000.f);
            Input.Context = Context;
            Input.SourceToken = SourceToken;
            Input.LiveTargetToken = TargetToken;
            Input.Origin = Target->GetActorLocation();
            Input.DamageParams.SourceAbilitySystemComponent = ASC;
            Input.DamageParams.DamageGameplayEffectClass = UGameplayEffect::StaticClass();
            Input.DamageParams.BaseDamage = 13.f;
            Input.DamageParams.AbilityLevel = 1.f;
            Input.DamageParams.DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder"));
            Run = NewObject<UAbilityGraphInstance>(World);
        }

        /** 取消本次宿主的Timer/状态所有权，避免夹具销毁时捕获this的费用回调继续运行。 */
        ~FSustainedRecipeFixture()
        {
            Run->Cancel();
            Context->CommitPeriodCost = nullptr;
        }

        /** 定义身份取自真实导出图，不依赖临时GUID或猜测节点数组下标。 */
        FGuid Find(FName Type) const
        {
            const auto* Node = Graph->Nodes.FindByPredicate(
                [Type](const FAbilityGraphNode& Item)
                {
                    return Item.TypeId == Type;
                });
            return Node ? Node->NodeId : FGuid();
        }

        /** 将实际支付325次；每个Advance明确推进真实帧，不能绕过每帧预算。 */
        bool StartAndPay325(UAbilityGraphDefinition* Definition, FString& Error)
        {
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Definition->BudgetPolicy;
            if (!Run->StartWithInput(Definition, Input, Options, Error))
            {
                return false;
            }
            World->GetTimerManager().Tick(0.f);
            for (int32 Index = 0; Index < 325 && !Run->IsFinished(); ++Index)
            {
                ++GFrameCounter;
                World->GetTimerManager().Tick(.011f);
                Run->Advance(.011f);
            }
            return true;
        }
    };

    /** 创建一个受普通有限预算保护的分支；是否能挂在重复端口由宿主合同决定。 */
    UAbilityGraphDefinition* MakeBranch(UObject* Outer, bool bDelay)
    {
        auto* Branch = NewObject<UAbilityGraphDefinition>(Outer);
        Branch->SchemaVersion = 2;
        Branch->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = bDelay ? TEXT("Control.DelayOrigin") : TEXT("Status.PetrifyBatch");
        if (bDelay)
        {
            auto* Config = NewObject<UAbilityDelayNodeConfig>(Branch);
            Config->DelaySeconds = .01f;
            Node.Config = Config;
        }
        else
        {
            auto* Config = NewObject<UAbilityPetrifyBatchConfig>(Branch);
            Config->Duration = 1.f;
            Node.Config = Config;
        }
        Branch->Nodes.Add(Node);
        Branch->EntryNodeId = Node.NodeId;
        return Branch;
    }

    /** 使用同一公开编译入口，失败必须清空输出并保留基础定义。 */
    bool CompilePatch(UObject* Outer, UAbilityGraphDefinition* Base, const FAbilityRuntimeRecipePatch& Patch,
                      UAbilityGraphDefinition*& Result, FString& Error)
    {
        FAbilityRuntimeRecipeContribution Source;
        Source.SourceId = TEXT("Test.SustainedSlot");
        Source.Patches.Add(Patch);
        return AbilityRuntimeRecipes::Compile(Outer, Base, {Source}, Result, Error);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSustainedAsyncReplacement,
                                 "test_25d.DynamicMage.SustainedSlots.AsyncReplacementCannotInheritNativePeriodBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Delay在普通有限图合法；同一配置不能借替换周期效果获得无限累计调度权限。 */
bool FRuntimeSustainedAsyncReplacement::RunTest(const FString&)
{
    FSustainedRecipeFixture F;
    auto* Delay = MakeBranch(F.World, true);
    FAbilityRuntimeRecipePatch Patch;
    Patch.Operation = EAbilityRecipePatchOperation::ReplaceNode;
    Patch.TargetNodeId = Delay->EntryNodeId;
    Patch.ReplacementTypeId = TEXT("Control.DelayOrigin");
    Patch.ReplacementConfig = Delay->Nodes[0].Config;
    FString Error;
    UAbilityGraphDefinition* Parsed = nullptr;
    if (!TestTrue(TEXT("The exact Delay configuration remains valid inside an ordinary finite host"),
                  CompilePatch(F.World, Delay, Patch, Parsed, Error)))
    {
        AddError(Error);
        return false;
    }
    auto* FiniteRun = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityExecutionOptions Options;
    TestTrue(TEXT("Finite Delay executes under ordinary budgets"),
             FiniteRun->StartWithInput(Parsed, F.Input, Options, Error));
    ++GFrameCounter;
    FiniteRun->Advance(.011f);
    TestTrue(TEXT("Finite Delay closes its own asynchronous invocation"), FiniteRun->IsFinished());
    FiniteRun->Cancel();
    Patch.TargetNodeId = F.Find(TEXT("Effect.ElectrocutePeriod"));
    const auto Hash = AbilityRuntimeRecipes::Fingerprint(*F.Graph);
    TestFalse(TEXT("An asynchronous replacement cannot inherit the registered sustained period slot"),
              CompilePatch(F.World, F.Graph, Patch, Parsed, Error));
    TestNull(TEXT("Rejected replacement leaves no partial definition"), Parsed);
    TestEqual(TEXT("Rejected replacement never mutates the original channel"),
              AbilityRuntimeRecipes::Fingerprint(*F.Graph), Hash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSustainedAsyncAppend,
                                 "test_25d.DynamicMage.SustainedSlots.AsyncAppendToRepeatedPeriodIsRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 不能先接受无限重复的有限Delay分支，再于第65次支付后终止原引导。 */
bool FRuntimeSustainedAsyncAppend::RunTest(const FString&)
{
    FSustainedRecipeFixture F;
    FAbilityRuntimeRecipePatch Patch;
    Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
    Patch.TargetNodeId = F.Find(TEXT("Channel.ElectrocuteHost"));
    Patch.OutputPort = TEXT("Period");
    Patch.BranchGraph = MakeBranch(F.World, true);
    UAbilityGraphDefinition* Parsed = nullptr;
    FString Error;
    TestFalse(TEXT("The repeated Period port rejects an unverified asynchronous branch at compile time"),
              CompilePatch(F.World, F.Graph, Patch, Parsed, Error));
    TestNull(TEXT("Rejected asynchronous append has no partially published graph"), Parsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSustainedPureAppend,
                                 "test_25d.DynamicMage.SustainedSlots.PureAppendSurvives325ActualPaidPeriods",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 正控制与待验证分支均使用原Timer和真实费用；目标石化不是事件计数替身。 */
bool FRuntimeSustainedPureAppend::RunTest(const FString&)
{
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        FSustainedRecipeFixture F;
        auto* Branch = MakeBranch(F.World, false);
        FAbilityRuntimeRecipePatch Patch;
        Patch.Operation =
            Mode == 0 ? EAbilityRecipePatchOperation::ReplaceNode : EAbilityRecipePatchOperation::AppendBranch;
        Patch.TargetNodeId = F.Find(Mode == 0 ? TEXT("Effect.ElectrocutePeriod") : TEXT("Channel.ElectrocuteHost"));
        Patch.ReplacementTypeId = TEXT("Status.PetrifyBatch");
        Patch.ReplacementConfig = Branch->Nodes[0].Config;
        Patch.BranchGraph = Branch;
        Patch.OutputPort = TEXT("Period");
        UAbilityGraphDefinition* Parsed = nullptr;
        FString Error;
        const FString Label = Mode == 0 ? TEXT("Verified pure replacement control") : TEXT("Pure appended leaf");
        if (!TestTrue(*Label, CompilePatch(F.World, F.Graph, Patch, Parsed, Error)))
        {
            AddError(Error);
            return false;
        }
        if (!TestTrue(*(Label + TEXT(" starts the actual original host")), F.StartAndPay325(Parsed, Error)))
        {
            AddError(Error);
            return false;
        }
        TestFalse(*(Label + TEXT(" does not truncate the channel at the finite64 boundary")), F.Run->IsFinished());
        TestEqual(*(Label + TEXT(" pays all325 real original costs")), F.Paid, 325);
        TestEqual(*(Label + TEXT(" retains exact real Mana")),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 6750.f);
        TestTrue(*(Label + TEXT(" changes actual target state")), F.IsPetrified());
        F.Context->RequestCompletion();
        F.Run->FlushPublishedEvents(true);
        TestTrue(*(Label + TEXT(" releases the original host on release")), F.Run->IsFinished());
        TestEqual(*(Label + TEXT(" leaves no graph-owned invocation")), F.Run->GetActiveInvocationCount(), 0);
        const int32 Before = F.Paid;
        ++GFrameCounter;
        F.World->GetTimerManager().Tick(1.1f);
        F.Run->Advance(1.1f);
        TestEqual(*(Label + TEXT(" cannot pay again after release")), F.Paid, Before);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeSustainedPayloadProof,
                                 "test_25d.DynamicMage.SustainedSlots.RuntimeProofRejectsForgedPurePayloadMetadata",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 额外协议覆盖：Graph.Validate不能仅相信服务器编译标记；本测试随Native3有效RED的修复一并新增。 */
bool FRuntimeSustainedPayloadProof::RunTest(const FString&)
{
    FSustainedRecipeFixture F;
    FAbilityRuntimeRecipePatch Patch;
    Patch.Operation = EAbilityRecipePatchOperation::AppendBranch;
    Patch.TargetNodeId = F.Find(TEXT("Channel.ElectrocuteHost"));
    Patch.OutputPort = TEXT("Period");
    Patch.BranchGraph = MakeBranch(F.World, false);
    FString Error;
    UAbilityGraphDefinition* Parsed = nullptr;
    if (!TestTrue(TEXT("Trusted pure branch compiles before each independent tamper"),
                  CompilePatch(F.World, F.Graph, Patch, Parsed, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto* OriginalPayload = Parsed->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.bRuntimeRepeatedPayload;
        });
    if (!TestNotNull(TEXT("Compiler marks the exact new payload domain"), OriginalPayload))
    {
        return false;
    }
    const FGuid PayloadId = OriginalPayload->NodeId;
    for (int32 Mode = 0; Mode < 5; ++Mode)
    {
        auto* Forged = DuplicateObject<UAbilityGraphDefinition>(Parsed, F.World);
        auto* Node = Forged->Nodes.FindByPredicate(
            [PayloadId](const FAbilityGraphNode& Item)
            {
                return Item.NodeId == PayloadId;
            });
        if (Mode == 0)
        {
            Node->TypeId = TEXT("Control.DelayOrigin");
            Node->Config = NewObject<UAbilityDelayNodeConfig>(Forged);
        }
        else if (Mode == 1)
        {
            for (auto& Edge : Forged->Edges)
            {
                if (Edge.To == PayloadId)
                {
                    Edge.OutputPort = TEXT("Final");
                }
            }
        }
        else if (Mode == 2)
        {
            Node->bRuntimeFinite = true;
            Forged->bHasRuntimeFinite = true;
        }
        else if (Mode == 3)
        {
            CastChecked<UAbilityPetrifyBatchConfig>(Node->Config)->Duration = 61.f;
        }
        else
        {
            // 只读快照被服务器代码误改时，删除公开域标记不能把新增Delay伪装成原合法helper。
            Node->bRuntimeRepeatedPayload = false;
            Node->TypeId = TEXT("Control.DelayOrigin");
            Node->Config = NewObject<UAbilityDelayNodeConfig>(Forged);
        }
        TArray<FString> Errors;
        TestFalse(TEXT("Validation recomputes strategy, exact repeated port, domain and config proof"),
                  Forged->ValidateForAuthoring(Errors));
        if (Mode == 4)
        {
            auto* ForgedRun = NewObject<UAbilityGraphInstance>(F.World);
            FAbilityExecutionOptions Options;
            Options.BudgetPolicy = Forged->BudgetPolicy;
            TestFalse(TEXT("Starting a copied compiled definition rejects cleared-marker asynchronous tampering"),
                      ForgedRun->StartWithInput(Forged, F.Input, Options, Error));
            ForgedRun->Cancel();
        }
    }
    auto* NoMarker = DuplicateObject<UAbilityGraphDefinition>(Parsed, F.World);
    NoMarker->Nodes
        .FindByPredicate(
            [PayloadId](const FAbilityGraphNode& Item)
            {
                return Item.NodeId == PayloadId;
            })
        ->bRuntimeRepeatedPayload = false;
    TestNotEqual(TEXT("The exact payload domain is part of semantic recipe identity"),
                 AbilityRuntimeRecipes::Fingerprint(*Parsed), AbilityRuntimeRecipes::Fingerprint(*NoMarker));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeCompilationSealWorkflow,
                                 "test_25d.DynamicMage.SustainedSlots.CompiledSealPreservesPrivateRecipeWorkflow",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 内容封印只限制已发布快照：正常复制、零编辑重编译和私有合法补丁继续沿原入口运行。 */
bool FRuntimeCompilationSealWorkflow::RunTest(const FString&)
{
    FSustainedRecipeFixture F;
    FAbilityRuntimeRecipePatch Append;
    Append.Operation = EAbilityRecipePatchOperation::AppendBranch;
    Append.TargetNodeId = F.Find(TEXT("Channel.ElectrocuteHost"));
    Append.OutputPort = TEXT("Period");
    Append.BranchGraph = MakeBranch(F.World, false);
    FString Error;
    UAbilityGraphDefinition* Published = nullptr;
    if (!TestTrue(TEXT("Initial private recipe compiles"), CompilePatch(F.World, F.Graph, Append, Published, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString PublishedHash = AbilityRuntimeRecipes::Fingerprint(*Published);
    auto* Duplicate = DuplicateObject<UAbilityGraphDefinition>(Published, F.World);
    TArray<FString> Errors;
    TestTrue(TEXT("An unchanged ordinary Duplicate preserves a valid published definition"),
             Duplicate->ValidateForAuthoring(Errors));
    TestEqual(TEXT("Object/outer identity and private seal do not change semantic content"),
              AbilityRuntimeRecipes::Fingerprint(*Duplicate), PublishedHash);
    UAbilityGraphDefinition* ZeroEdit = nullptr;
    if (!TestTrue(TEXT("Zero-edit Compile accepts a valid sealed base"),
                  AbilityRuntimeRecipes::Compile(F.World, Duplicate, {}, ZeroEdit, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Zero-edit output carries a valid new seal"), ZeroEdit->ValidateForAuthoring(Errors));
    TestEqual(TEXT("Zero-edit private recompilation preserves semantic identity"),
              AbilityRuntimeRecipes::Fingerprint(*ZeroEdit), PublishedHash);
    FAbilityRuntimeRecipePatch Scale;
    Scale.Operation = EAbilityRecipePatchOperation::DurationScale;
    Scale.TargetTypeId = TEXT("Status.PetrifyBatch");
    Scale.Scalar = 2.f;
    UAbilityGraphDefinition* Edited = nullptr;
    if (!TestTrue(TEXT("A legitimate typed patch reopens only the private working copy and reseals it"),
                  CompilePatch(F.World, ZeroEdit, Scale, Edited, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Legitimately edited output passes its new content seal"), Edited->ValidateForAuthoring(Errors));
    TestNotEqual(TEXT("Legitimate parameter edits produce a new content identity"),
                 AbilityRuntimeRecipes::Fingerprint(*Edited), PublishedHash);
    TestEqual(TEXT("Earlier published recipe remains unchanged"), AbilityRuntimeRecipes::Fingerprint(*Published),
              PublishedHash);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityExecutionOptions Options;
    Options.BudgetPolicy = Edited->BudgetPolicy;
    Options.OwnerLease = FGuid::NewGuid();
    const FString EditedHash = AbilityRuntimeRecipes::Fingerprint(*Edited);
    TestTrue(TEXT("Actual execution accepts the legitimately resealed recipe"),
             Run->StartWithInput(Edited, F.Input, Options, Error));
    F.World->GetTimerManager().Tick(0.f);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(.011f);
    Run->Advance(.011f);
    TestFalse(TEXT("Execution identity and owner lease do not invalidate the running definition"), Run->IsFinished());
    TestTrue(TEXT("Original published content remains valid after execution-state changes"),
             Edited->ValidateForAuthoring(Errors));
    TestEqual(TEXT("Runtime identity is outside the configuration fingerprint"),
              AbilityRuntimeRecipes::Fingerprint(*Edited), EditedHash);
    Run->Cancel();
    auto* Tampered = DuplicateObject<UAbilityGraphDefinition>(Edited, F.World);
    auto* Payload = Tampered->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Status.PetrifyBatch");
        });
    CastChecked<UAbilityPetrifyBatchConfig>(Payload->Config)->Duration = 3.f;
    TestFalse(TEXT("Ordinary Duplicate retains its seal and rejects a still-legal Duration-only mutation"),
              Tampered->ValidateForAuthoring(Errors));
    UAbilityGraphDefinition* Laundered = nullptr;
    TestFalse(TEXT("Compile validates its sealed base before resetting the private working seal"),
              AbilityRuntimeRecipes::Compile(F.World, Tampered, {}, Laundered, Error));
    TestNull(TEXT("A tampered base cannot be laundered into a new published recipe"), Laundered);
    // 原输入asset/未编译定义仍可按制作路径修改，Compile没有把调用者基础图变成只读输出。
    auto* Host = F.Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Channel.ElectrocuteHost");
        });
    CastChecked<UAbilityElectrocuteNodeConfig>(Host->Config)->DamageInterval = .02f;
    TestTrue(TEXT("Ordinary unsealed authoring definitions retain their existing editable workflow"),
             F.Graph->ValidateForAuthoring(Errors));
    return true;
}
#endif
