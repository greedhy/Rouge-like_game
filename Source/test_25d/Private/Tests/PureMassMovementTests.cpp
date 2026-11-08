/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.PureMass.Movement.CrowdConstraints
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "../PureMass/PureMassMovement.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassCrowdMovementTest, "test_25d.PureMass.Movement.CrowdConstraints",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.PureMass.Movement.CrowdConstraints 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPureMassCrowdMovementTest::RunTest(const FString&)
{
    using namespace PureMassMovement;
    TestNearlyEqual(TEXT("Long step stops at front neighbour"),
                    ContactFraction(FVector(-100, 0, 0), FVector(200, 0, 0), 64), .18f);
    TestEqual(TEXT("Touching neighbour blocks inward motion"),
              ContactFraction(FVector(-64, 0, 0), FVector(4, 0, 0), 64), 0.f);
    TestEqual(TEXT("Overlap can separate"), ContactFraction(FVector(-30, 0, 0), FVector(-4, 0, 0), 64), 1.f);
    TestEqual(TEXT("Lateral motion passes outside radius"),
              ContactFraction(FVector(-100, 80, 0), FVector(200, 0, 0), 64), 1.f);
    for (int32 Step = 0; Step < 120; ++Step)
    {
        const FVector Direction = Steer(FVector::ForwardVector, FVector(-3, Step % 2 ? .1f : -.1f, 0), true);
        TestTrue(TEXT("Congestion never reverses pursuit"), Direction.X >= 0);
        TestTrue(TEXT("Noise does not flip passing side"), Direction.Y > 0);
        TestTrue(TEXT("Steering bounded"), Direction.Size() <= 1.001f);
    }
    FQuat Facing = FQuat::Identity;
    for (int32 Step = 0; Step < 120; ++Step)
    {
        const FQuat Next = Turn(Facing, FRotator(0, Step % 2 ? 170.f : -170.f, 0).Quaternion(), 1.f / 60.f);
        TestTrue(TEXT("Alternating snapshots obey 240deg/s"),
                 FMath::RadiansToDegrees(Facing.AngularDistance(Next)) <= 4.01f);
        Facing = Next;
    }
    return true;
}
#endif
