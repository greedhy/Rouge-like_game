// 调用链：Automation -> 测试世界/真实 ASC 与池生命 -> Capture/Resolve -> 身份与权限断言；析构移除 WorldContext。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "DungeonPlayerRunTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunDirector.h"
#include "UObject/Script.h"

namespace AbilityGraphIdentityTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        ADungeonPlayerRunTestPawn* Source = nullptr;
        AEnemyCharacter* Enemy = nullptr;

        /** 创建普通服务器世界，使用已有测试角色把 PlayerState ASC 绑定到真实 Avatar。 */
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

        /** 无论断言是否提前返回，均移除引擎上下文并销毁夹具世界。 */
        ~FFixture()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        /** 创建当前子系统拥有的实际实体，让测试验证 Handle 而非手造 ID。 */
        int32 SpawnMass()
        {
            auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, FVector(200, 0, 0));
        }

        /** 建立两个房间，以生产 AllowsCombat 的布局与遭遇规则验证边界。 */
        ADungeonRunDirector* StartRun()
        {
            auto* Generation = World->SpawnActor<ADungeonGenerationRuntime>();
            Generation->Manifest.RoomSpacing = 2400.f;
            auto& First = Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef();
            First.RoomId = TEXT("R00");
            First.Cell = FIntPoint(0, 0);
            auto& Second = Generation->Manifest.Layout.Rooms.AddDefaulted_GetRef();
            Second.RoomId = TEXT("R01");
            Second.Cell = FIntPoint(1, 0);
            auto* Director = World->SpawnActor<ADungeonRunDirector>();
            Director->RunId = FGuid::NewGuid();
            Director->Phase = EDungeonRunPhase::Encounter;
            Director->ActiveRoom = 0;
            Enemy->Tags.Add(TEXT("DungeonEncounter"));
            return Director;
        }
    };
} // namespace AbilityGraphIdentityTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityIdentityActorLife, "test_25d.UnifiedGA.Identity.ActorLifeAndLease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同一对象两次池生命必须分开；非池敌人不虚构租约。 */
bool FAbilityIdentityActorLife::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    AbilityGraphIdentityTests::FFixture F;
    FAbilitySourceToken Source;
    FAbilityTargetToken Target;
    FString Error;
    FVector Location;
    TestTrue(TEXT("Ordinary world source captured"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    TestTrue(TEXT("Unmanaged target captured"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy), Target, Error));
    TestFalse(TEXT("Unmanaged Actor has no invented lease"), Target.ActorLease.IsSet());
    TestTrue(TEXT("Same unmanaged life resolves"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    F.Enemy->EnableDungeonPooling(0.f);
    TestFalse(TEXT("Pool adoption invalidates unmanaged identity"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    TestTrue(TEXT("Managed target captured"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy), Target, Error));
    TestTrue(TEXT("Pool token contains positive lease"), Target.ActorLease.IsSet() && Target.ActorLease.GetValue() > 0);
    TestTrue(TEXT("Same lease resolves"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    F.Enemy->SleepForDungeonPool();
    TestTrue(TEXT("Fixture actually retired lease"), F.Enemy->GetDungeonPoolLease() < 0);
    TestFalse(TEXT("Sleeping life cannot resolve"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    FAbilityTargetToken Rejected;
    TestFalse(
        TEXT("Sleeping target cannot be captured"),
        AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy), Rejected, Error));
    TestFalse(TEXT("Failed capture resets output"), Rejected.Target.IsSet());
    TestTrue(TEXT("Next life wakes"), F.Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("Next")));
    TestFalse(TEXT("Old target token cannot affect next life"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    TestTrue(TEXT("Fresh token accepts next life"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy), Target, Error));
    F.Enemy->Destroy();
    TestFalse(TEXT("Destroyed target cannot resolve"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    TestTrue(TEXT("Failed resolution clears location"), Location.IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityIdentitySourceLife, "test_25d.UnifiedGA.Identity.SourceLeaseAndAvatar",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 同 ASC 换 Avatar、源池复用及源销毁均撤销延迟执行资格。 */
bool FAbilityIdentitySourceLife::RunTest(const FString&)
{
    FEditorScriptExecutionGuard Guard;
    AbilityGraphIdentityTests::FFixture F;
    FAbilitySourceToken Source;
    FString Error;
    AActor* Resolved = nullptr;
    auto* ASC = F.Source->GetAbilitySystemComponent();
    TestTrue(TEXT("PlayerState ASC source captures"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    TestTrue(TEXT("Source bound to actual Avatar"), Source.Avatar.Get() == F.Source && Source.ASC.Get() == ASC);
    ASC->InitAbilityActorInfo(ASC->GetOwnerActor(), F.Enemy);
    TestFalse(TEXT("Same ASC with replacement Avatar fails"),
              AbilityExecutionIdentity::ResolveSource(Source, Resolved));
    TestNull(TEXT("Failed source resolution clears output"), Resolved);
    TestFalse(TEXT("Old Avatar cannot create a fresh token"),
              AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    F.Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(F.Enemy, F.Enemy);
    F.Enemy->EnableDungeonPooling(0.f);
    TestTrue(TEXT("Pooled source captures"), AbilityExecutionIdentity::CaptureSource(F.Enemy, Source, Error));
    TestTrue(TEXT("Pooled source lease present"), Source.EnemyLease.IsSet());
    F.Enemy->SleepForDungeonPool();
    TestFalse(TEXT("Retired source fails"), AbilityExecutionIdentity::ResolveSource(Source, Resolved));
    TestTrue(TEXT("Source wakes for new life"), F.Enemy->WakeFromDungeonPool(FTransform::Identity, TEXT("Next")));
    TestFalse(TEXT("New source life rejects old token"), AbilityExecutionIdentity::ResolveSource(Source, Resolved));
    TestTrue(TEXT("New source captures"), AbilityExecutionIdentity::CaptureSource(F.Enemy, Source, Error));
    F.Enemy->Destroy();
    TestFalse(TEXT("Destroyed source fails"), AbilityExecutionIdentity::ResolveSource(Source, Resolved));
    TestFalse(TEXT("Actor without ASC rejected"),
              AbilityExecutionIdentity::CaptureSource(F.World->SpawnActor<AActor>(), Source, Error));
    auto* Uninitialized = F.World->SpawnActor<AEnemyCharacter>();
    Uninitialized->GetAbilitySystemComponent()->AbilityActorInfo.Reset();
    TestFalse(TEXT("Uninitialized ASC cannot capture and must not dereference ActorInfo"),
              AbilityExecutionIdentity::CaptureSource(Uninitialized, Source, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityIdentityMassLife, "test_25d.UnifiedGA.Identity.MassHandleAndWorld",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 相同数字 ID 在不同世界不可互用；实际 Handle 被移除或销毁时立即拒绝。 */
bool FAbilityIdentityMassLife::RunTest(const FString&)
{
    AbilityGraphIdentityTests::FFixture F;
    AbilityGraphIdentityTests::FFixture Other;
    const int32 Id = F.SpawnMass();
    TestEqual(TEXT("Fixtures deliberately have same numeric ID"), Other.SpawnMass(), Id);
    FAbilitySourceToken Source;
    FAbilitySourceToken OtherSource;
    FAbilityTargetToken Target;
    FString Error;
    FVector Location;
    TestTrue(TEXT("First world source"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    TestTrue(TEXT("Other world source"), AbilityExecutionIdentity::CaptureSource(Other.Source, OtherSource, Error));
    TestTrue(TEXT("Mass identity captured"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget(Id), Target, Error));
    TestTrue(TEXT("Same handle resolves"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    TestFalse(TEXT("Another world's same ID cannot resolve token"),
              AbilityExecutionIdentity::ResolveTarget(OtherSource, Target, Location));
    TestFalse(
        TEXT("Cross-world Actor cannot capture"),
        AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Other.Enemy), Target, Error));
    TestTrue(TEXT("Recapture original Mass"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget(Id), Target, Error));
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->RemoveMonster(Id);
    TestFalse(TEXT("Removed Mass rejected"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    const int32 NextId = F.SpawnMass();
    TestTrue(TEXT("Next Mass captured"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget(NextId), Target, Error));
    auto& Manager = F.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
    Manager.DestroyEntity(Mass->GetEntity(NextId));
    TestFalse(TEXT("Stale lookup map cannot validate destroyed handle"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    Mass->RemoveMonster(NextId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityIdentityRunBoundary, "test_25d.UnifiedGA.Identity.RunAndCombatPermissions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** Run 变化撤销身份；目标敌对和房间限制仍由生产混合目标入口决定。 */
bool FAbilityIdentityRunBoundary::RunTest(const FString&)
{
    AbilityGraphIdentityTests::FFixture F;
    auto* Director = F.StartRun();
    FAbilitySourceToken Source;
    FAbilityTargetToken Target;
    FString Error;
    FVector Location;
    AActor* Avatar = nullptr;
    TestTrue(TEXT("Run source captured"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    TestTrue(TEXT("Local hostile captured"),
             AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy), Target, Error));
    TestTrue(TEXT("Local hostile resolves"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    F.Enemy->Tags.Remove(TEXT("Enemy"));
    F.Enemy->Tags.Add(TEXT("Player"));
    TestFalse(TEXT("Friendly target rejected on later resolution"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    F.Enemy->Tags.Remove(TEXT("Player"));
    F.Enemy->Tags.Add(TEXT("Enemy"));
    F.Enemy->SetActorLocation(FVector(2400, 0, 0));
    TestFalse(TEXT("Other room rejected"), AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    F.Enemy->SetActorLocation(FVector(100, 0, 0));
    TestTrue(TEXT("Returning to allowed room resolves"),
             AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    Director->RunId = FGuid::NewGuid();
    TestFalse(TEXT("Run change rejects source"), AbilityExecutionIdentity::ResolveSource(Source, Avatar));
    TestTrue(TEXT("New run source captures"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
    TestFalse(TEXT("Old run target cannot use fresh source"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityIdentityAuthority, "test_25d.UnifiedGA.Identity.ServerOnlyAndWorldCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 客户端角色不能 Capture；销毁世界后令牌不保活对象或遗留上下文。 */
bool FAbilityIdentityAuthority::RunTest(const FString&)
{
    const int32 ContextCount = GEngine->GetWorldContexts().Num();
    FAbilitySourceToken Source;
    FAbilityTargetToken Target;
    FString Error;
    AActor* Avatar = nullptr;
    FVector Location;
    {
        AbilityGraphIdentityTests::FFixture F;
        TestTrue(TEXT("Server source captures"), AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error));
        TestTrue(TEXT("Server target captures"), AbilityExecutionIdentity::CaptureTarget(
                                                     Source, FPureMassCombatTarget::FromActor(F.Enemy), Target, Error));
        F.Source->SetTestRole(ROLE_AutonomousProxy);
        FAbilitySourceToken ClientSource;
        FAbilityTargetToken ClientTarget;
        TestFalse(TEXT("Client source capture denied"),
                  AbilityExecutionIdentity::CaptureSource(F.Source, ClientSource, Error));
        TestFalse(TEXT("Client cannot use existing source token to capture"),
                  AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(F.Enemy),
                                                          ClientTarget, Error));
        F.Source->SetTestRole(ROLE_Authority);
    }
    TestEqual(TEXT("No residual WorldContext"), GEngine->GetWorldContexts().Num(), ContextCount);
    TestFalse(TEXT("Destroyed world rejects source"), AbilityExecutionIdentity::ResolveSource(Source, Avatar));
    TestFalse(TEXT("Destroyed world rejects target"),
              AbilityExecutionIdentity::ResolveTarget(Source, Target, Location));
    return true;
}
#endif
