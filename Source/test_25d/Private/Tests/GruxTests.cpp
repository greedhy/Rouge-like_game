/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Grux.AuthoredConfiguration
 * - test_25d.Grux.CorridorBoundary
 * - test_25d.Grux.RoarLeaseCleanup
 * - test_25d.Grux.PresentationLifetime
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "Character/GruxEnemyCharacter.h"
#include "Character/CombatPresentationComponent.h"
#include "AbilitySystem/Ability/GruxEnemyAbility.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Particles/ParticleSystemComponent.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGruxPresentationLifeTest, "test_25d.Grux.PresentationLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 用正式 Boss Blueprint 驱动同一组件的攻击和护甲，覆盖清理及旧句柄隔离。 */
bool FGruxPresentationLifeTest::RunTest(const FString&)
{
    UClass* BossClass = LoadClass<AGruxEnemyCharacter>(nullptr,
        TEXT("/Game/BluePrints/Characters/Grux/Expansion/BP_GruxBoss.BP_GruxBoss_C"));
    const auto* BossDefaults = BossClass ? BossClass->GetDefaultObject<AGruxEnemyCharacter>() : nullptr;
    if (!TestTrue(TEXT("Boss has its authored presentation Blueprint"),
            BossDefaults && BossDefaults->CombatPresentationClass))
    {
        return false;
    }

    FEditorScriptExecutionGuard Guard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    AGruxEnemyCharacter* Actor = World->SpawnActor<AGruxEnemyCharacter>(BossClass);
    UCombatPresentationComponent* FX = Actor ? Actor->FindComponentByClass<UCombatPresentationComponent>() : nullptr;
    if (TestNotNull(TEXT("Grux owns one local Blueprint instance"), FX))
    {
        // NullRHI 不创建 Cascade 实例，仍执行 GE、攻击阶段与回收流程。
        const bool bCanRender = FApp::CanEverRender();
        if (!bCanRender)
        {
            AddInfo(TEXT("Cascade instance assertions require a rendering RHI."));
        }
        const FName AttackSlot(TEXT("Attack"));
        const FName ArmorSlot(TEXT("RoarArmor"));
        FStoneGolemAttackTuning Tuning;
        Tuning.ImpactPresentationPreset = TEXT("AxeImpact");
        Tuning.bDrawFootprint = false;
        auto* ASC = Actor->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(Actor, Actor);
        ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Actor));
        Actor->EnableDungeonPooling(0);

        const auto First = Actor->ApplyRoarArmor(25.f, 6.f);
        TestTrue(TEXT("Armor GE applies"), First.IsValid() && Actor->HasRoarArmor());
        TestEqual(TEXT("Armor starts particles when a renderer is present"), FX->HasPresentation(ArmorSlot), bCanRender);
        TWeakObjectPtr<UParticleSystemComponent> ArmorParticle = Actor->FindComponentByClass<UParticleSystemComponent>();
        if (bCanRender)
        {
            TestTrue(TEXT("Armor particle is attached to the boss mesh"),
                     ArmorParticle.IsValid() && ArmorParticle->GetAttachParent() == Actor->GetMesh());
        }
        const int32 Attack = Actor->BeginAttack(Tuning, FVector::ZeroVector, 0.f);
        Actor->ShowImpact(Attack, FVector::ForwardVector * 100.f);
        if (bCanRender)
        {
            TestTrue(TEXT("Real attack phase starts its presentation"), FX->HasPresentation(AttackSlot));
        }
        Actor->FinishAttack(Attack);
        TestFalse(TEXT("Finishing the attack removes its presentation"), FX->HasPresentation(AttackSlot));
        TestTrue(TEXT("Armor GE survives a normally finished attack"), Actor->HasRoarArmor());
        TestEqual(TEXT("Armor presentation survives a normally finished attack"),
                  FX->HasPresentation(ArmorSlot), bCanRender);

        ASC->RemoveActiveGameplayEffect(First);
        TestFalse(TEXT("ASC removal callback stops armor particles"), FX->HasPresentation(ArmorSlot));
        TestFalse(TEXT("Armor particle component is released"), ArmorParticle.IsValid());
        const auto Second = Actor->ApplyRoarArmor(25.f, 6.f);
        const int32 Charge = Actor->BeginAttack(Tuning, FVector::ZeroVector, 0.f);
        const FVector Impact = FVector::ZeroVector;
        Actor->ShowActiveFootprint(Charge, &Impact);
        if (bCanRender)
        {
            TestTrue(TEXT("Active footprint phase can start presentation"), FX->HasPresentation(AttackSlot));
        }
        Actor->SleepForDungeonPool(false);
        TestFalse(TEXT("Pool return stops the attack particles"), FX->HasPresentation(AttackSlot));
        TestFalse(TEXT("Pool return stops the armor particles"), FX->HasPresentation(ArmorSlot));
        TestTrue(TEXT("Grux wakes for a second life"), Actor->WakeFromDungeonPool(FTransform::Identity, TEXT("GruxFX")));
        const auto Third = Actor->ApplyRoarArmor(25.f, 6.f);
        Actor->RemoveRoarArmor(Second);
        TestTrue(TEXT("Old life cannot remove the new GE"), Third.IsValid() && Actor->HasRoarArmor());
        TestEqual(TEXT("Old life cannot stop the new armor presentation"), FX->HasPresentation(ArmorSlot), bCanRender);
        Actor->RemoveRoarArmor(Third);
        TestFalse(TEXT("Canceling owned armor stops particles"), FX->HasPresentation(ArmorSlot));
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
