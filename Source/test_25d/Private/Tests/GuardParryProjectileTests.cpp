// 测试链：真实格挡GE与弹体Sweep -> 弹反归属转移 -> 原射手伤害、旧施法取消与生命清理。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "PerfectDodgeTestTypes.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityEnemyActionNodeExecutions.h"
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "Actor/EnemySlingProjectile.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/Script.h"
#include "test_25d/test_25d.h"

namespace
{
    /** 角色保留真实ASC/胶囊；没有动画或资产依赖，投射物只通过正常组件Sweep接触。 */
    struct FParryProjectileFixture : FAbilityGraphTestFixture
    {
        FAbilitySourceToken AttackSource;
        UAbilityWorldTimeSubsystem* Time = nullptr;

        FParryProjectileFixture()
        {
            Source->SetActorLocation(FVector(0, 0, 100));
            Target->SetActorLocation(FVector(-300, 0, 100));
            for (AEnemyCharacter* Character : {Source, Target})
            {
                Character->AutoPossessAI = EAutoPossessAI::Disabled;
                if (!Character->IsActorInitialized())
                {
                    Character->PostInitializeComponents();
                }
                Character->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                auto* Capsule = Character->GetCapsuleComponent();
                Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Capsule->SetCollisionObjectType(ECC_Pawn);
                Capsule->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
                Capsule->SetGenerateOverlapEvents(true);
                auto* CharacterASC = Character->GetAbilitySystemComponent();
                CharacterASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
                CharacterASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
            }
            Target->EnableDungeonPooling();
            FString Error;
            AbilityExecutionIdentity::CaptureSource(Target, AttackSource, Error);
            Time = World->GetSubsystem<UAbilityWorldTimeSubsystem>();
            Time->SampleClockForTesting(100., false);
            // 只启用真实重叠委托，不运行角色AI、HUD或整个临时世界的BeginPlay。
            World->SetBegunPlay(true);
        }

        /** 窗口绑定同一个共享Guard GE，时钟由测试显式推进到命中时点。 */
        FActiveGameplayEffectHandle OpenParry()
        {
            FGuardParrySettings Settings;
            Settings.bEnabled = true;
            const auto Guard = AbilityCharacterActions::OpenGuardWindow(SourceToken, -1.f, Settings);
            Time->SampleClockForTesting(100.05, false);
            return Guard;
        }

        /** 原生命、伤害和移动Ignore与生产发射口一致；测试只省略视觉资源。 */
        AMyProjectile* Launch(bool bSling)
        {
            const FTransform Transform(FRotator::ZeroRotator, FVector(-120, 0, 100));
            UClass* Class = bSling ? AEnemySlingProjectile::StaticClass() : AMyProjectile::StaticClass();
            auto* Shot = World->SpawnActorDeferred<AMyProjectile>(Class, Transform, Target, Target,
                                                                  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (auto* Sling = Cast<AEnemySlingProjectile>(Shot))
            {
                Sling->InitializeSource(Target);
            }
            Shot->SetExecutionSourceToken(AttackSource);
            Shot->DamageEffectParams.WorldContextObject = Target;
            Shot->DamageEffectParams.SourceAbilitySystemComponent = AttackSource.ASC.Get();
            Shot->DamageEffectParams.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
            Shot->DamageEffectParams.BaseDamage = 37.f;
            Shot->DamageEffectParams.DebuffDamage = 7.f;
            Shot->Sphere->SetSphereRadius(10.f);
            Shot->Sphere->SetGenerateOverlapEvents(true);
            Shot->Sphere->IgnoreActorWhenMoving(Target, true);
            Shot->ProjectileMovement->bInitialVelocityInLocalSpace = false;
            Shot->ProjectileMovement->InitialSpeed = 1000.f;
            Shot->ProjectileMovement->MaxSpeed = 1000.f;
            Shot->ProjectileMovement->Velocity = FVector(1000, 0, 0);
            Shot->FinishSpawning(Transform);
            if (!Shot->IsActorInitialized())
            {
                // 临时世界未初始化全部Actor；补齐正常Spawn路线中激活/初始化移动组件的步骤。
                // 只调用PostInitializeComponents会留下inactive移动组件，真实Tick因此不会进行Sweep。
                Shot->InitializeComponents();
                Shot->PostInitializeComponents();
            }
            if (!Shot->HasActorBegunPlay())
            {
                Shot->DispatchBeginPlay();
            }
            Shot->SetLifeSpan(2.5f);
            return Shot;
        }

        void SweepAcrossDefender(AMyProjectile* Shot) const
        {
            FHitResult Hit;
            Shot->Sphere->MoveComponent(FVector(220, 0, 0), Shot->GetActorQuat(), true, &Hit);
        }

        void SweepBackToShooter(AMyProjectile* Shot) const
        {
            FHitResult Hit;
            Shot->Sphere->MoveComponent(FVector(-500, 0, 0), Shot->GetActorQuat(), true, &Hit);
        }
    };

