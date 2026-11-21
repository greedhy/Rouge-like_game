// 调用链：临时权威World -> DeliveryInstance -> 原ProjectileMovement -> 实际Actor/Mass命中 -> 回执/真实扣血。
// 测试不手工调用Contact或伤害入口；世界销毁清理测试生成的全部Actor/实体。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "Components/BoxComponent.h"
#include <limits>

namespace AbilityDeliveryTests
{
    /** 同一段真实组件移动后推进投递所有者，禁止用人工Hit回调代替碰撞。 */
    void Fly(UAbilityDeliveryInstance& Delivery, float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds && !Delivery.IsFinished(); Elapsed += .01f)
        {
            if (auto* Projectile = Delivery.GetProjectile())
            {
                Projectile->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
            }
            Delivery.Advance(.01f);
        }
    }

    FAbilityDeliveryRequest Request(FAbilityGraphTestFixture& F)
    {
        FAbilityDeliveryRequest R;
        R.Source = F.SourceToken;
        R.Origin = FVector(60, 0, 0);
        R.Direction = FVector::ForwardVector;
        R.Settings.ProjectileClass = AMyProjectile::StaticClass();
        R.Settings.Speed = 300.f;
        R.Settings.MaxDistance = 1000.f;
        R.Settings.TimeoutSeconds = 5.f;
        R.Settings.CollisionRadius = 8.f;
        R.RootCastId = FGuid::NewGuid();
        R.StepId = FGuid::NewGuid();
        R.DamageParams.BaseDamage = 25.f;
        R.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
        return R;
    }
} // namespace AbilityDeliveryTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryConfigurationTest, "test_25d.DynamicMage.Delivery.Configuration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryConfigurationTest::RunTest(const FString&)
{
    FAbilityDeliverySettings Settings;
    FString Error;
    TestFalse(TEXT("Null projectile class is rejected"), Settings.Validate(Error));
    Settings.ProjectileClass = AMyProjectile::StaticClass();
    TestTrue(TEXT("Finite flight settings are accepted"), Settings.Validate(Error));
    Settings.ProjectileClass = AMyOrbitingFireBolt::StaticClass();
    TestFalse(TEXT("Unadapted native lifecycle is rejected before spawning"), Settings.Validate(Error));
    Settings.ProjectileClass = AMyProjectile::StaticClass();
    Settings.Speed = -1.f;
    TestFalse(TEXT("Nonpositive speed is rejected"), Settings.Validate(Error));
    Settings.Speed = 100.f;
    Settings.TimeoutSeconds = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Nonfinite timeout is rejected"), Settings.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryActualActorContactTest, "test_25d.DynamicMage.Delivery.RealActorContact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryActualActorContactTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(300, 0, 0));
    F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
    F.Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
    auto Request = AbilityDeliveryTests::Request(F);
    Request.Settings.bApplyDamage = false;
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    int32 Receipts = 0;
    Delivery->OnFinished.AddLambda(
        [&](const FAbilityDeliveryResult&)
        {
            ++Receipts;
        });
    FString Error;
    if (!TestTrue(TEXT("Actual actor delivery starts"), Delivery->Begin(Request, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestFalse(TEXT("Spawning is not a hit"), Delivery->IsFinished());
    auto* Shot = Delivery->GetProjectile();
    if (TestNotNull(TEXT("Real projectile was created"), Shot))
    {
        AddInfo(FString::Printf(TEXT("Before flight: position=%s velocity=%s updated=%s active=%d"),
                                *Shot->GetActorLocation().ToString(), *Shot->ProjectileMovement->Velocity.ToString(),
                                *GetNameSafe(Shot->ProjectileMovement->UpdatedComponent.Get()),
                                Shot->ProjectileMovement->IsActive()));
        TestEqual(TEXT("Delivery establishes actual moved component"), Shot->ProjectileMovement->UpdatedComponent.Get(),
                  static_cast<USceneComponent*>(Shot->Sphere));
    }
    AbilityDeliveryTests::Fly(*Delivery, .2f);
    if (IsValid(Shot))
    {
        AddInfo(FString::Printf(TEXT("After 0.2 flight: position=%s velocity=%s updated=%s"),
                                *Shot->GetActorLocation().ToString(), *Shot->ProjectileMovement->Velocity.ToString(),
                                *GetNameSafe(Shot->ProjectileMovement->UpdatedComponent.Get())));
        TestTrue(TEXT("Original movement component really advances the actor"), Shot->GetActorLocation().X > 100.f);
    }
    TestFalse(TEXT("Flight before actual contact is pending"), Delivery->IsFinished());
    AbilityDeliveryTests::Fly(*Delivery, 1.f);
    TestTrue(TEXT("Actual actor contact completes delivery"), Delivery->IsFinished());
    TestTrue(TEXT("Receipt is a confirmed hit"), Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Hit);
    TestTrue(TEXT("Receipt preserves original actor identity"),
             Delivery->GetResult().Impact.IsSet() &&
                 Delivery->GetResult().Impact->ConfirmedTarget.Target.Actor.Get() == F.Target);
    TestEqual(TEXT("Payload-free delivery does not inflict internal damage"),
              F.Target->GetAbilitySystemComponent()->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 100.f);
    Delivery->Cancel();
    Delivery->Advance(10.f);
    TestEqual(TEXT("Terminal receipt is emitted once"), Receipts, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryParryFlightTest, "test_25d.DynamicMage.Delivery.RealContactParry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryParryFlightTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->SetActorLocation(FVector(300, 0, 0));
    FAbilitySourceToken Defender;
    FString Error;
    if (!TestTrue(TEXT("Defender source is captured"),
                  AbilityExecutionIdentity::CaptureSource(F.Target, Defender, Error)))
    {
        return false;
    }
    auto* Time = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Time->SampleClockForTesting(10., false);
    FGuardParrySettings Parry;
    Parry.bEnabled = true;
    Parry.bRewardWorldSlow = false;
    auto Guard = AbilityCharacterActions::OpenGuardWindow(Defender, -1.f, Parry);
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 100, 100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(340, 0, 0));
    auto Request = AbilityDeliveryTests::Request(F);
    Request.Settings.bApplyDamage = false;
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    if (!TestTrue(TEXT("Reflectable original projectile starts"), Delivery->Begin(Request, Error)))
    {
        AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
        return false;
    }
    auto* Shot = Delivery->GetProjectile();
    // 一个原始物理步同时越过防御者和其后的墙；弹反必须让后方旧方向重叠失效。
    Shot->ProjectileMovement->TickComponent(1.f, LEVELTICK_All, nullptr);
    Delivery->Advance(.01f);
    TestTrue(TEXT("Actual actor contact uses original parry decision"), Shot->HasBeenParryReflected());
    TestTrue(TEXT("Original delivery is cancelled by source transfer"),
             Delivery->IsFinished() && Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Cancelled);
    TestFalse(TEXT("Old delivery cannot destroy reflected return flight"), Shot->IsActorBeingDestroyed());
    TestFalse(TEXT("Reflected projectile no longer belongs to old graph handler"), Shot->IsGraphDeliveryManaged());
    TestEqual(TEXT("Reflected projectile belongs to defender ASC"),
              Shot->DamageEffectParams.SourceAbilitySystemComponent.Get(), Defender.ASC.Get());
    AbilityCharacterActions::CloseGuardWindow(Defender.ASC.Get(), Guard);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryMassAndCancellationTest, "test_25d.DynamicMage.Delivery.MassAndCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryMassAndCancellationTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(300, 0, 0));
    auto Request = AbilityDeliveryTests::Request(F);
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Actual Mass delivery starts"), Delivery->Begin(Request, Error)))
    {
        AddInfo(Error);
        return false;
    }
    AbilityDeliveryTests::Fly(*Delivery, .2f);
    FPureMassView View;
    Mass->Resolve(Id, View);
    TestEqual(TEXT("Mass health unchanged before contact"), View.Health, 100.f);
    AbilityDeliveryTests::Fly(*Delivery, 1.f);
    TestTrue(TEXT("Mass contact receipt completes"), Delivery->IsFinished());
    TestTrue(TEXT("Mass still resolves"), Mass->Resolve(Id, View));
    TestEqual(TEXT("Mass contact applies one payload"), View.Health, 75.f);
    TestTrue(TEXT("Actual removed health is reported, not invented"),
             Delivery->GetResult().Damage.ActualDamage.IsSet());
    if (Delivery->GetResult().Damage.ActualDamage.IsSet())
    {
        TestEqual(TEXT("Receipt observes actual removed HP"), Delivery->GetResult().Damage.ActualDamage.GetValue(),
                  25.f);
    }
    auto* Cancelled = NewObject<UAbilityDeliveryInstance>(F.World);
    TestTrue(TEXT("Second delivery starts independently"), Cancelled->Begin(Request, Error));
    Cancelled->Cancel();
    AbilityDeliveryTests::Fly(*Cancelled, 2.f);
    TestTrue(TEXT("Cancellation is terminal"), Cancelled->GetResult().Outcome == EAbilityDeliveryOutcome::Cancelled);
    Mass->Resolve(Id, View);
    TestEqual(TEXT("Cancelled flight leaves no future damage"), View.Health, 75.f);
    return true;
}
#endif
