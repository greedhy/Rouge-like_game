// 测试调用链：真实弹体移动/反射或模拟代理重叠 -> 受管投递边界 -> 图调用结束和资源回收。
// 这些测试分别观察网络快照合同、旧原生追踪参数及取消；不把命中回调直接当作物理接触。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityBasicNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "test_25d/test_25d.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "UObject/Script.h"
#include <limits>

namespace AbilityDeliveryBoundaryTests
{
    FAbilityDeliveryRequest Request(const FAbilityGraphTestFixture& F)
    {
        FAbilityDeliveryRequest R;
        R.Source = F.SourceToken;
        R.Origin = FVector(60, 0, 0);
        R.Direction = FVector::ForwardVector;
        R.RootCastId = FGuid::NewGuid();
        R.StepId = FGuid::NewGuid();
        R.Settings.ProjectileClass = AMyProjectile::StaticClass();
        R.Settings.Speed = 300.f;
        R.Settings.CollisionRadius = 8.f;
        R.Settings.MaxDistance = 900.f;
        R.Settings.bApplyDamage = false;
        return R;
    }

    /** 只改变网络角色；接触仍由真实移动组件的扫掠产生。 */
    AMyProjectile* Proxy(FAbilityGraphTestFixture& F, bool bManaged)
    {
        const FTransform Transform(FRotator::ZeroRotator, FVector(60, 0, 0));
        auto* Shot = F.World->SpawnActorDeferred<AMyProjectile>(AMyProjectile::StaticClass(), Transform, F.Source);
        Shot->DamageEffectParams.SourceAbilitySystemComponent = F.ASC;
        if (bManaged)
        {
            Shot->SetGraphDeliveryHandlers(
                [](const FAbilityTargetToken&, FVector, FVector)
                {
                    return EAbilityProjectileContactDisposition::Continue;
                },
                {});
        }
        Shot->FinishSpawning(Transform);
        if (!Shot->IsActorInitialized())
        {
            Shot->InitializeComponents();
            Shot->PostInitializeComponents();
        }
        if (!Shot->HasActorBegunPlay())
        {
            Shot->DispatchBeginPlay();
        }
        Shot->Sphere->SetSphereRadius(8.f);
        Shot->Sphere->IgnoreActorWhenMoving(F.Source, true);
        Shot->SetRole(ROLE_SimulatedProxy);
        return Shot;
    }

