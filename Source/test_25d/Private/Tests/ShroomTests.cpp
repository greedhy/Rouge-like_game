/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Shroom.PulseCadence
 * - test_25d.Shroom.AuthoredConfiguration
 * - test_25d.Shroom.TwoLives
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Ability/ShroomEnemyAbility.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShroomPulseTest, "test_25d.Shroom.PulseCadence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Shroom.PulseCadence 回归，失败信息由下方断言交给自动化框架收集。 */
bool FShroomPulseTest::RunTest(const FString&)
{
    FShroomPulseClock Clock;
    Clock.Reset(10, 1, 3);
    TestTrue(TEXT("Landing causes first pulse"), Clock.Consume(10));
    TestFalse(TEXT("Same timestamp cannot double hit"), Clock.Consume(10));
    TestFalse(TEXT("No early pulse"), Clock.Consume(10.999));
    TestTrue(TEXT("Next due pulse"), Clock.Consume(11.01));
    TestTrue(TEXT("Last in-duration pulse"), Clock.Consume(12.01));
    TestFalse(TEXT("End boundary is no longer damaging"), Clock.Consume(13));
    Clock.Reset(20, .25f, 3);
    TestTrue(TEXT("Hitch emits one pulse"), Clock.Consume(22.4));
    TestFalse(TEXT("Hitch does not burst all missed pulses"), Clock.Consume(22.4));
    TestTrue(TEXT("Future cadence resumes"), Clock.Consume(22.5));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShroomAssetsTest, "test_25d.Shroom.AuthoredConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Shroom.AuthoredConfiguration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FShroomAssetsTest::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("Slam"), TEXT("Spore")})
    {
        auto* Config = LoadObject<UStoneGolemAttackConfig>(
            nullptr, *FString::Printf(TEXT("/Game/BluePrints/Characters/Shroom/Expansion/DA_Shroom_%s.DA_Shroom_%s"),
                                      Name, Name));
        if (!TestNotNull(Name, Config))
            continue;
        FString Error;
        TestTrue(TEXT("Valid timing"), Config->Tuning.Validate(Error));
        TestTrue(TEXT("Circle area has a visible footprint"),
                 Config->Tuning.Shape == EStoneGolemShape::Circle && Config->Tuning.bDrawFootprint);
        if (auto* Spore = Cast<UShroomSporeConfig>(Config))
            TestTrue(TEXT("Bounded spore cadence"), Spore->ValidateSpore(Error));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShroomPoolTest, "test_25d.Shroom.TwoLives",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Shroom.TwoLives 回归，失败信息由下方断言交给自动化框架收集。 */
bool FShroomPoolTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* Class = LoadClass<ATelegraphedEnemyCharacter>(
        nullptr, TEXT("/Game/BluePrints/Characters/Shroom/Expansion/BP_Shroom.BP_Shroom_C"));
    if (!TestNotNull(TEXT("Authored Shroom"), Class))
        return false;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<ATelegraphedEnemyCharacter>(Class);
    Actor->GetAbilitySystemComponent()->InitAbilityActorInfo(Actor, Actor);
    Actor->Tags.Add(TEXT("DungeonEncounter"));
    Actor->EnableDungeonPooling(0);
    const int32 Lease = Actor->GetDungeonPoolLease();
    FStoneGolemAttackTuning T;
    const int32 Serial = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    Actor->Die(FVector::ZeroVector);
    Actor->SleepForDungeonPool(false);
    TestTrue(TEXT("Wake after death"), Actor->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    const int32 Second = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    Actor->FinishAttack(Serial);
    TestFalse(TEXT("Old field lease cannot remain active"), Actor->IsAttackCurrent(Serial, Lease));
    TestTrue(TEXT("Old cleanup preserves new phase"), Actor->IsAttackCurrent(Second, Actor->GetDungeonPoolLease()));
    TestEqual(TEXT("Selector and exactly two skills"),
              Actor->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), 3);
    Actor->SleepForDungeonPool(false);
    TestTrue(TEXT("Second reuse"), Actor->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    TestEqual(TEXT("No duplicate grants"), Actor->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), 3);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