    /** 提供真实WorldStatic阻挡，覆盖Sling Tick的生产预扫路径。 */
    void AddParryWall(UWorld* World, const FVector& Position = FVector(150, 0, 100))
    {
        auto* Wall = World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Wall);
        Wall->SetRootComponent(Box);
        Wall->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(10, 200, 100));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();
        Wall->SetActorLocation(Position);
        if (!Wall->IsActorInitialized())
        {
            Wall->PostInitializeComponents();
        }
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryProjectileDamageTest,
                                 "test_25d.UnifiedGA.GuardParry.ProjectileContactReturnsActualDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryProjectileDamageTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    for (const bool bSling : {false, true})
    {
        FParryProjectileFixture F;
        TestTrue(TEXT("Real guard GE opens"), F.OpenParry().IsValid());
        auto* Shot = F.Launch(bSling);
        Shot->ProjectileMovement->bIsHomingProjectile = true;
        Shot->ProjectileMovement->HomingTargetComponent = F.Source->GetRootComponent();
        int32 OriginalCallbacks = 0;
        Shot->DamageApplied.AddLambda(
            [&](AActor*)
            {
                ++OriginalCallbacks;
            });
        const float RemainingLife = Shot->GetLifeSpan();
        F.SweepAcrossDefender(Shot);
        TestTrue(TEXT("Actual incoming swept contact reflects"), Shot->HasBeenParryReflected());
        TestFalse(TEXT("Reflection has no terminal impact"), Shot->IsActorBeingDestroyed());
        TestEqual(TEXT("Incoming contact does not damage guard owner"),
                  F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
        TestFalse(TEXT("Incoming debuff is negated"),
                  F.ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Test.PerfectDodge.HitDebuff"))));
        TestTrue(TEXT("Current velocity reverses with original speed"),
                 Shot->ProjectileMovement->Velocity.Equals(FVector(-1000, 0, 0), .01f));
        TestEqual(TEXT("Frozen damage survives ownership transfer"), Shot->DamageEffectParams.BaseDamage, 37.f);
        TestEqual(TEXT("Frozen debuff survives ownership transfer"), Shot->DamageEffectParams.DebuffDamage, 7.f);
        TestEqual(TEXT("Transfer does not extend remaining life"), Shot->GetLifeSpan(), RemainingLife);
        TestEqual(TEXT("Defender owns returned shot"), Shot->GetOwner(), static_cast<AActor*>(F.Source));
        TestEqual(TEXT("Defender becomes instigator"), Shot->GetInstigator(), static_cast<APawn*>(F.Source));
        TestEqual(TEXT("Returned damage uses defender ASC"),
                  Shot->DamageEffectParams.SourceAbilitySystemComponent.Get(), F.ASC);
        TestNull(TEXT("No old pending target survives"), Shot->DamageEffectParams.TargetAbilitySystemComponent.Get());
        TestFalse(TEXT("Old homing cannot turn the projectile back"), Shot->ProjectileMovement->bIsHomingProjectile);
        TestFalse(TEXT("One projectile cannot bounce indefinitely"), Shot->TryReflectFrom(F.AttackSource));

        int32 ReturnedCallbacks = 0;
        UAbilitySystemComponent* DamageInstigator = nullptr;
        Shot->DamageApplied.AddLambda(
            [&](AActor*)
            {
                ++ReturnedCallbacks;
            });
        auto* ShooterASC = F.Target->GetAbilitySystemComponent();
        ShooterASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                DamageInstigator = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
            });
        F.SweepBackToShooter(Shot);
        TestEqual(TEXT("Real return contact damages original shooter"),
                  ShooterASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
        TestTrue(
            TEXT("Return contact retains damage debuff"),
            ShooterASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Test.PerfectDodge.HitDebuff"))));
        TestEqual(TEXT("Effect context attributes hit to defender"), DamageInstigator, F.ASC);
        TestEqual(TEXT("Original caster cannot receive returned damage callback"), OriginalCallbacks, 0);
        TestEqual(TEXT("Returned shot publishes one actual hit"), ReturnedCallbacks, 1);
        TestTrue(TEXT("Return impact terminates shot normally"), Shot->IsActorBeingDestroyed());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryProjectileLifetimeTest,
                                 "test_25d.UnifiedGA.GuardParry.ProjectileTransfersLifeAndRejectsPooledShooter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryProjectileLifetimeTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    for (const bool bSling : {false, true})
    {
        FParryProjectileFixture F;
        F.OpenParry();
        auto* Shot = F.Launch(bSling);
        F.SweepAcrossDefender(Shot);
        F.Target->Die(FVector::ZeroVector);
        TestTrue(TEXT("Original source really enters its dead pool life"), F.Target->IsDead_Implementation());
        Shot->Tick(.01f);
        TestFalse(TEXT("Original source death no longer retires reflected shot"), Shot->IsActorBeingDestroyed());
        F.Target->SleepForDungeonPool();
        TestTrue(TEXT("Original shooter starts a new pool life"),
                 F.Target->WakeFromDungeonPool(FTransform(FVector(-300, 0, 100)), TEXT("ParryNextLife")));
        TestFalse(TEXT("Returned shot cannot target old shooter new life"), Shot->IsValidOverlap(F.Target));
        F.SweepBackToShooter(Shot);
        TestFalse(TEXT("Stale original shooter contact does not consume return shot"), Shot->IsActorBeingDestroyed());
        TestEqual(TEXT("New shooter life receives no stale damage"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  100.f);
        F.Source->Die(FVector::ZeroVector);
        TestTrue(TEXT("Current defender death retires reflected shot"), Shot->IsActorBeingDestroyed());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryProjectileWorldSlowTest,
                                 "test_25d.UnifiedGA.GuardParry.ReflectedProjectileWorldSlowCompensation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryProjectileWorldSlowTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    for (const bool bSling : {false, true})
    {
        FParryProjectileFixture F;
        auto Guard = F.OpenParry();
        auto* Shot = F.Launch(bSling);
        F.SweepAcrossDefender(Shot);
        TestTrue(TEXT("Actual contact transfers the shot before slow reward"), Shot->HasBeenParryReflected());
        const auto* Parry = F.Source->FindComponentByClass<UGuardParryComponent>();
        const FGuid Reward = Parry->GetRewardLease();
        TestTrue(TEXT("Reflected contact earns the independent time lease"), F.Time->IsActive(Reward));
        F.Time->SampleClockForTesting(100.11, false);
        TestEqual(TEXT("World reaches default one fifth speed"), F.World->GetWorldSettings()->TimeDilation, .2f);
        TestEqual(TEXT("Defender receives compensation"), F.Source->CustomTimeDilation, 5.f);
        TestEqual(TEXT("Already existing reflected projectile joins defender compensation"), Shot->CustomTimeDilation,
                  5.f);
        TestEqual(TEXT("Original shooter remains subject to slowed world"), F.Target->CustomTimeDilation, 1.f);
        AbilityCharacterActions::CloseGuardWindow(F.ASC, Guard);
        TestTrue(TEXT("Guard completion preserves earned reward"), F.Time->IsActive(Reward));
        F.Time->SampleClockForTesting(101.46, false);
        TestFalse(TEXT("Reward expires naturally from original contact time"), F.Time->IsActive(Reward));
        TestEqual(TEXT("Natural expiration restores world"), F.World->GetWorldSettings()->TimeDilation, 1.f);
        TestEqual(TEXT("Natural expiration restores defender"), F.Source->CustomTimeDilation, 1.f);
        TestEqual(TEXT("Natural expiration restores returned projectile"), Shot->CustomTimeDilation, 1.f);
        TestFalse(TEXT("Time reward expiration does not retire the live return shot"), Shot->IsActorBeingDestroyed());
        F.SweepBackToShooter(Shot);
        TestEqual(TEXT("Real return contact still damages shooter after reward expiration"),
                  F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
                  90.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryProjectileWindowTest,
                                 "test_25d.UnifiedGA.GuardParry.ProjectileLateContactUsesOriginalPipeline",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryProjectileWindowTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FParryProjectileFixture F;
    F.OpenParry();
    F.Time->SampleClockForTesting(100.2, false);
    auto* Shot = F.Launch(false);
    F.SweepAcrossDefender(Shot);
    TestFalse(TEXT("Late guard contact cannot reflect"), Shot->HasBeenParryReflected());
    TestEqual(TEXT("Late projectile reaches original damage pipeline"),
              F.ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestTrue(TEXT("Late impact retains original terminal behavior"), Shot->IsActorBeingDestroyed());
    TestFalse(TEXT("Orbit contract explicitly rejects generic reflection"),
              GetDefault<AMyOrbitingFireBolt>()->SupportsParryReflection());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParrySlingWallTest,
                                 "test_25d.UnifiedGA.GuardParry.SlingReflectionInvalidatesPredictedWall",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParrySlingWallTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FParryProjectileFixture F;
    F.OpenParry();
    auto* Shot = F.Launch(true);
    AddParryWall(F.World);
    // Production Tick finds the wall, then its real MoveComponent crosses the defender first.
    Shot->Tick(.4f);
    TestTrue(TEXT("Real Sling wall sweep reaches parry contact"), Shot->HasBeenParryReflected());
    TestFalse(TEXT("Old predicted wall cannot terminate reflected path"), Shot->IsActorBeingDestroyed());
    TestTrue(TEXT("Return path starts at contact before the defender"), Shot->GetActorLocation().X < 0.);
    F.SweepBackToShooter(Shot);
    TestEqual(TEXT("Sling survives obsolete wall and damages shooter on return"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    TestTrue(TEXT("Real return impact still terminates Sling"), Shot->IsActorBeingDestroyed());

    FParryProjectileFixture ReturnWall;
    ReturnWall.OpenParry();
    auto* BlockedShot = ReturnWall.Launch(true);
    ReturnWall.SweepAcrossDefender(BlockedShot);
    AddParryWall(ReturnWall.World, FVector(-200, 0, 100));
    BlockedShot->Tick(.4f);
    TestTrue(TEXT("A real wall on the new return path still terminates Sling"), BlockedShot->IsActorBeingDestroyed());
    TestEqual(
        TEXT("A real return wall protects the shooter behind it"),
        ReturnWall.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()),
        100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryProjectileQueuedContactTest,
                                 "test_25d.UnifiedGA.GuardParry.ProjectileIgnoresOldSweepContacts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParryProjectileQueuedContactTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FParryProjectileFixture F;
    F.OpenParry();
    auto* Shot = F.Launch(false);
    AddParryWall(F.World);
    // A single engine movement can queue both defender and later wall overlaps.
    // Once the defender reverses the shot, contacts beyond it belong to the invalidated outgoing segment.
    TestTrue(TEXT("Fixture completes normal projectile component initialization"),
             Shot->ProjectileMovement->HasBeenInitialized());
    TestTrue(TEXT("Native projectile movement is active before simulation"), Shot->ProjectileMovement->IsActive());
    TestEqual(TEXT("Native movement updates the real collision sphere"),
              Shot->ProjectileMovement->UpdatedComponent.Get(), static_cast<USceneComponent*>(Shot->Sphere.Get()));
    Shot->ProjectileMovement->TickComponent(.4f, LEVELTICK_All, &Shot->ProjectileMovement->PrimaryComponentTick);
    if (!Shot->HasBeenParryReflected())
    {
        AddInfo(FString::Printf(
            TEXT("Flight state: position=%s velocity=%s active=%d simulation=%d updated=%s destroyed=%d"),
            *Shot->GetActorLocation().ToString(), *Shot->ProjectileMovement->Velocity.ToString(),
            Shot->ProjectileMovement->IsActive(), Shot->ProjectileMovement->bSimulationEnabled,
            *GetNameSafe(Shot->ProjectileMovement->UpdatedComponent), Shot->IsActorBeingDestroyed()));
    }
    TestTrue(TEXT("Engine movement generates real parry contact"), Shot->HasBeenParryReflected());
    TestFalse(TEXT("Queued old-direction wall cannot consume reflected projectile"), Shot->IsActorBeingDestroyed());
    TestTrue(TEXT("Engine preserves velocity changed by overlap callback"),
             Shot->ProjectileMovement->Velocity.Equals(FVector(-1000, 0, 0), .01f));
    F.SweepBackToShooter(Shot);
    TestEqual(TEXT("Returned ordinary shot still applies real damage after queued contacts"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParrySlingCancellationTest,
                                 "test_25d.UnifiedGA.GuardParry.SlingSurvivesOriginalGraphCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGuardParrySlingCancellationTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FParryProjectileFixture F;
    F.OpenParry();
    auto* Settings = NewObject<USlingAttackConfig>(F.World);
    Settings->ProjectileClass = AEnemySlingProjectile::StaticClass();
    Settings->MuzzleSocket = NAME_None;
    Settings->Speed = 1000.f;
    auto* Config = NewObject<UAbilityEnemySlingNodeConfig>(F.World);
    Config->Settings = Settings;
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Delivery.EnemySling");
    Node.Config = Config;
    Graph->EntryNodeId = Node.NodeId;
    Graph->Nodes.Add(Node);
    FAbilityNodeInput Input;
    Input.SourceToken = F.AttackSource;
    Input.EventCenter = F.Source->GetActorLocation();
    Input.DamageParams.SourceAbilitySystemComponent = F.AttackSource.ASC.Get();
    Input.DamageParams.DamageGameplayEffectClass = UPerfectDodgeHitTestEffect::StaticClass();
    FString Error;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    if (!TestTrue(TEXT("Original Sling graph starts"), Run->StartWithInput(Graph, Input, {}, Error)))
    {
        AddError(Error);
        return false;
    }
    AEnemySlingProjectile* Shot = nullptr;
    for (TActorIterator<AEnemySlingProjectile> It(F.World); It; ++It)
    {
        Shot = *It;
    }
    if (!TestNotNull(TEXT("Production Sling action creates the shot"), Shot))
    {
        return false;
    }
    Shot->SetActorLocation(FVector(-120, 0, 100));
    if (!Shot->IsActorInitialized())
    {
        Shot->InitializeComponents();
        Shot->PostInitializeComponents();
    }
    if (!Shot->HasActorBegunPlay())
    {
        Shot->DispatchBeginPlay();
    }
    F.SweepAcrossDefender(Shot);
    TestTrue(TEXT("Production shot reflected before graph cancellation"), Shot->HasBeenParryReflected());
    Run->Cancel();
    TestFalse(TEXT("Original graph cancellation releases rather than destroys transferred shot"),
              Shot->IsActorBeingDestroyed());
    F.SweepBackToShooter(Shot);
    TestEqual(TEXT("Transferred production Sling still damages original shooter"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 90.f);
    return true;
}
#endif
