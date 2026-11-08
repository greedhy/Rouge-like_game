/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Batch6.PetrifyLeaseIsolation
 * - test_25d.Batch6.MassPetrifyLifecycle
 * - test_25d.Batch6.ActorPetrifyLifecycle
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MyGameplayTags.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6PetrifyLeaseTest, "test_25d.Batch6.PetrifyLeaseIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Batch6.PetrifyLeaseIsolation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBatch6PetrifyLeaseTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Unmanaged live actor"), UBatch6PetrifyComponent::IsLeaseValid(0, 0));
    TestTrue(TEXT("Same live life"), UBatch6PetrifyComponent::IsLeaseValid(7, 7));
    TestFalse(TEXT("Pool return invalidates control"), UBatch6PetrifyComponent::IsLeaseValid(7, -7));
    TestFalse(TEXT("Next life rejects old control"), UBatch6PetrifyComponent::IsLeaseValid(7, 8));
    TestFalse(TEXT("Retired life never valid"), UBatch6PetrifyComponent::IsLeaseValid(-7, -7));
    FPureMassView View;
    View.Id = 42;
    View.bPetrified = true;
    FPureMassNetEntity Wire;
    TestTrue(TEXT("Stone state dirties snapshot"), Wire.SetFromView(View));
    TestTrue(TEXT("Stone flag reaches remote view"), Wire.ToView().bPetrified);
    TestFalse(TEXT("Identical stone snapshot stays clean"), Wire.SetFromView(View));
    View.bPetrified = false;
    TestTrue(TEXT("Recovery dirties snapshot"), Wire.SetFromView(View));
    TestFalse(TEXT("Recovery reaches remote view"), Wire.ToView().bPetrified);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6MassPetrifyLifecycleTest, "test_25d.Batch6.MassPetrifyLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Batch6.MassPetrifyLifecycle 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBatch6MassPetrifyLifecycleTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(World);
    Type->Health = 100;
    Type->Speed = 100;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector::ZeroVector);
    TestTrue(TEXT("Apply live control"), Mass->ApplyPetrify(Id, 1.f, nullptr));
    FPureMassView View;
    Mass->Resolve(Id, View);
    TestTrue(TEXT("Publishes stone immediately"), View.bPetrified);
    TestFalse(TEXT("Reject invalid duration"), Mass->ApplyPetrify(Id, -1.f, nullptr));
    auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
    TestTrue(TEXT("Short refresh accepted"), Mass->ApplyPetrify(Id, .1f, nullptr));
    TestEqual(TEXT("Short refresh preserves longer control"), Fragment.PetrifyRemaining, 1.f);
    for (int32 Step = 0; Step < 2; ++Step)
    {
        Mass->BeginUpdate(.25f);
        Mass->UpdateMonster(Fragment);
        Mass->FinishUpdate();
    }
    Mass->Resolve(Id, View);
    TestTrue(TEXT("Control survives first half"), View.bPetrified);
    TestTrue(TEXT("Controlled target is stationary"), View.Velocity.IsNearlyZero());
    for (int32 Step = 0; Step < 2; ++Step)
    {
        Mass->BeginUpdate(.25f);
        Mass->UpdateMonster(Fragment);
        Mass->FinishUpdate();
    }
    Mass->Resolve(Id, View);
    TestFalse(TEXT("Expires and republishes recovery"), View.bPetrified);
    Mass->RemoveMonster(Id);
    TestFalse(TEXT("Retired identity rejects control"), Mass->ApplyPetrify(Id, 1.f, nullptr));
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatch6ActorPetrifyLifecycleTest, "test_25d.Batch6.ActorPetrifyLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Batch6.ActorPetrifyLifecycle 回归，失败信息由下方断言交给自动化框架收集。 */
bool FBatch6ActorPetrifyLifecycleTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Enemy = World->SpawnActor<AEnemyCharacter>();
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy);
    ASC->InitAbilityActorInfo(Enemy, Enemy);
    auto* Control = NewObject<UBatch6PetrifyComponent>(Enemy);
    Enemy->AddInstanceComponent(Control);
    Control->RegisterComponent();
    auto* Original = NewObject<UMaterial>(World);
    auto* Stone = NewObject<UMaterial>(World);
    auto* MeshAsset = NewObject<USkeletalMesh>(World);
    MeshAsset->GetMaterials().Add(FSkeletalMaterial(Original));
    Enemy->GetMesh()->SetSkeletalMeshAsset(MeshAsset);
    Enemy->GetMesh()->SetMaterial(0, Original);
    TestFalse(TEXT("Idle controller tick disabled"), Control->IsComponentTickEnabled());
    TestTrue(TEXT("Live Actor accepts petrification"), Control->Apply(2.f, Stone));
    TestTrue(TEXT("Active controller tick enabled"), Control->IsComponentTickEnabled());
    TestTrue(TEXT("Stone applied"), Enemy->GetMesh()->GetMaterial(0) == Stone);
    Control->Apply(.1f, nullptr);
    TestTrue(TEXT("Runtime spec grants native stun"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestTrue(TEXT("Actor animation freezes"), Enemy->GetMesh()->bPauseAnims);
    ASC->RemoveActiveEffects(FGameplayEffectQuery());
    TestFalse(TEXT("Removing effect restores animation synchronously before pool disables tick"),
              Enemy->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Null refresh still restores original material"), Enemy->GetMesh()->GetMaterial(0) == Original);
    TestFalse(TEXT("Cleared controller tick disabled"), Control->IsComponentTickEnabled());
    TestFalse(TEXT("Owned stun removed"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestTrue(TEXT("Same component can control next life"), Control->Apply(.1f, nullptr));
    ASC->RemoveActiveEffects(FGameplayEffectQuery());
    TestFalse(TEXT("Second removal restores animation"), Enemy->GetMesh()->bPauseAnims);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
