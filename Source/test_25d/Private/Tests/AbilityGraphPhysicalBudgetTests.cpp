// 调用链：真实内存图运行 -> 根生成预算预留 -> 分支共享计数；测试不依赖Actor生成数量冒充预算检查。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityGraphPhysicalBudgetTest,
                                 "test_25d.UnifiedGA.DynamicMage.RootPhysicalSpawnBudget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAbilityGraphPhysicalBudgetTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Graph->Budget.MaxInvocations = 3;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Pending graph starts with actual original life"),
                  Run->Start(F.Graph, F.SourceToken, F.TargetToken, Error)))
    {
        return false;
    }
    TestFalse(TEXT("Negative count cannot refund budget"), Run->ReservePhysicalSpawn(-1));
    TestFalse(TEXT("Zero count is not a generation request"), Run->ReservePhysicalSpawn(0));
    TestTrue(TEXT("First branch reserves two physical deliveries"), Run->ReservePhysicalSpawn(2));
    TestFalse(TEXT("Second branch cannot independently reset the same root budget"), Run->ReservePhysicalSpawn(2));
    TestTrue(TEXT("Rejected reservation did not consume available remainder"), Run->ReservePhysicalSpawn(1));
    TestFalse(TEXT("Root physical budget is exhausted across all branches"), Run->ReservePhysicalSpawn());
    Run->Cancel();
    TestFalse(TEXT("Cancelled root cannot generate more"), Run->ReservePhysicalSpawn());
    return true;
}
#endif
