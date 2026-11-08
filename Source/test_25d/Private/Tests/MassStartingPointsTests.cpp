/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Extensions.StartingPointsSession
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "PureMass/Extensions/MassStartingPoints.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassStartingPointsTest, "test_25d.Extensions.StartingPointsSession",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Extensions.StartingPointsSession 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMassStartingPointsTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Session = NewObject<UMassStartingPointsSession>(GI);
    TestEqual(TEXT("Host fresh start"), Session->EnterPlayer(0), 999);
    TestEqual(TEXT("Remote fresh start"), Session->EnterPlayer(1), 999);
    Session->RememberPlayer(0, 978);
    Session->RememberPlayer(1, 0);
    TestEqual(TEXT("Host retains spending after travel"), Session->EnterPlayer(0), 978);
    TestEqual(TEXT("Zero points is not a fresh start"), Session->EnterPlayer(1), 0);
    auto* NewSession = NewObject<UMassStartingPointsSession>(NewObject<UGameInstance>());
    TestEqual(TEXT("Separate session starts fresh"), NewSession->EnterPlayer(0), 999);
    return true;
}
#endif
