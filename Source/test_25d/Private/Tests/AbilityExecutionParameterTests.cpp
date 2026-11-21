// 测试链：真实根图/来源 -> 创建只读值帧 -> 尝试跨根借用 -> 应在真实费用效果前拒绝。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityGraphTestAbility.h"
#include "AbilityExecutionParameterTestTypes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParameterFrameRootIsolation,
                                 "test_25d.UnifiedGA.DynamicMage.Parameters.CrossRootCannotAuthorizePayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParameterFrameRootIsolation::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    FString Error;
    const TStrongObjectPtr<UAbilityGraphInstance> First(NewObject<UAbilityGraphInstance>(F.World));
    TestTrue(TEXT("Original real root starts"), First->Start(F.Graph, F.SourceToken, F.TargetToken, Error));
    const TStrongObjectPtr<UAbilityExecutionParameterTestValue> Frame(
        NewObject<UAbilityExecutionParameterTestValue>(F.World));
    TestTrue(TEXT("Frame binds original root and source once"),
             Frame->BindExecution(First->GetExecutionId(), F.SourceToken));
    TestFalse(TEXT("Immutable frame cannot be reparented to a later root"),
              Frame->BindExecution(FGuid::NewGuid(), F.SourceToken));
    TestTrue(TEXT("Original identity remains intact"), Frame->BelongsTo(First->GetExecutionId(), F.SourceToken));

    auto* Other = NewObject<UAbilityGraphDefinition>(F.World);
    Other->SchemaVersion = 2;
    Other->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Delay;
    Delay.NodeId = FGuid::NewGuid();
    Delay.TypeId = TEXT("Control.DelayOrigin");
    auto* DelayConfig = NewObject<UAbilityDelayNodeConfig>(Other);
    DelayConfig->DelaySeconds = .01f;
    Delay.Config = DelayConfig;
    FAbilityGraphNode Cost;
    Cost.NodeId = FGuid::NewGuid();
    Cost.TypeId = TEXT("Effect.GameplayEffect");
    auto* CostConfig = NewObject<UAbilityGameplayEffectNodeConfig>(Other);
    CostConfig->EffectClass = UAbilityGraphTestCost::StaticClass();
    Cost.Config = CostConfig;
    Other->Nodes = {Delay, Cost};
    Other->EntryNodeId = Delay.NodeId;
    Other->Edges = {{Delay.NodeId, Cost.NodeId, TEXT("Completed")}};
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
    F.ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.ParameterFrame = Frame.Get();
    const TStrongObjectPtr<UAbilityGraphInstance> Second(NewObject<UAbilityGraphInstance>(F.World));
    TestFalse(TEXT("Another root rejects borrowed values before scheduling any effect"),
              Second->StartWithInput(Other, Input, {}, Error));
    ++GFrameCounter;
    Second->Advance(.02f);
    TestEqual(TEXT("Cross-root values cannot reach a real source cost GE"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()), 100.f);
    First->Cancel();
    Second->Cancel();
    return true;
}
#endif
