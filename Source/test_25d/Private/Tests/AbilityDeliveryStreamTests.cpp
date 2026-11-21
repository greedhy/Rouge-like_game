// 测试调用链：同一个真实弹体 -> 多次物理接触/命中流 -> 单次终态；不直接调用命中或伤害服务。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "Actor/MyProjectile.h"
#include "Components/BoxComponent.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

namespace AbilityDeliveryStreamTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        UPureMassSubsystem* Mass = nullptr;
        UPureMassMonsterType* Type = nullptr;
        FFixture()
        {
            Target->Destroy();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = 100.f;
            Type->XPReward = 0;
        }
        FAbilityDeliveryRequest Request() const
        {
            FAbilityDeliveryRequest R;
            R.Source = SourceToken;
            R.Origin = FVector(60, 0, 0);
            R.Direction = FVector::ForwardVector;
            R.RootCastId = FGuid::NewGuid();
            R.StepId = FGuid::NewGuid();
            R.Settings.ProjectileClass = AMyProjectile::StaticClass();
            R.Settings.Speed = 300.f;
            R.Settings.MaxDistance = 600.f;
            R.Settings.TimeoutSeconds = 8.f;
            R.Settings.CollisionRadius = 8.f;
            R.DamageParams.BaseDamage = 25.f;
            R.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
            return R;
        }
        void Fly(UAbilityDeliveryInstance& Delivery, float Seconds) const
        {
            for (float T = 0; T + UE_SMALL_NUMBER < Seconds && !Delivery.IsFinished(); T += .01f)
            {
                if (auto* Shot = Delivery.GetProjectile())
                {
                    Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
                Delivery.Advance(.01f);
            }
        }
        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }
    };
} // namespace AbilityDeliveryStreamTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryPiercingStreamTest, "test_25d.DynamicMage.DeliveryStream.Piercing",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryPiercingStreamTest::RunTest(const FString&)
{
    AbilityDeliveryStreamTests::FFixture F;
    const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
    const int32 B = F.Mass->SpawnMonster(F.Type, FVector(350, 0, 0));
    const int32 C = F.Mass->SpawnMonster(F.Type, FVector(500, 0, 0));
    auto Request = F.Request();
    Request.Settings.ImpactPolicy = EAbilityProjectileImpactPolicy::Pierce;
    Request.Settings.ImpactLimit = 3;
    Request.Settings.DamageFalloff = .5f;
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    TArray<FAbilityDeliveryImpact> Impacts;
    int32 Ends = 0;
    Delivery->OnImpact.AddLambda(
        [&](const FAbilityDeliveryImpact& Hit)
        {
            Impacts.Add(Hit);
        });
    Delivery->OnFinished.AddLambda(
        [&](const FAbilityDeliveryResult&)
        {
            ++Ends;
        });
    FString Error;
    if (!TestTrue(TEXT("Piercing delivery starts"), Delivery->Begin(Request, Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto* OriginalShot = Delivery->GetProjectile();
    F.Fly(*Delivery, .7f);
    TestFalse(TEXT("First impact does not end the piercing delivery"), Delivery->IsFinished());
    TestEqual(TEXT("The same projectile continues after first impact"), Delivery->GetProjectile(), OriginalShot);
    F.Fly(*Delivery, 2.f);
    TestEqual(TEXT("One impact event per actual contact"), Impacts.Num(), 3);
    TestEqual(TEXT("Three impacts have one terminal receipt"), Ends, 1);
    TestEqual(TEXT("First payload full damage"), F.Health(A), 75.f);
    TestEqual(TEXT("Second payload exponential falloff"), F.Health(B), 87.5f);
    TestEqual(TEXT("Third payload exponential falloff"), F.Health(C), 93.75f);
    if (Impacts.Num() == 3)
    {
        TestTrue(TEXT("All impacts belong to one physical delivery"),
                 Impacts[0].DeliveryId == Impacts[1].DeliveryId && Impacts[1].DeliveryId == Impacts[2].DeliveryId);
        TestTrue(TEXT("Impact sequence strictly increases"), Impacts[0].ImpactSequence < Impacts[1].ImpactSequence &&
                                                                 Impacts[1].ImpactSequence < Impacts[2].ImpactSequence);
    }
    Delivery->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryReturningStreamTest, "test_25d.DynamicMage.DeliveryStream.Returning",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryReturningStreamTest::RunTest(const FString&)
{
    AbilityDeliveryStreamTests::FFixture F;
    const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
    const int32 B = F.Mass->SpawnMonster(F.Type, FVector(350, 0, 0));
    auto Request = F.Request();
    Request.Settings.Motion = EAbilityProjectileMotion::Returning;
    Request.Settings.ImpactPolicy = EAbilityProjectileImpactPolicy::ReturnPerPhase;
    Request.Settings.ImpactLimit = 2;
    Request.Settings.DamageFalloff = .5f;
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    TArray<FAbilityDeliveryImpact> Impacts;
    int32 Ends = 0;
    Delivery->OnImpact.AddLambda(
        [&](const FAbilityDeliveryImpact& Hit)
        {
            Impacts.Add(Hit);
        });
    Delivery->OnFinished.AddLambda(
        [&](const FAbilityDeliveryResult&)
        {
            ++Ends;
        });
    FString Error;
    if (!TestTrue(TEXT("Returning delivery starts"), Delivery->Begin(Request, Error)))
    {
        AddInfo(Error);
        return false;
    }
    F.Fly(*Delivery, 6.f);
    TestEqual(TEXT("Each of two targets is hit once in each direction"), Impacts.Num(), 4);
    TestEqual(TEXT("Return phase does not create a second terminal receipt"), Ends, 1);
    TestTrue(TEXT("The delivery ends by returning to source"),
             Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Returned);
    TestEqual(TEXT("A receives outbound 25 and return 12.5"), F.Health(A), 62.5f);
    TestEqual(TEXT("B receives outbound 25 and return 12.5"), F.Health(B), 62.5f);
    if (Impacts.Num() == 4)
    {
        TestTrue(TEXT("Phase history resets only at actual turnaround"),
                 Impacts[0].Phase == EAbilityProjectilePhase::Outbound &&
                     Impacts[1].Phase == EAbilityProjectilePhase::Outbound &&
                     Impacts[2].Phase == EAbilityProjectilePhase::Returning &&
                     Impacts[3].Phase == EAbilityProjectilePhase::Returning);
    }
    Delivery->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryStreamWallAndCancellationTest,
                                 "test_25d.DynamicMage.DeliveryStream.WallAndReentrantCancellation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryStreamWallAndCancellationTest::RunTest(const FString&)
{
    AbilityDeliveryStreamTests::FFixture F;
    const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
    const int32 B = F.Mass->SpawnMonster(F.Type, FVector(500, 0, 0));
    auto* Wall = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5, 100, 100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(FVector(350, 0, 0));
    auto Request = F.Request();
    Request.Settings.ImpactPolicy = EAbilityProjectileImpactPolicy::Pierce;
    Request.Settings.ImpactLimit = 4;
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
    FString Error;
    if (!TestTrue(TEXT("Wall-limited stream starts"), Delivery->Begin(Request, Error)))
    {
        return false;
    }
    F.Fly(*Delivery, 2.f);
    TestTrue(TEXT("Wall after a valid impact terminates the stream as blocked"),
             Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Blocked);
    TestEqual(TEXT("Wall preserves the earlier real hit"), F.Health(A), 75.f);
    TestEqual(TEXT("Wall prevents a later target hit"), F.Health(B), 100.f);
    Delivery->Cancel();
    Wall->Destroy();
    auto* Cancelled = NewObject<UAbilityDeliveryInstance>(F.World);
    int32 Hits = 0;
    int32 Ends = 0;
    Cancelled->OnImpact.AddLambda(
        [&](const FAbilityDeliveryImpact&)
        {
            ++Hits;
            Cancelled->Cancel();
        });
    Cancelled->OnFinished.AddLambda(
        [&](const FAbilityDeliveryResult&)
        {
            ++Ends;
        });
    TestTrue(TEXT("Reentrant cancellation stream starts"), Cancelled->Begin(Request, Error));
    F.Fly(*Cancelled, 3.f);
    TestEqual(TEXT("Cancellation callback observes one actual impact"), Hits, 1);
    TestEqual(TEXT("Reentrant cancellation terminates once"), Ends, 1);
    TestTrue(TEXT("Reentrant cancellation remains cancellation"),
             Cancelled->GetResult().Outcome == EAbilityDeliveryOutcome::Cancelled);
    TestEqual(TEXT("No later payload survives callback cancellation"), F.Health(B), 100.f);
    Cancelled->Cancel();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryTwoHomingModelsTest, "test_25d.DynamicMage.DeliveryStream.TwoHomingModels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryTwoHomingModelsTest::RunTest(const FString&)
{
    AbilityDeliveryStreamTests::FFixture F;
    const int32 Id = F.Mass->SpawnMonster(F.Type, FVector(300, 100, 0));
    FAbilityTargetToken Target;
    FString Error;
    TestTrue(TEXT("Original off-axis target life is captured"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), Target, Error));
    for (const auto Model : {EAbilityProjectileMotion::NativeHoming, EAbilityProjectileMotion::LegacyTurnHoming})
    {
        auto Request = F.Request();
        Request.Target = Target;
        Request.Settings.Motion = Model;
        Request.Settings.HomingTurnRate = 360.f;
        auto* Delivery = NewObject<UAbilityDeliveryInstance>(F.World);
        if (!TestTrue(TEXT("Explicit homing model starts"), Delivery->Begin(Request, Error)))
        {
            return false;
        }
        F.Fly(*Delivery, 4.f);
        TestTrue(TEXT("Each explicit homing model reaches the real off-axis target"),
                 Delivery->IsFinished() && Delivery->GetResult().Outcome == EAbilityDeliveryOutcome::Hit);
        Delivery->Cancel();
    }
    TestEqual(TEXT("Both real homing flights apply exactly one payload"), F.Health(Id), 50.f);
    return true;
}
#endif
