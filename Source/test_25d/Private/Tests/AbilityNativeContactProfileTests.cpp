// 测试调用链：真实BP_FireBolt原Mass桥对照 -> Native contact profile -> 外部payload；以及真实弹反归属转移后的HP/DOT/RNG。
// 原桥不准备死亡力/击退随机数的合同单独记录；累计伤害转移只应冻结数值，不提前准备随机force。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityDeliveryNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "MassEntitySubsystem.h"
#include "MyGameplayTags.h"
#include "EngineUtils.h"
#include "Engine/LatentActionManager.h"
#include "PureMass/PureMassLegacyProjectile.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "test_25d/test_25d.h"

namespace AbilityNativeContactProfileTests
{
    UClass* ProjectileClass()
    {
        return LoadClass<AMyProjectile>(
            nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
    }
    void Initialize(AMyProjectile* P)
    {
        if (!P->IsActorInitialized())
        {
            P->InitializeComponents();
            P->PostInitializeComponents();
        }
        if (!P->HasActorBegunPlay())
        {
            P->DispatchBeginPlay();
        }
    }
    FString FlightState(AMyProjectile* P)
    {
        return FString::Printf(TEXT("pos=%s velocity=%s active=%d query=%d latent=%d adapter=%d"),
                               *P->GetActorLocation().ToString(), *P->ProjectileMovement->Velocity.ToString(),
                               P->ProjectileMovement->IsActive(), P->Sphere->IsQueryCollisionEnabled(),
                               P->GetWorld()->GetLatentActionManager().GetNumActionsForObject(P),
                               P->FindComponentByClass<UPureMassLegacyProjectile>() != nullptr);
    }
    FDamageEffectParams Params()
    {
        FDamageEffectParams P;
        P.BaseDamage = 25.f;
        P.DamageType = FMyGameplayTags::Get().Damage_Fire;
        P.DeathImpulseMagnitude = 900.f;
        P.KnockbackChance = 80.f;
        P.KnockbackForceMagnitude = 500.f;
        return P;
    }
    FGuid Node(UAbilityGraphDefinition* G, FName Type, UAbilityNodeConfig* Config)
    {
        FAbilityGraphNode N;
        N.NodeId = FGuid::NewGuid();
        N.TypeId = Type;
        N.Config = Config;
        G->Nodes.Add(N);
        return N.NodeId;
    }
    void Tick(UWorld* World, UAbilityGraphInstance* Run, float Seconds)
    {
        for (float T = 0.f; T < Seconds; T += .01f)
        {
            TArray<TWeakObjectPtr<AMyProjectile>> Shots;
            for (TActorIterator<AMyProjectile> It(World); It; ++It)
            {
                Shots.Add(*It);
            }
            for (auto P : Shots)
            {
                if (P.IsValid() && !P->IsActorBeingDestroyed())
                {
                    P->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
            }
            if (Run && !Run->IsFinished())
            {
                Run->Advance(.01f);
            }
        }
    }
    float DPS(UWorld* World, UPureMassSubsystem* Mass, int32 Id)
    {
        return World->GetSubsystem<UMassEntitySubsystem>()
            ->GetMutableEntityManager()
            .GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id))
            .StatusDPS;
    }
} // namespace AbilityNativeContactProfileTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMassContactProfileTest,
                                 "test_25d.DynamicMage.NativeFireBolt.OriginalMassContactProfile",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeMassContactProfileTest::RunTest(const FString&)
{
    using namespace AbilityNativeContactProfileTests;
    auto* Class = ProjectileClass();
    if (!TestNotNull(TEXT("Original mass-adapted BP_FireBolt loads"), Class))
    {
        return false;
    }
    FVector OldImpulse = FVector(999, 999, 999);
    int32 OldRandom = 0;
    for (const bool bRuntime : {false, true})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        F.Target->Destroy();
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 10.f;
        Type->XPReward = 0;
        const int32 Id = Mass->SpawnMonster(Type, FVector(200, 0, 0));
        auto* Crowd = F.World->SpawnActor<APureMassCrowd>();
        UAbilityGraphInstance* Run = nullptr;
        if (!bRuntime)
        {
            FAbilityActorProjectileSettings S;
            S.ProjectileClass = Class;
            auto Spawned = AbilityTraditionalActions::SpawnProjectiles(F.SourceToken, Params(), S, 1.f,
                                                                       FVector(60, 0, 0), FVector(1000, 0, 0));
            if (!TestEqual(TEXT("Original bridge control spawns actual BP projectile"), Spawned.Num(), 1))
            {
                return false;
            }
            Initialize(Spawned[0]);
        }
        else
        {
            auto* G = NewObject<UAbilityGraphDefinition>(F.World);
            G->SchemaVersion = 2;
            G->CompatibilityProfile = TEXT("UnifiedV2");
            auto* D = NewObject<UAbilityDeliveryNodeConfig>(G);
            D->Delivery.ProjectileClass = Class;
            D->Delivery.FlightProfile = EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
            D->Delivery.ContactProfile = EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
            D->Delivery.bApplyDamage = false;
            const FGuid Root = Node(G, TEXT("Delivery.ProjectileAwait"), D);
            const FGuid Bridge = Node(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
            const FGuid Effect = Node(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
            G->EntryNodeId = Root;
            G->Edges = {{Root, Bridge, TEXT("Hit")}, {Bridge, Effect, TEXT("TargetFound")}};
            FAbilityNodeInput I;
            I.SourceToken = F.SourceToken;
            I.Origin = FVector(60, 0, 0);
            I.EventCenter = FVector(1000, 0, 0);
            I.Direction = FVector::ForwardVector;
            I.DamageParams = Params();
            Run = NewObject<UAbilityGraphInstance>(F.World);
            FString Error;
            TestTrue(TEXT("Native mass contact payload graph starts"), Run->StartWithInput(G, I, {}, Error));
        }
        for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
        {
            AddInfo(TEXT("BP before latent grace: ") + FlightState(*It));
            F.World->GetLatentActionManager().ProcessLatentActions(*It, .25f);
            AddInfo(TEXT("BP after latent grace: ") + FlightState(*It));
        }
        F.World->GetTimerManager().Tick(.2f); // 同时推进原Timer；不直接改碰撞配置。
        FMath::RandInit(58121);
        Tick(F.World, Run, 2.f);
        const int32 NextRandom = FMath::Rand();
        FPureMassView V;
        AddInfo(FString::Printf(TEXT("Mass contact group=%s nextRandom=%d"),
                                bRuntime ? TEXT("runtime") : TEXT("original"), NextRandom));
        TestFalse(TEXT("Actual physical BP contact kills the original Mass life"), Mass->Resolve(Id, V));
        auto* Property = FindFProperty<FArrayProperty>(APureMassCrowd::StaticClass(), TEXT("PendingDeaths"));
        if (TestNotNull(TEXT("Actual death queue is reflected"), Property))
        {
            FScriptArrayHelper Q(Property, Property->ContainerPtrToValuePtr<void>(Crowd));
            TestEqual(TEXT("One actual Mass death queues once"), Q.Num(), 1);
            if (Q.Num() == 1)
            {
                const auto* Death = reinterpret_cast<const FPureMassDeathPresentation*>(Q.GetRawPtr(0));
                const FVector Impulse(Death->DeathImpulse);
                if (!bRuntime)
                {
                    OldImpulse = Impulse;
                    OldRandom = NextRandom;
                    TestTrue(TEXT("Old BP Mass bridge has no implicit death impulse"), OldImpulse.IsZero());
                }
                else
                {
                    TestTrue(TEXT("Native compatibility preserves old Mass force contract"),
                             Impulse.Equals(OldImpulse));
                    TestEqual(TEXT("Native compatibility preserves old Mass contact RNG consumption"), NextRandom,
                              OldRandom);
                }
            }
        }
        if (Run)
        {
            Run->Cancel();
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeActorContactDirectionTest,
                                 "test_25d.DynamicMage.NativeFireBolt.OriginalActorImpulseDirection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeActorContactDirectionTest::RunTest(const FString&)
{
    using namespace AbilityNativeContactProfileTests;
    FVector OldImpulse = FVector::ZeroVector;
    for (const bool bRuntime : {false, true})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        F.Target->SetActorLocation(FVector(300, 0, 0));
        if (!F.Target->IsActorInitialized())
        {
            F.Target->InitializeComponents();
            F.Target->PostInitializeComponents();
        }
        auto* ASC = F.Target->GetAbilitySystemComponent();
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
        FVector Impulse = FVector::ZeroVector;
        int32 Applied = 0;
        const auto Hook = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
            [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
            {
                ++Applied;
                Impulse = UMyAbilitySystemLibrary::GetDeathImpulse(Spec.GetContext());
            });
        FDamageEffectParams P;
        P.BaseDamage = 25.f;
        P.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        P.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        P.DeathImpulseMagnitude = 900.f;
        AMyProjectile* Shot = nullptr;
        UAbilityGraphInstance* Run = nullptr;
        if (!bRuntime)
        {
            FAbilityActorProjectileSettings S;
            S.ProjectileClass = AMyProjectile::StaticClass();
            S.bConfigureHoming = S.bLaunchHoming = true;
            S.HomingAccelerationMin = S.HomingAccelerationMax = 2300.f;
            auto Spawned = AbilityTraditionalActions::SpawnProjectiles(F.SourceToken, P, S, 1.f, FVector(60, 0, 0),
                                                                       FVector(300, 0, 0), F.Target);
            if (!TestEqual(TEXT("Original Actor homing control spawns"), Spawned.Num(), 1))
            {
                return false;
            }
            Shot = Spawned[0];
            Initialize(Shot);
        }
        else
        {
            auto* G = NewObject<UAbilityGraphDefinition>(F.World);
            G->SchemaVersion = 2;
            G->CompatibilityProfile = TEXT("UnifiedV2");
            auto* D = NewObject<UAbilityDeliveryNodeConfig>(G);
            D->Delivery.ProjectileClass = AMyProjectile::StaticClass();
            D->Delivery.Speed = 300.f;
            D->Delivery.CollisionRadius = 8.f;
            D->Delivery.Motion = EAbilityProjectileMotion::NativeHoming;
            D->Delivery.HomingAccelerationMin = D->Delivery.HomingAccelerationMax = 2300.f;
            D->Delivery.ContactProfile = EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
            D->Delivery.bApplyDamage = false;
            const FGuid Root = Node(G, TEXT("Delivery.ProjectileAwait"), D);
            const FGuid Bridge = Node(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
            const FGuid Payload = Node(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
            G->EntryNodeId = Root;
            G->Edges = {{Root, Bridge, TEXT("Hit")}, {Bridge, Payload, TEXT("TargetFound")}};
            FAbilityNodeInput I;
            I.SourceToken = F.SourceToken;
            I.LiveTargetToken = F.TargetToken;
            I.Origin = FVector(60, 0, 0);
            I.Direction = FVector::ForwardVector;
            I.DamageParams = P;
            Run = NewObject<UAbilityGraphInstance>(F.World);
            FString Error;
            TestTrue(TEXT("Actor native contact profile starts"), Run->StartWithInput(G, I, {}, Error));
            for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
            {
                Shot = *It;
                break;
            }
        }
        if (!TestNotNull(TEXT("Actual homing actor exists"), Shot))
        {
            ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
            return false;
        }
        Shot->Sphere->SetSphereRadius(8.f);
        Shot->ProjectileMovement->InitialSpeed = Shot->ProjectileMovement->MaxSpeed = 300.f;
        Shot->ProjectileMovement->Velocity = FVector(300, 0, 0);
        TestFalse(TEXT("Original class keeps initial actor orientation while homing"),
                  Shot->ProjectileMovement->bRotationFollowsVelocity);
        F.Target->SetActorLocation(FVector(300, 150, 0));
        F.World->SetBegunPlay(true);
        for (int32 Frame = 0; Frame < 300 && Applied == 0; ++Frame)
        {
            if (Run && !Run->IsFinished())
            {
                Run->Advance(0.f); // 刷新本次目标位置，不推进时间或伪造命中。
            }
            if (!Shot->IsActorBeingDestroyed())
            {
                Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
            }
            if (Run && !Run->IsFinished())
            {
                Run->Advance(.01f);
            }
        }
        F.World->SetBegunPlay(false);
        TestEqual(TEXT("Real curved homing contact applies one Actor GE"), Applied, 1);
        TestEqual(TEXT("Actual Actor health is removed once"),
                  ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 75.f);
        if (!bRuntime)
        {
            OldImpulse = Impulse;
            TestTrue(TEXT("Old native impulse follows initial ActorForward rather than curved velocity"),
                     OldImpulse.Equals(FVector(900, 0, 0)));
        }
        else
        {
            TestTrue(TEXT("Native Actor contact profile preserves actual original force direction"),
                     Impulse.Equals(OldImpulse));
        }
        if (Run)
        {
            Run->Cancel();
        }
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Hook);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParryCumulativePayloadTest,
                                 "test_25d.DynamicMage.ContactForces.ParryPreservesCumulativePayload",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParryCumulativePayloadTest::RunTest(const FString&)
{
    using namespace AbilityNativeContactProfileTests;
    auto* Class = ProjectileClass();
    if (!TestNotNull(TEXT("Original reflected BP_FireBolt loads"), Class))
    {
        return false;
    }
    int32 OldParryRandom = 0;
    int32 OldHitRandom = 0;
    for (const bool bManaged : {false, true})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard ScriptGuard;
        F.Source->Tags.Remove(TEXT("Player"));
        F.Source->Tags.AddUnique(TEXT("Enemy"));
        F.Target->Tags.Remove(TEXT("Enemy"));
        F.Target->Tags.AddUnique(TEXT("Player"));
        F.Target->SetActorLocation(FVector(300, 0, 0));
        if (!F.Target->IsActorInitialized())
        {
            F.Target->InitializeComponents();
            F.Target->PostInitializeComponents();
        }
        F.Target->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        F.Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
        auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
        Mass->Enable();
        auto* Type = NewObject<UPureMassMonsterType>(F.World);
        Type->Health = 100.f;
        Type->XPReward = 0;
        const int32 Id = Mass->SpawnMonster(Type, FVector(150, 0, 0));
        auto P = Params();
        P.DebuffChance = 100.f;
        P.DebuffDamage = 8.f;
        P.DebuffDuration = 2.f;
        P.DebuffFrequency = .5f;
        FAbilitySourceToken Source;
        FAbilitySourceToken Defender;
        FString Error;
        AbilityExecutionIdentity::CaptureSource(F.Source, Source, Error);
        AbilityExecutionIdentity::CaptureSource(F.Target, Defender, Error);
        F.World->GetSubsystem<UAbilityWorldTimeSubsystem>()->SampleClockForTesting(10., false);
        FGuardParrySettings GuardSettings;
        GuardSettings.bEnabled = true;
        GuardSettings.bRewardWorldSlow = false;
        auto Guard = AbilityCharacterActions::OpenGuardWindow(Defender, -1.f, GuardSettings);
        AMyProjectile* Shot = nullptr;
        UAbilityDeliveryInstance* Delivery = nullptr;
        if (!bManaged)
        {
            P.BaseDamage *= 2.f;
            P.DebuffDamage *= .25f;
            FAbilityActorProjectileSettings S;
            S.ProjectileClass = Class;
            auto Spawned =
                AbilityTraditionalActions::SpawnProjectiles(Source, P, S, 1.f, FVector(60, 0, 0), FVector(1000, 0, 0));
            if (!TestEqual(TEXT("Original numerically frozen reflection control spawns"), Spawned.Num(), 1))
            {
                return false;
            }
            Shot = Spawned[0];
            Initialize(Shot);
        }
        else
        {
            FAbilityDeliveryRequest R;
            R.Source = Source;
            R.Origin = FVector(60, 0, 0);
            R.Direction = FVector::ForwardVector;
            R.RootCastId = FGuid::NewGuid();
            R.StepId = FGuid::NewGuid();
            R.Settings.ProjectileClass = Class;
            R.Settings.Speed = 300.f;
            R.Settings.CollisionRadius = 8.f;
            R.Settings.bApplyDamage = false;
            R.DamageParams = P;
            R.DamageScale = 2.f;
            R.DebuffDamageScale = .25f;
            Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
            if (!TestTrue(TEXT("Managed cumulative payload starts"), Delivery->Begin(R, Error)))
            {
                return false;
            }
            Shot = Delivery->GetProjectile();
        }
        AddInfo(TEXT("Parry BP before latent grace: ") + FlightState(Shot));
        F.World->GetLatentActionManager().ProcessLatentActions(Shot, .25f);
        AddInfo(TEXT("Parry BP after latent grace: ") + FlightState(Shot));
        F.World->GetTimerManager().Tick(.2f);
        Shot->Sphere->SetSphereRadius(8.f);
        Shot->ProjectileMovement->InitialSpeed = Shot->ProjectileMovement->MaxSpeed = 300.f;
        Shot->ProjectileMovement->Velocity = FVector(300, 0, 0);
        Shot->ProjectileMovement->bIsHomingProjectile = false; // 两组都使用本用例的显式直线飞行合同。
        AddInfo(FString::Printf(TEXT("Parry fixture original class gravity scale=%.6f"),
                                Shot->ProjectileMovement->ProjectileGravityScale));
        // 本用例已显式覆盖速度/半径并关闭追踪；也关闭BP自带重力，保持两组相同的直线接触合同。
        Shot->ProjectileMovement->ProjectileGravityScale = 0.f;
        FMath::RandInit(12371);
        AddInfo(FString::Printf(
            TEXT(
                "Parry original-policy gates group=%s validOverlap=%d guard=%d guardActive=%d sphereResponse=%d capsuleResponse=%d"),
            bManaged ? TEXT("managed") : TEXT("original"), Shot->IsValidOverlap(F.Target),
            UGuardParryComponent::CanParryProjectile(F.Target, Source),
            Defender.ASC->GetActiveGameplayEffect(Guard) != nullptr,
            static_cast<int32>(
                Shot->Sphere->GetCollisionResponseToChannel(F.Target->GetCapsuleComponent()->GetCollisionObjectType())),
            static_cast<int32>(F.Target->GetCapsuleComponent()->GetCollisionResponseToChannel(
                Shot->Sphere->GetCollisionObjectType()))));
        TestTrue(TEXT("Real original guard is open before physical flight"),
                 UGuardParryComponent::CanParryProjectile(F.Target, Source));
        TestTrue(TEXT("Original overlap policy accepts this defender"), Shot->IsValidOverlap(F.Target));
        F.World->SetBegunPlay(true);
        for (int32 Frame = 0; Frame < 100 && !Shot->HasBeenParryReflected() && !Shot->IsActorBeingDestroyed(); ++Frame)
        {
            Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
        }
        AddInfo(TEXT("Parry after physical outbound: ") + FlightState(Shot));
        TestTrue(TEXT("Actual outgoing flight is reflected"), Shot->HasBeenParryReflected());
        TestFalse(TEXT("Reflection leaves the same physical actor in original mode"), Shot->IsGraphDeliveryManaged());
        const int32 AfterParryRandom = FMath::Rand();
        if (bManaged)
        {
            TestTrue(TEXT("Original graph receipt cancels exactly at ownership transfer"),
                     Delivery->IsFinished() && Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Cancelled);
            TestEqual(TEXT("Ownership transfer does not prematurely roll contact forces"), AfterParryRandom,
                      OldParryRandom);
        }
        else
        {
            OldParryRandom = AfterParryRandom;
        }
        FMath::RandInit(7861);
        for (int32 Frame = 0; Frame < 100 && !Shot->IsActorBeingDestroyed(); ++Frame)
        {
            Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
        }
        F.World->SetBegunPlay(false);
        const int32 AfterHitRandom = FMath::Rand();
        FPureMassView V;
        TestTrue(TEXT("Reflected Mass victim survives"), Mass->Resolve(Id, V));
        TestEqual(TEXT("Returned unmanaged contact retains cumulative main damage"), V.Health, 50.f);
        TestEqual(TEXT("Returned unmanaged contact retains independent cumulative DOT damage"), DPS(F.World, Mass, Id),
                  4.f);
        if (bManaged)
        {
            TestEqual(TEXT("Returned contact consumes same original-mode RNG"), AfterHitRandom, OldHitRandom);
        }
        else
        {
            OldHitRandom = AfterHitRandom;
        }
        AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
        if (Delivery)
        {
            Delivery->Cancel();
        }
    }
    return true;
}
#endif
