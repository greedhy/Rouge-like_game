// 调用链：Automation -> 真实 Actor/Mass 目标 -> 共享石化服务 -> 原组件/子系统生命周期及权限断言。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "DungeonPlayerRunTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Materials/Material.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunDirector.h"
#include "UObject/Script.h"
#include <limits>

namespace AbilityGraphPetrifyServiceTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        ADungeonPlayerRunTestPawn* Source = nullptr;
        AEnemyCharacter* Enemy = nullptr;

        /** 用实际 PlayerState ASC 与 Enemy 组件建立独立服务器测试世界，不依赖资源资产。 */
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Source = World->SpawnActor<ADungeonPlayerRunTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
            Source->AttachState(World->SpawnActor<AMyPlayerState>());
            Source->Tags = {TEXT("Player")};
            Enemy = World->SpawnActor<AEnemyCharacter>(FVector(100, 0, 0), FRotator::ZeroRotator, Params);
            Enemy->Tags = {TEXT("Enemy")};
            Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
        }

        /** 析构始终撤销测试上下文，状态组件由原 EndPlay 路径清理。 */
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 原子转发到被测入口，便于同一 Actor 的参数与生命周期用例。 */
        bool ApplyActor(float Duration, UMaterialInterface* Material = nullptr)
        {
            return AbilityStatusService::ApplyPetrify(Source, FPureMassCombatTarget::FromActor(Enemy), Duration,
                                                      Material);
        }

        /** 构造有界房间实体以验证原 Mass 权限和状态时长。 */
        int32 SpawnMass(FName Room = NAME_None, FVector Location = FVector(200, 0, 0))
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Location, Room);
        }
    };
} // namespace AbilityGraphPetrifyServiceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityPetrifyActorLifecycle, "test_25d.UnifiedGA.PetrifyService.ActorLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 服务使用唯一复制组件；短刷新不缩短期限，到期与 GE 移除均恢复原表现。 */
bool FAbilityPetrifyActorLifecycle::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    AbilityGraphPetrifyServiceTests::FFixture F;
    auto* Original = NewObject<UMaterial>(F.World);
    auto* Stone = NewObject<UMaterial>(F.World);
    auto* MeshAsset = NewObject<USkeletalMesh>(F.World);
    MeshAsset->GetMaterials().Add(FSkeletalMaterial(Original));
    F.Enemy->GetMesh()->SetSkeletalMeshAsset(MeshAsset);
    F.Enemy->GetMesh()->SetMaterial(0, Original);
    auto* ASC = F.Enemy->GetAbilitySystemComponent();
    TestTrue(TEXT("Service applies Actor control"), F.ApplyActor(1.f, Stone));
    auto* Component = F.Enemy->FindComponentByClass<UBatch6PetrifyComponent>();
    if (!TestNotNull(TEXT("Service registers existing component type"), Component))
    {
        return false;
    }
    TestTrue(TEXT("Original component replication retained"), Component->GetIsReplicated());
    TestTrue(TEXT("Actor receives stun"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestTrue(TEXT("Animation pauses"), F.Enemy->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Stone material applies"), F.Enemy->GetMesh()->GetMaterial(0) == Stone);
    TestTrue(TEXT("Short refresh accepted"), F.ApplyActor(.1f, nullptr));
    TInlineComponentArray<UBatch6PetrifyComponent*> Components(F.Enemy);
    TestEqual(TEXT("Repeated application creates one component"), Components.Num(), 1);
    TestEqual(TEXT("Repeated application retains one control GE"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),
              1);
    // 世界不 BeginPlay；显式推动组件入口，以世界真实时间验证原到期规则。
    for (int32 Frame = 0; Frame < 2; ++Frame)
    {
        F.World->Tick(LEVELTICK_All, .25f);
        Component->TickComponent(.25f, LEVELTICK_All, nullptr);
    }
    TestTrue(TEXT("Short refresh preserves longer original duration"), F.Enemy->GetMesh()->bPauseAnims);
    for (int32 Frame = 0; Frame < 4; ++Frame)
    {
        F.World->Tick(LEVELTICK_All, .25f);
        Component->TickComponent(.25f, LEVELTICK_All, nullptr);
    }
    TestFalse(TEXT("Actor expiry removes stun"), ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun));
    TestFalse(TEXT("Actor expiry restores animation"), F.Enemy->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Null refresh still restores original material"), F.Enemy->GetMesh()->GetMaterial(0) == Original);
    TestTrue(TEXT("Same component can reapply"), F.ApplyActor(1.f, Stone));
    ASC->RemoveActiveEffects(FGameplayEffectQuery());
    TestFalse(TEXT("GE removal restores animation synchronously"), F.Enemy->GetMesh()->bPauseAnims);
    TestTrue(TEXT("GE removal restores material"), F.Enemy->GetMesh()->GetMaterial(0) == Original);
    TestFalse(TEXT("Cleared component stops ticking"), Component->IsComponentTickEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityPetrifyMassLifecycle, "test_25d.UnifiedGA.PetrifyService.MassLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 共享入口转发 Mass 原状态机，刷新使用最大剩余时长并到期发布解除。 */
bool FAbilityPetrifyMassLifecycle::RunTest(const FString&)
{
    AbilityGraphPetrifyServiceTests::FFixture F;
    const int32 Id = F.SpawnMass();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    auto Apply = [&](float Duration)
    {
        return AbilityStatusService::ApplyPetrify(F.Source, FPureMassCombatTarget(Id), Duration, nullptr);
    };
    TestTrue(TEXT("Mass target accepts service"), Apply(1.f));
    FPureMassView View;
    TestTrue(TEXT("Mass snapshot resolves"), Mass->Resolve(Id, View));
    TestTrue(TEXT("Mass snapshot is petrified"), View.bPetrified);
    auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    auto& Fragment = Manager.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id));
    TestTrue(TEXT("Short Mass refresh accepted"), Apply(.1f));
    TestEqual(TEXT("Mass refresh preserves original duration"), Fragment.PetrifyRemaining, 1.f);
    for (int32 Step = 0; Step < 4; ++Step)
    {
        Mass->BeginUpdate(.25f);
        Mass->UpdateMonster(Fragment);
        Mass->FinishUpdate();
        if (Step == 1)
        {
            Mass->Resolve(Id, View);
            TestTrue(TEXT("Mass still controlled halfway through"), View.bPetrified);
            TestTrue(TEXT("Controlled Mass stops moving"), View.Velocity.IsNearlyZero());
        }
    }
    Mass->Resolve(Id, View);
    TestFalse(TEXT("Mass expires through original state machine"), View.bPetrified);
    Mass->RemoveMonster(Id);
    TestFalse(TEXT("Removed Mass rejected"), Apply(1.f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityPetrifyInvalidInputs,
                                 "test_25d.UnifiedGA.PetrifyService.InvalidInputsAndAuthority",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 失败输入不得创建组件或施加控制；保留友军、失效目标、非服务器及跨世界拒绝。 */
bool FAbilityPetrifyInvalidInputs::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    AbilityGraphPetrifyServiceTests::FFixture F;
    TestFalse(TEXT("Zero duration rejected"), F.ApplyActor(0.f));
    TestFalse(TEXT("Negative duration rejected"), F.ApplyActor(-1.f));
    TestFalse(TEXT("NaN duration rejected"), F.ApplyActor(std::numeric_limits<float>::quiet_NaN()));
    TestFalse(TEXT("Infinite duration rejected"), F.ApplyActor(std::numeric_limits<float>::infinity()));
    F.Enemy->Tags = {TEXT("Player")};
    TestFalse(TEXT("Friendly Actor rejected"), F.ApplyActor(1.f));
    F.Enemy->Tags = {TEXT("Enemy")};
    F.Source->SetTestRole(ROLE_AutonomousProxy);
    TestFalse(TEXT("Client source rejected"), F.ApplyActor(1.f));
    F.Source->SetTestRole(ROLE_Authority);
    TestFalse(TEXT("Null source rejected"),
              AbilityStatusService::ApplyPetrify(nullptr, FPureMassCombatTarget::FromActor(F.Enemy), 1.f, nullptr));
    TestFalse(TEXT("Empty target rejected"), AbilityStatusService::ApplyPetrify(F.Source, {}, 1.f, nullptr));
    {
        AbilityGraphPetrifyServiceTests::FFixture Other;
        TestFalse(
            TEXT("Other world's Actor rejected"),
            AbilityStatusService::ApplyPetrify(F.Source, FPureMassCombatTarget::FromActor(Other.Enemy), 1.f, nullptr));
    }
    F.Enemy->EnableDungeonPooling(0.f);
    F.Enemy->SleepForDungeonPool();
    TestFalse(TEXT("Sleeping Actor rejected"), F.ApplyActor(1.f));
    TestNull(TEXT("Rejected inputs never created a component"),
             F.Enemy->FindComponentByClass<UBatch6PetrifyComponent>());
    F.Enemy->Destroy();
    TestFalse(TEXT("Destroyed Actor rejected"), F.ApplyActor(1.f));
    const int32 Id = F.SpawnMass();
    F.Source->Tags = {TEXT("Enemy")};
    TestFalse(TEXT("Mass allies rejected"),
              AbilityStatusService::ApplyPetrify(F.Source, FPureMassCombatTarget(Id), 1.f, nullptr));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityPetrifyRoomPermissions,
                                 "test_25d.UnifiedGA.PetrifyService.ActorAndMassRoomPermissions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Actor 遵循 Run 房间权限，Mass 遵循子系统已登记的实体房间；服务不另造查询或范围规则。 */
bool FAbilityPetrifyRoomPermissions::RunTest(const FString&)
{
    AbilityGraphPetrifyServiceTests::FFixture F;
    auto* Generation = F.World->SpawnActor<ADungeonGenerationRuntime>();
    Generation->Manifest.RoomSpacing = 2400.f;
    Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef().Cell = FIntPoint(0, 0);
    Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef().Cell = FIntPoint(1, 0);
    auto* Director = F.World->SpawnActor<ADungeonRunDirector>();
    Director->Phase = EDungeonRunPhase::Encounter;
    Director->ActiveRoom = 0;
    F.Enemy->Tags.Add(TEXT("DungeonEncounter"));
    F.Enemy->SetActorLocation(FVector(2400, 0, 0));
    TestFalse(TEXT("Actor outside active room rejected"), F.ApplyActor(1.f));
    F.Enemy->SetActorLocation(FVector(100, 0, 0));
    TestTrue(TEXT("Actor in active room accepted"), F.ApplyActor(1.f));
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    TestTrue(TEXT("Mass room registered"),
             Mass->SetRoomBounds(TEXT("OtherRoom"), FVector(2400, 0, 0), FVector(500, 500, 500)));
    const int32 Id = F.SpawnMass(TEXT("OtherRoom"), FVector(2400, 0, 0));
    TestTrue(TEXT("Room Mass spawned"), Id > 0);
    TestFalse(TEXT("Mass source outside target room rejected"),
              AbilityStatusService::ApplyPetrify(F.Source, FPureMassCombatTarget(Id), 1.f, nullptr));
    F.Source->SetActorLocation(FVector(2400, 0, 0));
    TestTrue(TEXT("Mass source in target room accepted"),
             AbilityStatusService::ApplyPetrify(F.Source, FPureMassCombatTarget(Id), 1.f, nullptr));
    return true;
}
#endif
