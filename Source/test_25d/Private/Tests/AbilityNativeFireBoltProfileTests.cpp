// 测试调用链：原发射动作/真实ASC Notify对照 -> 原class真实弹体 -> profile投递/外部payload。
// Native profile在本轮只有声明；测试记录旧Actor与Mass桥合同，禁止以Unified表现冒充零修改等价。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityNativeFireBoltProfileTestTypes.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityGraphWorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "Actor/MyProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassLegacyProjectile.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "test_25d/test_25d.h"

namespace AbilityNativeFireBoltProfileTests
{
    /** 补齐真实世界正常Spawn已有的初始化；不启动敌人BeginPlay/AI。 */
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
    FAbilityActorProjectileSettings OriginalSettings(UClass* Class, bool bHoming = true)
    {
        FAbilityActorProjectileSettings S;
        S.ProjectileClass = Class;
        S.Count = 1;
        S.bConfigureHoming = true;
        S.bLaunchHoming = bHoming;
        S.HomingAccelerationMin = S.HomingAccelerationMax = 2300.f;
        return S;
    }
    FAbilityDeliveryRequest Request(FAbilityGraphTestFixture& F, UClass* Class)
    {
        FAbilityDeliveryRequest R;
        R.Source = F.SourceToken;
        R.Target = F.TargetToken;
        R.Origin = FVector::ZeroVector;
        R.Direction = FVector::ForwardVector;
        R.RootCastId = FGuid::NewGuid();
        R.StepId = FGuid::NewGuid();
        R.Settings.ProjectileClass = Class;
        R.Settings.FlightProfile = EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
        R.Settings.ContactProfile = EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
        R.Settings.NativeConfigureHoming = true;
        R.Settings.Motion = EAbilityProjectileMotion::NativeHoming;
        R.Settings.HomingAccelerationMin = R.Settings.HomingAccelerationMax = 2300.f;
        R.Settings.bApplyDamage = false;
        return R;
    }
    TArray<AMyProjectile*> Shots(UWorld* World)
    {
        TArray<AMyProjectile*> Result;
        for (TActorIterator<AMyProjectile> It(World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed())
            {
                Result.Add(*It);
            }
        }
        Result.Sort(
            [](const AMyProjectile& A, const AMyProjectile& B)
            {
                return A.GetActorForwardVector().Rotation().Yaw < B.GetActorForwardVector().Rotation().Yaw;
            });
        return Result;
    }
    struct FRecord
    {
        UClass* Class = nullptr;
        FVector Direction = FVector::ZeroVector;
        float Speed = 0.f;
        float Radius = 0.f;
        float Life = 0.f;
        float Acceleration = 0.f;
        float Damage = 0.f;
        float Debuff = 0.f;
        bool bHoming = false;
    };
    FRecord Record(AMyProjectile* P)
    {
        Initialize(P);
        return {P->GetClass(),
                P->GetActorForwardVector(),
                P->ProjectileMovement->InitialSpeed,
                P->Sphere->GetScaledSphereRadius(),
                P->GetLifeSpan(),
                P->ProjectileMovement->HomingAccelerationMagnitude,
                P->DamageEffectParams.BaseDamage,
                P->DamageEffectParams.DebuffDamage,
                P->ProjectileMovement->bIsHomingProjectile != 0};
    }
    struct FNotifyFixture : FAbilityGraphTestFixture
    {
        FEditorScriptExecutionGuard Guard;
        FGameplayAbilitySpecHandle Handle;
        UAbilityNativeFireBoltProfileTest* Ability = nullptr;
        FNotifyFixture()
        {
            Target->Destroy();
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(UAbilityNativeFireBoltProfileTest::StaticClass(), 1));
            Ability = CastChecked<UAbilityNativeFireBoltProfileTest>(
                ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
        }
        ~FNotifyFixture()
        {
            ASC->CancelAllAbilities();
            ASC->ClearAllAbilities();
        }
        bool NoOp(FString& Error)
        {
            auto* C = NewObject<UAbilityRuntimeRecipeComponent>(Source);
            Source->AddInstanceComponent(C);
            C->RegisterComponent();
            auto* G = Ability->BuildRuntimeBaseGraph(World, Error);
            if (!G || !C->RegisterBase(FMyGameplayTags::Get().Abilities_Fire_FireBolt, G, Error))
            {
                return false;
            }
            FAbilityRuntimeRecipeContribution NoOp;
            NoOp.SourceId = TEXT("NativeParity.NoOp");
            return C->SetContribution(FMyGameplayTags::Get().Abilities_Fire_FireBolt, NoOp, Error);
        }
        void Gear()
        {
            auto* E = NewObject<UUniqueEquipmentGameplayEffect>(World);
            E->ProjectileRule.AbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
            E->ProjectileRule.ExtraProjectiles = FScalableFloat(2.f);
            E->ProjectileRule.DamageMultiplier = FScalableFloat(.55f);
            E->ProjectileRule.MinimumSpread = FScalableFloat(40.f);
            E->ProjectileRule.bDisableHoming = true;
            FGameplayEffectSpec Spec(E, ASC->MakeEffectContext(), 1.f);
            ASC->ApplyGameplayEffectSpecToSelf(Spec);
        }
    };
} // namespace AbilityNativeFireBoltProfileTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFireBoltSplitDefinitionTest,
                                 "test_25d.DynamicMage.NativeFireBolt.SplitDefaultDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFireBoltSplitDefinitionTest::RunTest(const FString&)
{
    FString Error;
    auto* G = GetDefault<UAbilityNativeFireBoltProfileTest>()->BuildRuntimeBaseGraph(GetTransientPackage(), Error);
    if (!TestNotNull(TEXT("Real native FireBolt exports its default definition"), G))
    {
        return false;
    }
    auto Has = [&](FName Type)
    {
        return G->Nodes.ContainsByPredicate(
            [&](const FAbilityGraphNode& N)
            {
                return N.TypeId == Type;
            });
    };
    TestTrue(TEXT("Native default separates distribution"), Has(TEXT("Distribution.Scatter")));
    TestTrue(TEXT("Native default waits for the real projectile"), Has(TEXT("Delivery.ProjectileAwait")));
    TestTrue(TEXT("Native default exposes replaceable input payload"), Has(TEXT("Effect.InputDamage")));
    TestFalse(TEXT("Native default no longer hides flight/payload inside opaque spawn"),
              Has(TEXT("Delivery.ActorProjectiles")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFireBoltClassDefaultsTest,
                                 "test_25d.DynamicMage.NativeFireBolt.RealClassFlightDefaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFireBoltClassDefaultsTest::RunTest(const FString&)
{
    using namespace AbilityNativeFireBoltProfileTests;
    auto* BP = LoadClass<AMyProjectile>(
        nullptr, TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/BP_FireBolt.BP_FireBolt_C"));
    if (!TestNotNull(TEXT("Actual original FireBolt projectile class loads"), BP))
    {
        return false;
    }
    for (UClass* Class : {AMyProjectile::StaticClass(), BP})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard Guard;
        F.Target->SetActorLocation(FVector(3000, 0, 0));
        auto Old = AbilityTraditionalActions::SpawnProjectiles(F.SourceToken, {}, OriginalSettings(Class), 1.f,
                                                               FVector::ZeroVector, FVector(3000, 0, 0), F.Target);
        if (!TestEqual(TEXT("Original class control creates one projectile"), Old.Num(), 1))
        {
            return false;
        }
        const auto Expected = Record(Old[0]);
        auto R = Request(F, Class);
        auto* D = NewObject<UAbilityDeliveryInstance>(F.World);
        FString Error;
        if (!TestTrue(TEXT("Native class-default flight starts"), D->Begin(R, Error)))
        {
            AddInfo(Error);
            return false;
        }
        const auto Actual = Record(D->GetProjectile());
        TestEqual(TEXT("Original actual class remains selected"), Actual.Class, Expected.Class);
        TestEqual(TEXT("Original class initial speed is preserved"), Actual.Speed, Expected.Speed);
        TestEqual(TEXT("Original class collision sphere is preserved"), Actual.Radius, Expected.Radius);
        TestTrue(TEXT("Original actor lifespan is preserved"), FMath::IsNearlyEqual(Actual.Life, Expected.Life, .01f));
        TestEqual(TEXT("Native homing acceleration remains configured"), Actual.Acceleration, Expected.Acceleration);
        // 原Actor仍有寿命时，profile不能被Explicit默认5秒提前截断。
        if (Expected.Life > 6.f)
        {
            D->Advance(Expected.Life * .75f);
            TestFalse(TEXT("Class-native lifetime is not shortened by generic explicit timeout"), D->IsFinished());
        }
        D->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFireBoltPointConfigureTest,
                                 "test_25d.DynamicMage.NativeFireBolt.PointAimAndDisabledHomingRNG",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFireBoltPointConfigureTest::RunTest(const FString&)
{
    using namespace AbilityNativeFireBoltProfileTests;
    for (const bool bLaunch : {false, true})
    {
        FAbilityGraphTestFixture F;
        FEditorScriptExecutionGuard Guard;
        const FVector Aim(1000, 300, 100);
        auto S = OriginalSettings(AMyProjectile::StaticClass(), bLaunch);
        FMath::RandInit(8712);
        auto Old =
            AbilityTraditionalActions::SpawnProjectiles(F.SourceToken, {}, S, 1.f, FVector::ZeroVector, Aim, nullptr);
        if (!TestEqual(TEXT("Old point-aim launch creates one actual actor"), Old.Num(), 1))
        {
            return false;
        }
        Initialize(Old[0]);
        const int32 OldNextRandom = FMath::Rand();
        auto R = Request(F, AMyProjectile::StaticClass());
        R.Target.Reset();
        R.AimPoint = Aim;
        R.Settings.Motion = bLaunch ? EAbilityProjectileMotion::NativeHoming : EAbilityProjectileMotion::Linear;
        auto* D = NewObject<UAbilityDeliveryInstance>(F.World);
        FString Error;
        FMath::RandInit(8712);
        if (TestTrue(TEXT("Native point aim does not require a live Actor token"), D->Begin(R, Error)))
        {
            auto* P = D->GetProjectile();
            TestEqual(TEXT("Configure and launch homing remain independent"),
                      P->ProjectileMovement->bIsHomingProjectile, bLaunch);
            TestEqual(TEXT("Even disabled native homing preserves original configured acceleration"),
                      P->ProjectileMovement->HomingAccelerationMagnitude,
                      Old[0]->ProjectileMovement->HomingAccelerationMagnitude);
            const auto Target = P->ProjectileMovement->HomingTargetComponent.Get();
            TestTrue(TEXT("Native point target keeps the actual requested world point"),
                     Target && Target->GetComponentLocation().Equals(Aim));
            TestEqual(TEXT("Native setup consumes the same one per-projectile random sample"), FMath::Rand(),
                      OldNextRandom);
        }
        D->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFireBoltTargetLossTest,
                                 "test_25d.DynamicMage.NativeFireBolt.OriginalLifeAndLastAimPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFireBoltTargetLossTest::RunTest(const FString&)
{
    using namespace AbilityNativeFireBoltProfileTests;
    FAbilityGraphTestFixture F;
    FEditorScriptExecutionGuard Guard;
    F.Target->SetActorLocation(FVector(1000, 0, 0));
    F.Target->EnableDungeonPooling(0.f);
    FString Error;
    FAbilityTargetToken Target;
    AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget::FromActor(F.Target), Target, Error);
    auto R = Request(F, AMyProjectile::StaticClass());
    R.Target = Target;
    R.Settings.TargetLossPolicy = EAbilityDeliveryTargetLossPolicy::KeepLastAimPoint;
    auto* D = NewObject<UAbilityDeliveryInstance>(F.World);
    if (!TestTrue(TEXT("Original-life homing starts"), D->Begin(R, Error)))
    {
        return false;
    }
    auto* P = D->GetProjectile();
    F.Target->SleepForDungeonPool();
    TestTrue(TEXT("Target wakes with a new actual pool lease"),
             F.Target->WakeFromDungeonPool(FTransform(FVector(1000, 600, 0)), NAME_None));
    D->Advance(.01f);
    TestFalse(TEXT("Explicit last-aim policy continues harmless flight after original target life retires"),
              D->IsFinished());
    if (!D->IsFinished())
    {
        auto* Aim = P->ProjectileMovement->HomingTargetComponent.Get();
        TestTrue(TEXT("The new lease location cannot replace original last aim"),
                 Aim && Aim->GetComponentLocation().Equals(FVector(1000, 0, 0)));
    }
    D->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFireBoltNotifyEquipmentTest,
                                 "test_25d.DynamicMage.NativeFireBolt.NotifyGearOnceAndAwaitOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeFireBoltNotifyEquipmentTest::RunTest(const FString&)
{
    using namespace AbilityNativeFireBoltProfileTests;
    TArray<FRecord> OldRecords;
    int32 OldRandom = 0;
    for (const bool bRuntime : {false, true})
    {
        FNotifyFixture F;
        F.Gear();
        FString Error;
        if (bRuntime && !TestTrue(TEXT("No-op contribution installs original runtime definition"), F.NoOp(Error)))
        {
            AddInfo(Error);
            return false;
        }
        TestTrue(TEXT("Real FireBolt ASC frontend activates"), F.ASC->TryActivateAbility(F.Handle));
        TestEqual(TEXT("No projectile exists before actual notification"), Shots(F.World).Num(), 0);
        // 原蓝图的Commit已发生，动态图通知不能再次扣费。
        TestTrue(
            TEXT("Original frontend commit succeeds"),
            F.Ability->CommitAbility(F.Handle, F.ASC->AbilityActorInfo.Get(), F.Ability->GetCurrentActivationInfo()));
        FMath::RandInit(2419);
        F.Ability->SpawnProjectiles(FVector(1000, 100, 120), {}, true, 35.f, nullptr);
        auto ActualShots = Shots(F.World);
        TestEqual(TEXT("Level1 base one plus gear extra two yields three actual shots"), ActualShots.Num(), 3);
        TArray<FRecord> Records;
        for (auto* P : ActualShots)
        {
            Records.Add(Record(P));
            TestTrue(TEXT("Notify pitch reaches the original physical transform"),
                     FMath::IsNearlyEqual(P->GetActorRotation().Pitch, 35., 1.e-5));
            TestTrue(TEXT("Gear damage multiplier is applied once"),
                     FMath::IsNearlyEqual(P->DamageEffectParams.BaseDamage, 13.75f));
            TestTrue(TEXT("Gear debuff multiplier is applied once"),
                     FMath::IsNearlyEqual(P->DamageEffectParams.DebuffDamage, 4.4f));
            TestFalse(TEXT("Gear disables actual homing"), P->ProjectileMovement->bIsHomingProjectile);
        }
        const int32 NextRandom = FMath::Rand();
        TestEqual(TEXT("Release costs exactly once"), F.ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()),
                  90.f);
        if (!bRuntime)
        {
            OldRecords = Records;
            OldRandom = NextRandom;
        }
        else
        {
            for (int32 I = 0; I < FMath::Min(OldRecords.Num(), Records.Num()); ++I)
            {
                TestEqual(TEXT("No-op keeps actual original class"), Records[I].Class, OldRecords[I].Class);
                TestTrue(TEXT("No-op keeps original angle order/pitch"),
                         Records[I].Direction.Equals(OldRecords[I].Direction));
                TestEqual(TEXT("No-op keeps original class speed"), Records[I].Speed, OldRecords[I].Speed);
                TestEqual(TEXT("No-op keeps original class sphere"), Records[I].Radius, OldRecords[I].Radius);
                TestEqual(TEXT("Homing setup still samples original acceleration when launch is disabled"),
                          Records[I].Acceleration, OldRecords[I].Acceleration);
            }
            TestEqual(TEXT("No-op keeps actual native random consumption"), NextRandom, OldRandom);
            auto* Owner = F.World->GetSubsystem<UAbilityGraphWorldSubsystem>();
            TestEqual(TEXT("Real await graph remains world-owned until physical flight completes"),
                      Owner->GetActiveExecutionCount(), 1);
            F.Ability->EndAbility(F.Handle, F.ASC->AbilityActorInfo.Get(), F.Ability->GetCurrentActivationInfo(), true,
                                  false);
            TestEqual(TEXT("Normal frontend end retains actual in-flight work"), Owner->GetActiveExecutionCount(), 1);
        }
    }
    return true;
}
#endif
