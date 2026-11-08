/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Demon.AuthoredConfiguration
 * - test_25d.Demon.TwoLives
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemonAssetsTest, "test_25d.Demon.AuthoredConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Demon.AuthoredConfiguration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDemonAssetsTest::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("TailStab"), TEXT("Tailspin")})
    {
        const FString Path =
            FString::Printf(TEXT("/Game/BluePrints/Characters/Demon/Expansion/DA_Demon_%s.DA_Demon_%s"), Name, Name);
        auto* Config = LoadObject<UStoneGolemAttackConfig>(nullptr, *Path);
        if (!TestNotNull(Name, Config))
            continue;
        FString Error;
        TestTrue(TEXT("Valid tuning"), Config->Tuning.Validate(Error));
        const bool Single = FString(Name) == TEXT("TailStab");
        TestTrue(TEXT("Near-contact reach, not legacy 240cm circle"), Config->Tuning.Radius <= 100);
        TestTrue(TEXT("Actual forward sweep"), Config->Tuning.Shape == EStoneGolemShape::Sector);
        TestEqual(TEXT("Single-target policy"), Config->Tuning.bSingleTarget, Single);
        TestEqual(TEXT("Area attacks show footprint"), Config->Tuning.bDrawFootprint, !Single);
        TestTrue(TEXT("Every attack has warning"), Config->Tuning.bDrawFootprint || Config->Tuning.bExclamation);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemonPoolTest, "test_25d.Demon.TwoLives",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Demon.TwoLives 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDemonPoolTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UClass* Class = LoadClass<ATelegraphedEnemyCharacter>(
        nullptr, TEXT("/Game/BluePrints/Characters/Demon/Expansion/BP_DemonWarrior.BP_DemonWarrior_C"));
    if (!TestNotNull(TEXT("Authored Demon class"), Class))
        return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<ATelegraphedEnemyCharacter>(Class);
    Actor->GetAbilitySystemComponent()->InitAbilityActorInfo(Actor, Actor);
    Actor->Tags.Add(TEXT("DungeonEncounter"));
    Actor->EnableDungeonPooling(0);
    const int32 OldLease = Actor->GetDungeonPoolLease();
    FStoneGolemAttackTuning T;
    const int32 First = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    TestTrue(TEXT("First life active"), Actor->IsAttackCurrent(First, OldLease));
    Actor->Die(FVector::ZeroVector);
    Actor->SleepForDungeonPool(false);
    TestFalse(TEXT("Dead life cannot attack"), Actor->IsAttackCurrent(First, OldLease));
    TestTrue(TEXT("Wake succeeds"), Actor->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    TestEqual(TEXT("Exactly selector and two attacks granted"),
              Actor->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), 3);
    const int32 Second = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    Actor->FinishAttack(First);
    TestTrue(TEXT("Stale cleanup cannot end new attack"), Actor->IsAttackCurrent(Second, Actor->GetDungeonPoolLease()));
    TestFalse(TEXT("Old lease cannot address new life"), Actor->IsAttackCurrent(Second, OldLease));
    Actor->Die(FVector::ZeroVector);
    Actor->SleepForDungeonPool(false);
    TestTrue(TEXT("Second wake succeeds"), Actor->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    TestEqual(TEXT("No duplicate grants after reuse"),
              Actor->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), 3);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
