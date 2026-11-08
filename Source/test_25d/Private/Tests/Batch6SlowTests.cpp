/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Batch6.Slow.OverlapExpiry
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6SlowTest, "test_25d.Batch6.Slow.OverlapExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Batch6.Slow.OverlapExpiry 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBatch6SlowTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Caster = World->SpawnActor<AActor>();
    Caster->Tags.Add(TEXT("Player"));
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    Enemy->Tags.Add(TEXT("Enemy"));
    Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
    auto Target = FPureMassCombatTarget::FromActor(Enemy);
    TestTrue(TEXT("Strong short slow applied"), UBatch6SlowComponent::Apply(World, Caster, Target, .25f, 1));
    TestTrue(TEXT("Weak long slow applied independently"), UBatch6SlowComponent::Apply(World, Caster, Target, .75f, 3));
    TestEqual(TEXT("Strongest active slow wins"), UBatch6SlowComponent::GetMultiplier(Enemy), .25f);
    for (int32 Step = 0; Step < 6; ++Step)
        World->Tick(LEVELTICK_All, .2f);
    TestEqual(TEXT("Weaker slow survives stronger expiry"), UBatch6SlowComponent::GetMultiplier(Enemy), .75f);
    TestFalse(TEXT("Invalid multiplier rejected"), UBatch6SlowComponent::Apply(World, Caster, Target, -1, 2));
    auto* Component = Enemy->FindComponentByClass<UBatch6SlowComponent>();
    if (Component)
        Component->DestroyComponent();
    TestEqual(TEXT("Removal restores independent neutral multiplier"), UBatch6SlowComponent::GetMultiplier(Enemy), 1.f);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
