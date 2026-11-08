/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.StoneGolem.RuntimeLocomotionSamples
 * - test_25d.StoneGolem.Geometry
 * - test_25d.StoneGolem.AttackGeneration
 * - test_25d.StoneGolem.ProjectedSurface
 * - test_25d.StoneGolem.AuthoredConfiguration
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "StoneGolem/StoneGolemCharacter.h"
#include "StoneGolem/StoneGolemAbility.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/Script.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Animation/BlendSpace.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoneGolemBlendTest, "test_25d.StoneGolem.RuntimeLocomotionSamples",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StoneGolem.RuntimeLocomotionSamples 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStoneGolemBlendTest::RunTest(const FString&)
{
    auto* Blend = LoadObject<UBlendSpace>(
        nullptr, TEXT("/Game/BluePrints/Characters/StoneGolem/Animations/BS_SG_Locomotion.BS_SG_Locomotion"));
    if (!TestNotNull(TEXT("Saved locomotion asset"), Blend))
        return false;
    for (float Speed : {0.f, 150.f, 300.f})
    {
        TArray<FBlendSampleData> Samples;
        int32 Cached = INDEX_NONE;
        TestTrue(TEXT("Saved interpolation data produces runtime samples"),
                 Blend->GetSamplesFromBlendInput(FVector(Speed, 0, 0), Samples, Cached, true));
        float Weight = 0;
        for (const auto& Sample : Samples)
        {
            Weight += Sample.TotalWeight;
            if (Sample.TotalWeight > .99f)
            {
                TestTrue(TEXT("Selected sample valid"), Blend->GetBlendSamples().IsValidIndex(Sample.SampleDataIndex));
                if (Blend->GetBlendSamples().IsValidIndex(Sample.SampleDataIndex))
                    TestEqual(TEXT("Exact speed selects matching sample"),
                              Blend->GetBlendSamples()[Sample.SampleDataIndex].SampleValue.X, double(Speed));
            }
        }
        TestTrue(TEXT("Runtime weights normalize"), FMath::IsNearlyEqual(Weight, 1.f));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoneGolemGeometryTest, "test_25d.StoneGolem.Geometry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StoneGolem.Geometry 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStoneGolemGeometryTest::RunTest(const FString&)
{
    FStoneGolemAttackTuning T;
    T.Radius = 220;
    T.Shape = EStoneGolemShape::Sector;
    T.Angle = 80;
    TestTrue(TEXT("Center accepted"), T.Contains(FVector::ZeroVector, 0, FVector::ZeroVector));
    TestTrue(TEXT("Radial boundary accepted"), T.Contains(FVector::ZeroVector, 0, FVector(220, 0, 0)));
    TestFalse(TEXT("Beyond radius rejected"), T.Contains(FVector::ZeroVector, 0, FVector(220.1f, 0, 0)));
    TestFalse(TEXT("Rear rejected"), T.Contains(FVector::ZeroVector, 0, FVector(-20, 0, 0)));
    TestTrue(TEXT("Angular boundary accepted"), T.Contains(FVector::ZeroVector, 0, FRotator(0, 40, 0).Vector() * 200));
    TestFalse(TEXT("Outside angle rejected"), T.Contains(FVector::ZeroVector, 0, FRotator(0, 40.1f, 0).Vector() * 200));
    TestTrue(TEXT("Rotated shape accepted"), T.Contains(FVector(100, 100, 0), 90, FVector(100, 200, 0)));
    TestFalse(TEXT("Different floor rejected"), T.Contains(FVector::ZeroVector, 0, FVector(10, 0, 61)));
    T.Shape = EStoneGolemShape::Circle;
    T.Radius = 100;
    for (int32 Step = 0; Step <= 96; ++Step)
        TestTrue(TEXT("Sampled rim remains inside despite trig rounding"),
                 T.Contains(FVector::ZeroVector, 0, FRotator(0, -80.f + 160.f * Step / 96.f, 0).Vector() * 100));
    T.Radius = 220;
    TestTrue(TEXT("Circle includes rear"), T.Contains(FVector::ZeroVector, 0, FVector(-100, 0, 0)));
    const TArray<FVector> Surface = {FVector(0, 0, 0), FVector(100, 0, 0), FVector(0, 100, 0)};
    const TArray<int32> Triangles = {0, 1, 2};
    TestTrue(TEXT("Visible triangle interior accepted"),
             StoneGolemGeometry::SurfaceContains(Surface, Triangles, FVector(20, 20, 0)));
    TestTrue(TEXT("Visible triangle edge accepted"),
             StoneGolemGeometry::SurfaceContains(Surface, Triangles, FVector(50, 50, 0)));
    TestFalse(TEXT("Culled half of cell cannot damage"),
              StoneGolemGeometry::SurfaceContains(Surface, Triangles, FVector(80, 80, 0)));
    TestFalse(TEXT("Empty warning cannot damage"),
              StoneGolemGeometry::SurfaceContains(Surface, {}, FVector(20, 20, 0)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoneGolemLifeTest, "test_25d.StoneGolem.AttackGeneration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StoneGolem.AttackGeneration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStoneGolemLifeTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<AStoneGolemCharacter>();
    Actor->Tags.Add(TEXT("DungeonEncounter"));
    Actor->GetAbilitySystemComponent()->InitAbilityActorInfo(Actor, Actor);
    Actor->EnableDungeonPooling(0);
    const int32 Lease = Actor->GetDungeonPoolLease();
    FStoneGolemAttackTuning T;
    const int32 First = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    TestTrue(TEXT("First generation valid"), Actor->IsAttackCurrent(First, Lease));
    TestEqual(TEXT("Concurrent attack refused"), Actor->BeginAttack(T, FVector::ZeroVector, 0), 0);
    Actor->FinishAttack(First);
    const int32 Second = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    Actor->FinishAttack(First);
    TestTrue(TEXT("Stale finish cannot end new attack"), Actor->IsAttackCurrent(Second, Lease));
    const bool BaselineRotation = Actor->GetCharacterMovement()->bUseControllerDesiredRotation;
    Actor->GetCharacterMovement()->bUseControllerDesiredRotation = !BaselineRotation;
    Actor->Die(FVector::ZeroVector);
    TestFalse(TEXT("Death immediately invalidates callbacks"), Actor->IsAttackCurrent(Second, Lease));
    TestFalse(TEXT("Death removes active presentation"), Actor->IsAttacking());
    Actor->SleepForDungeonPool(false);
    TestTrue(TEXT("Pooled actor wakes"), Actor->WakeFromDungeonPool(FTransform::Identity, NAME_None));
    TestEqual(TEXT("New life restores controller rotation"),
              Actor->GetCharacterMovement()->bUseControllerDesiredRotation, BaselineRotation);
    const int32 Third = Actor->BeginAttack(T, FVector::ZeroVector, 0);
    TestFalse(TEXT("Old lease rejected"), Actor->IsAttackCurrent(Third, Lease));
    TestTrue(TEXT("New life works"), Actor->IsAttackCurrent(Third, Actor->GetDungeonPoolLease()));
    Actor->FinishAttack(Second);
    TestTrue(TEXT("Old life cleanup cannot end new life attack"), Actor->IsAttacking());
    Actor->StunTagChanged(FGameplayTag::RequestGameplayTag(TEXT("Debuff.Stun")), 1);
    TestFalse(TEXT("Stun immediately clears current attack"), Actor->IsAttacking());
    Actor->Die(FVector::ZeroVector);
    Actor->Die(FVector::ZeroVector);
    TestFalse(TEXT("Second-life death invalidates callbacks"),
              Actor->IsAttackCurrent(Third, Actor->GetDungeonPoolLease()));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoneGolemSurfaceTest, "test_25d.StoneGolem.ProjectedSurface",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StoneGolem.ProjectedSurface 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStoneGolemSurfaceTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Box = [&](const FVector& Position, const FVector& Extent)
    {
        auto* Actor = World->SpawnActor<AActor>();
        auto* Component = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Component);
        Actor->AddInstanceComponent(Component);
        Component->SetBoxExtent(Extent);
        Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Component->SetCollisionObjectType(ECC_WorldStatic);
        Component->SetCollisionResponseToAllChannels(ECR_Block);
        Component->RegisterComponent();
        Actor->SetActorLocation(Position);
        return Actor;
    };
    Box(FVector(0, 0, -10), FVector(600, 600, 10));
    Box(FVector(100, 0, 100), FVector(10, 300, 100));
    FStoneGolemAttackTuning T;
    T.Radius = 450;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    StoneGolemGeometry::BuildSurface(World, nullptr, T, FVector::ZeroVector, 0, Vertices, Triangles);
    TestTrue(TEXT("Floor produces visible warning triangles"), Triangles.Num() > 0);
    TestTrue(TEXT("Unblocked floor stays dangerous"),
             StoneGolemGeometry::SurfaceContains(Vertices, Triangles, FVector(-100, 0, 0)));
    TestFalse(TEXT("Behind wall has no warning or damage"),
              StoneGolemGeometry::SurfaceContains(Vertices, Triangles, FVector(200, 0, 0)));
    FVector Ground;
    TestFalse(TEXT("Higher floor cannot project into lower floor"),
              StoneGolemGeometry::Project(World, nullptr, FVector(-100, 0, 200), 60, Ground));
    TestFalse(TEXT("Missing floor rejected"),
              StoneGolemGeometry::Project(World, nullptr, FVector(900, 0, 0), 60, Ground));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStoneGolemAssetsTest, "test_25d.StoneGolem.AuthoredConfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.StoneGolem.AuthoredConfiguration 回归，失败信息由下方断言交给自动化框架收集。 */
bool FStoneGolemAssetsTest::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("HeavyHammer"), TEXT("Quake"), TEXT("ShieldBash")})
    {
        FString Path = FString::Printf(TEXT("/Game/BluePrints/Characters/StoneGolem/DA_SG_%s.DA_SG_%s"), Name, Name);
        auto* Config = LoadObject<UStoneGolemAttackConfig>(nullptr, *Path);
        if (!TestNotNull(Name, Config))
            continue;
        FString Error;
        TestTrue(*FString(Name), Config->Tuning.Validate(Error));
        auto Invalid = Config->Tuning;
        Invalid.LockTime = Invalid.HitTime();
        TestFalse(TEXT("No reaction time rejected"), Invalid.Validate(Error));
        Invalid = Config->Tuning;
        Invalid.PlayRate = 0;
        TestFalse(TEXT("Zero rate rejected"), Invalid.Validate(Error));
        Invalid = Config->Tuning;
        Invalid.ContactTime = 99;
        TestFalse(TEXT("Out of montage contact rejected"), Invalid.Validate(Error));
    }
    return true;
}
#endif