    /** 真实弹反测试使用既有守卫组件和共享原生命令牌。 */
    FActiveGameplayEffectHandle Guard(FAbilityGraphTestFixture& F, FAbilitySourceToken& Defender)
    {
        FString Error;
        AbilityExecutionIdentity::CaptureSource(F.Target, Defender, Error);
        F.World->GetSubsystem<UAbilityWorldTimeSubsystem>()->SampleClockForTesting(10., false);
        FGuardParrySettings Settings;
        Settings.bEnabled = true;
        Settings.bRewardWorldSlow = false;
        return AbilityCharacterActions::OpenGuardWindow(Defender, -1.f, Settings);
    }
} // namespace AbilityDeliveryBoundaryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryManagedSnapshotTest, "test_25d.DynamicMage.DeliveryBoundary.ManagedSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryManagedSnapshotTest::RunTest(const FString&)
{
    auto* Property = FindFProperty<FBoolProperty>(AMyProjectile::StaticClass(), TEXT("bGraphDeliveryManaged"));
    if (TestNotNull(TEXT("Managed flight has a stable reflected replication field"), Property))
    {
        TestTrue(TEXT("Managed mode is replicated"), Property->HasAnyPropertyFlags(CPF_Net));
        TestTrue(TEXT("Managed mode synchronizes movement ownership through RepNotify"),
                 Property->HasAnyPropertyFlags(CPF_RepNotify) && !Property->RepNotifyFunc.IsNone());
    }
    auto* Impact = AMyProjectile::StaticClass()->FindFunctionByName(TEXT("MulticastGraphDeliveryImpact"));
    if (TestNotNull(TEXT("Impact stream has a stable multicast presentation endpoint"), Impact))
    {
        TestTrue(TEXT("Per-contact presentation uses a multicast RPC"),
                 Impact->HasAllFunctionFlags(FUNC_Net | FUNC_NetMulticast));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryProxyContactTest, "test_25d.DynamicMage.DeliveryBoundary.ProxyContact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryProxyContactTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(200, 0, 0));
    auto* Capsule = F.Target->GetCapsuleComponent();
    AddInfo(FString::Printf(TEXT("Proxy fixture before setup: worldBegun=%d capsuleOverlap=%d collision=%d"),
                            F.World->HasBegunPlay(), Capsule->GetGenerateOverlapEvents(),
                            Capsule->IsQueryCollisionEnabled()));
    AddInfo(FString::Printf(TEXT("Proxy target before setup: initialized=%d registered=%d physics=%d"),
                            F.Target->IsActorInitialized(), Capsule->IsRegistered(), Capsule->IsPhysicsStateCreated()));
    // UE的ShouldIgnoreOverlapResult会直接排除尚未PostInitializeComponents的另一个Actor。
    // 只完成组件/Actor初始化；仍不运行敌人的BeginPlay、授予或AI。
    if (!F.Target->IsActorInitialized())
    {
        F.Target->InitializeComponents();
        F.Target->PostInitializeComponents();
    }
    // 空Mesh的原角色胶囊默认不发Overlap；这里显式建立物理接触，不启动角色BeginPlay/AI。
    Capsule->SetGenerateOverlapEvents(true);
    Capsule->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
    auto* Managed = AbilityDeliveryBoundaryTests::Proxy(F, true);
    AddInfo(FString::Printf(
        TEXT(
            "Managed proxy notify gates: begun=%d delegate=%d validTarget=%d sourceASC=%s sourceAvatar=%s playerTag=%d targetEnemyTag=%d"),
        Managed->HasActorBegunPlay(), Managed->Sphere->OnComponentBeginOverlap.IsBound(),
        Managed->IsValidOverlap(F.Target), *GetNameSafe(Managed->DamageEffectParams.SourceAbilitySystemComponent),
        *GetNameSafe(Managed->DamageEffectParams.SourceAbilitySystemComponent
                         ? Managed->DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor()
                         : nullptr),
        F.Source->ActorHasTag(TEXT("Player")), F.Target->ActorHasTag(TEXT("Enemy"))));
    TestTrue(TEXT("Managed proxy BeginPlay has run"), Managed->HasActorBegunPlay());
    TestTrue(TEXT("Managed proxy has its original component overlap delegate"),
             Managed->Sphere->OnComponentBeginOverlap.IsBound());
    TestTrue(TEXT("Managed proxy target is permitted by original overlap policy"), Managed->IsValidOverlap(F.Target));
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
    AddInfo(
        FString::Printf(TEXT("Proxy world user callbacks enabled=%d"), F.World->IsActorTickAndUserCallbacksEnabled()));
#endif
    AddInfo(FString::Printf(
        TEXT("Proxy setup: targetInitialized=%d sphereOverlap=%d capsuleOverlap=%d responses=%d/%d physics=%d/%d"),
        F.Target->IsActorInitialized(), Managed->Sphere->GetGenerateOverlapEvents(),
        Capsule->GetGenerateOverlapEvents(),
        static_cast<int32>(Managed->Sphere->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType())),
        static_cast<int32>(Capsule->GetCollisionResponseToChannel(Managed->Sphere->GetCollisionObjectType())),
        Managed->Sphere->IsPhysicsStateCreated(), Capsule->IsPhysicsStateCreated()));
    FHitResult Hit;
    F.World->SetBegunPlay(true);
    {
        // Actor::ProcessEvent在临时World.AreActorsInitialized=false时会拦截动态UFUNCTION委托。
        // 只开放本次真实碰撞的脚本分发；不手工调用命中、不运行整个世界/敌人BeginPlay。
        FEditorScriptExecutionGuard ScriptGuard;
        Managed->ProjectileMovement->MoveUpdatedComponent(FVector(140, 0, 0), FQuat::Identity, true, &Hit);
    }
    F.World->SetBegunPlay(false);
    AddInfo(FString::Printf(TEXT("Managed proxy actual location=%s overlaps=%d terminal=%d"),
                            *Managed->GetActorLocation().ToString(), Managed->Sphere->IsOverlappingActor(F.Target),
                            Managed->HasTerminalImpactForTesting()));
    TestTrue(TEXT("Managed proxy reaches a real component overlap"), Managed->Sphere->IsOverlappingActor(F.Target));
    TestFalse(TEXT("Actual proxy overlap cannot close a managed multi-impact flight"),
              Managed->HasTerminalImpactForTesting());
    Managed->SetRole(ROLE_Authority);
    Managed->Destroy();

    auto* Original = AbilityDeliveryBoundaryTests::Proxy(F, false);
    AddInfo(FString::Printf(
        TEXT("Original proxy notify gates: begun=%d delegate=%d validTarget=%d sourceASC=%s sourceAvatar=%s"),
        Original->HasActorBegunPlay(), Original->Sphere->OnComponentBeginOverlap.IsBound(),
        Original->IsValidOverlap(F.Target), *GetNameSafe(Original->DamageEffectParams.SourceAbilitySystemComponent),
        *GetNameSafe(Original->DamageEffectParams.SourceAbilitySystemComponent
                         ? Original->DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor()
                         : nullptr)));
    TestTrue(TEXT("Original proxy BeginPlay has run"), Original->HasActorBegunPlay());
    TestTrue(TEXT("Original proxy has its original component overlap delegate"),
             Original->Sphere->OnComponentBeginOverlap.IsBound());
    TestTrue(TEXT("Original proxy target is permitted by original overlap policy"), Original->IsValidOverlap(F.Target));
    F.World->SetBegunPlay(true);
    {
        FEditorScriptExecutionGuard ScriptGuard;
        Original->ProjectileMovement->MoveUpdatedComponent(FVector(140, 0, 0), FQuat::Identity, true, &Hit);
    }
    F.World->SetBegunPlay(false);
    AddInfo(FString::Printf(TEXT("Original proxy actual location=%s overlaps=%d terminal=%d"),
                            *Original->GetActorLocation().ToString(), Original->Sphere->IsOverlappingActor(F.Target),
                            Original->HasTerminalImpactForTesting()));
    TestTrue(TEXT("Original proxy reaches the same physical component overlap"),
             Original->Sphere->IsOverlappingActor(F.Target));
    TestTrue(TEXT("Unmanaged original proxy retains original first-contact presentation"),
             Original->HasTerminalImpactForTesting());
    Original->SetRole(ROLE_Authority);
    Original->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryNativeAccelerationTest,
                                 "test_25d.DynamicMage.DeliveryBoundary.NativeAcceleration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryNativeAccelerationTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto R = AbilityDeliveryBoundaryTests::Request(F);
    R.Target = F.TargetToken;
    R.Settings.Motion = EAbilityProjectileMotion::NativeHoming;
    TestEqual(TEXT("Original FireBolt minimum acceleration remains default"), R.Settings.HomingAccelerationMin, 1600.f);
    TestEqual(TEXT("Original FireBolt maximum acceleration remains default"), R.Settings.HomingAccelerationMax, 3200.f);
    R.Settings.HomingAccelerationMin = R.Settings.HomingAccelerationMax = 1777.f;
    auto* Fixed = NewObject<UAbilityDeliveryInstance>(F.World);
    FString Error;
    if (TestTrue(TEXT("Fixed native acceleration flight starts"), Fixed->Begin(R, Error)))
    {
        TestEqual(TEXT("Fixed configured acceleration reaches the actual native movement component"),
                  Fixed->GetProjectile()->ProjectileMovement->HomingAccelerationMagnitude, 1777.f);
    }
    Fixed->Cancel();
    R.Settings.HomingAccelerationMin = 4000.f;
    R.Settings.HomingAccelerationMax = 5000.f;
    auto* Random = NewObject<UAbilityDeliveryInstance>(F.World);
    if (TestTrue(TEXT("Explicit native acceleration range starts"), Random->Begin(R, Error)))
    {
        const float Actual = Random->GetProjectile()->ProjectileMovement->HomingAccelerationMagnitude;
        TestTrue(TEXT("Each native projectile samples its configured range"), Actual >= 4000.f && Actual <= 5000.f);
    }
    Random->Cancel();
    R.Settings.HomingAccelerationMax = 3000.f;
    TestFalse(TEXT("Inverted acceleration interval is rejected"), R.Settings.Validate(Error));
    R.Settings.HomingAccelerationMin = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Non-finite acceleration is rejected"), R.Settings.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryManualParryMovementTest,
                                 "test_25d.DynamicMage.DeliveryBoundary.ManualParryMovement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryManualParryMovementTest::RunTest(const FString&)
{
    for (auto Motion : {EAbilityProjectileMotion::Returning, EAbilityProjectileMotion::LegacyTurnHoming})
    {
        FAbilityGraphTestFixture F;
        F.Target->SetActorLocation(FVector(300, 0, 0));
        FAbilitySourceToken Defender;
        auto Guard = AbilityDeliveryBoundaryTests::Guard(F, Defender);
        auto R = AbilityDeliveryBoundaryTests::Request(F);
        R.Target = F.TargetToken;
        R.Settings.Motion = Motion;
        auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
        FString Error;
        if (!TestTrue(TEXT("Manually segmented flight starts"), Delivery->Begin(R, Error)))
        {
            AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
            return false;
        }
        auto* Shot = Delivery->GetProjectile();
        Delivery->Advance(1.f);
        TestTrue(TEXT("Actual manual flight contact can be parried"), Shot->HasBeenParryReflected());
        TestTrue(TEXT("Transfer closes the original delivery"),
                 Delivery->IsFinished() && Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Cancelled);
        const FVector Before = Shot->GetActorLocation();
        Shot->ProjectileMovement->TickComponent(.05f, LEVELTICK_All, nullptr);
        TestTrue(TEXT("Transferred flight resumes original movement toward its former caster"),
                 Shot->GetActorLocation().X < Before.X);
        AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
        Delivery->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryGraphParryCancellationTest,
                                 "test_25d.DynamicMage.DeliveryBoundary.GraphParryCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryGraphParryCancellationTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(300, 0, 0));
    FAbilitySourceToken Defender;
    auto Guard = AbilityDeliveryBoundaryTests::Guard(F, Defender);
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Root;
    Root.NodeId = FGuid::NewGuid();
    Root.TypeId = TEXT("Control.DelayOrigin");
    auto* Start = NewObject<UAbilityDelayNodeConfig>(Graph);
    Start->DelaySeconds = .01f;
    Root.Config = Start;
    FAbilityGraphNode Projectile;
    Projectile.NodeId = FGuid::NewGuid();
    Projectile.TypeId = TEXT("Delivery.ProjectileAwait");
    auto* Delivery = NewObject<UAbilityDeliveryNodeConfig>(Graph);
    Delivery->Delivery = AbilityDeliveryBoundaryTests::Request(F).Settings;
    Projectile.Config = Delivery;
    FAbilityGraphNode Sibling;
    Sibling.NodeId = FGuid::NewGuid();
    Sibling.TypeId = TEXT("Control.DelayOrigin");
    auto* SiblingDelay = NewObject<UAbilityDelayNodeConfig>(Graph);
    SiblingDelay->DelaySeconds = 2.f;
    Sibling.Config = SiblingDelay;
    FAbilityGraphNode Follow;
    Follow.NodeId = FGuid::NewGuid();
    Follow.TypeId = TEXT("Control.DelayOrigin");
    auto* FollowDelay = NewObject<UAbilityDelayNodeConfig>(Graph);
    FollowDelay->DelaySeconds = .01f;
    Follow.Config = FollowDelay;
    Graph->EntryNodeId = Root.NodeId;
    Graph->Nodes = {Root, Projectile, Sibling, Follow};
    Graph->Edges = {{Root.NodeId, Projectile.NodeId, TEXT("Completed")},
                    {Root.NodeId, Sibling.NodeId, TEXT("Completed")},
                    {Projectile.NodeId, Follow.NodeId, TEXT("Completed")}};
    auto* Instance = NewObject<UAbilityGraphInstance>(F.World);
    int32 Completed = 0;
    Instance->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            Completed += Event.OutputPort == TEXT("Completed") ? 1 : 0;
        });
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = FVector(60, 0, 0);
    Input.Direction = FVector::ForwardVector;
    FString Error;
    if (!TestTrue(TEXT("Graph with independent sibling branch starts"),
                  Instance->StartWithInput(Graph, Input, {}, Error)))
    {
        AddInfo(Error);
        AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
        return false;
    }
    Instance->Advance(.02f);
    AMyProjectile* Shot = nullptr;
    for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
    {
        Shot = *It;
        break;
    }
    if (TestNotNull(TEXT("Real graph delivery creates an actual projectile"), Shot))
    {
        Shot->ProjectileMovement->TickComponent(1.f, LEVELTICK_All, nullptr);
        Instance->Advance(.01f);
        TestTrue(TEXT("Graph contact uses real parry"), Shot->HasBeenParryReflected());
        TestEqual(TEXT("Only independent sibling remains active after delivery cancellation"),
                  Instance->GetActiveInvocationCount(), 1);
        TestFalse(TEXT("Cancelling one delivery does not cancel the independent sibling"), Instance->IsFinished());
        Instance->Advance(2.1f);
        TestTrue(TEXT("Graph finishes after remaining sibling releases its resources"), Instance->IsFinished());
        TestEqual(TEXT("Cancelled delivery emits no Completed follow-up"), Completed, 2);
        TestEqual(TEXT("No cancelled delivery execution leaks"), Instance->GetActiveInvocationCount(), 0);
    }
    Instance->Cancel();
    AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
    return true;
}
#endif
