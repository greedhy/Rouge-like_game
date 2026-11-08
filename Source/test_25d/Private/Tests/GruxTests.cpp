/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Grux.AuthoredConfiguration
 * - test_25d.Grux.CorridorBoundary
 * - test_25d.Grux.RoarLeaseCleanup
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "Character/GruxEnemyCharacter.h"
#include "AbilitySystem/Ability/GruxEnemyAbility.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Script.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGruxAssetsTest, "test_25d.Grux.AuthoredConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Grux.AuthoredConfiguration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGruxAssetsTest::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("Combo"), TEXT("Charge"), TEXT("Roar")})
    {
        auto* Config = LoadObject<UGruxAttackConfig>(
            nullptr,
            *FString::Printf(TEXT("/Game/BluePrints/Characters/Grux/Expansion/DA_Grux_%s.DA_Grux_%s"), Name, Name));
        if (!TestNotNull(Name, Config))
            continue;
        FString Error;
        TestTrue(TEXT("Valid staged timing"), Config->ValidateGrux(Error));
        TestTrue(TEXT("Every activation has a warning"), Config->Tuning.bExclamation || Config->Tuning.bDrawFootprint);
        if (Config->Kind != EGruxAttackKind::Roar)
        {
            TestTrue(TEXT("Each damaging phase has actual footprint"),
                     Config->Tuning.bDrawFootprint && Config->Followup.bDrawFootprint);
            TestTrue(TEXT("Followup has reaction time"), Config->Followup.HitTime() >= .3f);
        }
        else
            TestEqual(TEXT("Roar does not deal damage"), Config->Tuning.DamageMultiplier, 0.f);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGruxCorridorTest, "test_25d.Grux.CorridorBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Grux.CorridorBoundary 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGruxCorridorTest::RunTest(const FString&)
{
    FStoneGolemAttackTuning T;
    // Appended enum value preserves existing serialized Circle/Sector identities.
    T.Shape = static_cast<EStoneGolemShape>(2);
    T.Radius = 600;
    T.Angle = 180;
    TestTrue(TEXT("Inside forward corridor"), T.Contains(FVector::ZeroVector, 0, FVector(500, 69, 0)));
    TestFalse(TEXT("Outside corridor width is safe"), T.Contains(FVector::ZeroVector, 0, FVector(500, 71, 0)));
    TestFalse(TEXT("Behind the charge is safe"), T.Contains(FVector::ZeroVector, 0, FVector(-1, 0, 0)));
    TestFalse(TEXT("Beyond the end is safe"), T.Contains(FVector::ZeroVector, 0, FVector(601, 0, 0)));
    TestTrue(TEXT("Rotation preserves corridor shape"), T.Contains(FVector(100, 100, 0), 90, FVector(31, 600, 0)));
    TestFalse(TEXT("Rotated outer width remains safe"), T.Contains(FVector(100, 100, 0), 90, FVector(29, 600, 0)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGruxRoarLifeTest, "test_25d.Grux.RoarLeaseCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Grux.RoarLeaseCleanup 回归，失败信息由下方断言交给自动化框架收集。 */
bool FGruxRoarLifeTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<AGruxEnemyCharacter>();
    auto* ASC = Actor->GetAbilitySystemComponent();
    ASC->InitAbilityActorInfo(Actor, Actor);
    ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
    ASC->SetNumericAttributeBase(UMyAttributeSet::GetArmorAttribute(), 10);
    Actor->EnableDungeonPooling(0);
    auto First = Actor->ApplyRoarArmor(25, 6);
    TestTrue(TEXT("Real armor GE applied"), First.IsValid() && Actor->HasRoarArmor());
    TestEqual(TEXT("Armor increases exactly once"), ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()),
              35.f);
    TestFalse(TEXT("Roar does not stack"), Actor->ApplyRoarArmor(25, 6).IsValid());
    Actor->SleepForDungeonPool(false);
    TestFalse(TEXT("Pool retirement clears buff immediately"), Actor->HasRoarArmor());
    TestEqual(TEXT("Base armor restored"), ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 10.f);
    TestTrue(TEXT("Next life wakes"), Actor->WakeFromDungeonPool(FTransform::Identity, TEXT("GruxTest")));
    auto Second = Actor->ApplyRoarArmor(15, 4);
    Actor->RemoveRoarArmor(First);
    TestTrue(TEXT("Old handle cannot remove new life buff"), Actor->HasRoarArmor());
    TestEqual(TEXT("New life armor independent"), ASC->GetNumericAttribute(UMyAttributeSet::GetArmorAttribute()), 25.f);
    Actor->RemoveRoarArmor(Second);
    TestFalse(TEXT("Owned handle removes current buff"), Actor->HasRoarArmor());
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
