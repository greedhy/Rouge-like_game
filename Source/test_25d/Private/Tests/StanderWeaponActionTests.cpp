/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.StanderWeapons.RejectUnconfigured
 * - test_25d.StanderWeapons.TimelineValidation
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Player/StanderCombatConfig.h"
#include "Animation/AnimMontage.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponActionRejectEmpty, "test_25d.StanderWeapons.RejectUnconfigured",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StanderWeapons.RejectUnconfigured 回归，失败信息由下方断言交给自动化框架收集。 */
bool FWeaponActionRejectEmpty::RunTest(const FString&)
{
    auto* Config = NewObject<UStanderWeaponActionConfig>();
    TestFalse(TEXT("Unconfigured action must never commit resources"), Config->Validate());
    Config->Events.AddDefaulted();
    Config->PlayRate = 0;
    TestFalse(TEXT("Zero playback speed rejected"), Config->Validate());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponActionTimeline, "test_25d.StanderWeapons.TimelineValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StanderWeapons.TimelineValidation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FWeaponActionTimeline::RunTest(const FString&)
{
    auto* Baseline =
        LoadObject<UStanderCombatConfig>(nullptr, TEXT("/Game/BluePrints/Characters/Stander/DA_Stander.DA_Stander"));
    if (!TestNotNull(TEXT("Existing animation baseline"), Baseline))
        return false;
    auto* C = NewObject<UStanderWeaponActionConfig>();
    C->Montage = Baseline->Lift.Montage;
    C->WeaponFamily = TEXT("Test");
    C->ActionTag = FGameplayTag::RequestGameplayTag(TEXT("Abilities.Stander.Lift"));
    auto& E = C->Events.AddDefaulted_GetRef();
    E.Kind = EStanderWeaponActionKind::Move;
    E.Duration = .1f;
    E.MoveHeight = 100;
    TestTrue(TEXT("Bounded jump timeline accepted"), C->Validate());
    TestEqual(TEXT("Jump starts grounded"), C->JumpHeight(0, 100), 0.f);
    TestEqual(TEXT("Jump midpoint reaches authored height"), C->JumpHeight(.5f, 100), 100.f);
    TestEqual(TEXT("Late timer lands without overshoot"), C->JumpHeight(2, 100), 0.f);
    TestTrue(TEXT("Airborne release may aim down at a forward target"),
             C->IsInAimCone(FVector::ForwardVector, FVector(300, 10, -150)));
    TestFalse(TEXT("Aim assistance never redirects to a rear enemy"),
              C->IsInAimCone(FVector::ForwardVector, FVector(-300, 0, 0)));
    TestFalse(TEXT("Aim assistance cannot select a side enemy"),
              C->IsInAimCone(FVector::ForwardVector, FVector(0, 300, 0)));
    E.Duration = C->Montage->GetPlayLength() + 1;
    TestFalse(TEXT("Movement cannot outlive montage"), C->Validate());
    E.Duration = .1f;
    C->Cooldown = 1;
    TestFalse(TEXT("Cooldown requires persistent identity"), C->Validate());
    C->Cooldown = 0;
    E.Time = -.1f;
    TestFalse(TEXT("Negative event rejected"), C->Validate());
    E.Time = .1f;
    const auto Second = E;
    C->Events.Add(Second);
    C->Events.Last().Time = 0;
    TestFalse(TEXT("Out of order events rejected"), C->Validate());
    return true;
}
#endif
