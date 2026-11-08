/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Abilities.PrototypeFacing
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Prototypes/PrototypeFacing.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeFacingTest, "test_25d.Abilities.PrototypeFacing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.PrototypeFacing 回归，失败信息由下方断言交给自动化框架收集。 */
bool FPrototypeFacingTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player = World->SpawnActor<ADungeonPlayerCharacter>();
    const FVector Origin = Player->GetActorLocation();
    PrototypeFacing::Apply(Player, Origin + FVector(0, 500, 200), false);
    TestTrue(TEXT("Room player faces aimed cast"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 90.f));
    TestTrue(TEXT("Cast does not tilt player"), FMath::IsNearlyZero(Player->GetActorRotation().Pitch));
    PrototypeFacing::Apply(Player, Origin + FVector(-500, 0, 0), true);
    TestTrue(TEXT("Self cast preserves facing"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 90.f));
    PrototypeFacing::Apply(Player, Origin + FVector(0, 0, 200), false);
    TestTrue(TEXT("Vertical target preserves facing"), FMath::IsNearlyEqual(Player->GetActorRotation().Yaw, 90.f));
    auto* Other = World->SpawnActor<AActor>();
    PrototypeFacing::Apply(Other, Other->GetActorLocation() + FVector(0, 500, 0), false);
    TestTrue(TEXT("Does not override non-player AI facing"), FMath::IsNearlyZero(Other->GetActorRotation().Yaw));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
