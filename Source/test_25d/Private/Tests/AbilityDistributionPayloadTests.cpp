// 测试调用链：运行时散射/投递图 -> 真实弹体接触 -> ResolveImpact -> InputDamage -> Mass真实血量/死亡力。
// 固定Damage配置模式保留独立对照；接触准备只由实际伤害所有者执行一次。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "Actor/MyProjectile.h"
#include "EngineUtils.h"
#include "MassEntitySubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace AbilityDistributionPayloadTests
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
        FAbilityNodeInput Input() const
        {
            FAbilityNodeInput I;
            I.SourceToken = SourceToken;
            I.Origin = FVector(60, 0, 0);
            I.Direction = FVector::ForwardVector;
            I.DamageParams.BaseDamage = 25.f;
            I.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
            I.DamageParams.DeathImpulseMagnitude = 900.f;
            I.DamageScale = 2.f;
            I.DebuffDamageScale = .25f;
            return I;
        }
        FAbilityDeliverySettings Flight(bool bExternal) const
        {
            FAbilityDeliverySettings D;
            D.ProjectileClass = AMyProjectile::StaticClass();
            D.Speed = 300.f;
            D.MaxDistance = 800.f;
            D.TimeoutSeconds = 10.f;
            D.CollisionRadius = 8.f;
            D.bApplyDamage = !bExternal;
            return D;
        }
        FAbilityGraphNode Node(UAbilityGraphDefinition* G, FName TypeId, UAbilityNodeConfig* Config) const
        {
            FAbilityGraphNode N;
            N.NodeId = FGuid::NewGuid();
            N.TypeId = TypeId;
            N.Config = Config;
            G->Nodes.Add(N);
            return N;
        }
        UAbilityGraphDefinition* Graph(bool bExternal, const FAbilityDeliverySettings& Flight,
                                       bool bPropagation = false) const
        {
            auto* G = NewObject<UAbilityGraphDefinition>(World);
            G->SchemaVersion = 2;
            G->CompatibilityProfile = TEXT("UnifiedV2");
            FAbilityGraphNode Root;
            if (bPropagation)
            {
                auto* C = NewObject<UAbilityPropagationNodeConfig>(G);
                C->Delivery = Flight;
                C->MaxTargets = 3;
                C->JumpDelay = .05f;
                C->JumpRadius = 250.f;
                C->DamageFalloff = .5f;
                Root = Node(G, TEXT("Propagation.Sequential"), C);
            }
            else
            {
                auto* C = NewObject<UAbilityDeliveryNodeConfig>(G);
                C->Delivery = Flight;
                Root = Node(G, TEXT("Delivery.ProjectileAwait"), C);
            }
            G->EntryNodeId = Root.NodeId;
            if (bExternal)
            {
                auto Bridge = Node(G, TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
                auto Damage = Node(G, TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
                G->Edges = {{Root.NodeId, Bridge.NodeId, TEXT("Hit")},
                            {Bridge.NodeId, Damage.NodeId, TEXT("TargetFound")}};
            }
            return G;
        }
        FAbilityNodeRegistry Registry() const
        {
            FAbilityNodeRegistry R;
            R.RegisterBuiltins();
            AbilityDistributionNodes::Register(R);
            AbilityInputDamageNodes::Register(R);
            return R;
        }
        /** 只推进真实PM与图；不手动调用命中或伤害函数。 */
        void Fly(UAbilityGraphInstance& Run, float Seconds) const
        {
            for (float T = 0.f; T + UE_SMALL_NUMBER < Seconds && !Run.IsFinished(); T += .01f)
            {
                TArray<TWeakObjectPtr<AMyProjectile>> Shots;
                for (TActorIterator<AMyProjectile> It(World); It; ++It)
                {
                    Shots.Add(*It);
                }
                for (auto Shot : Shots)
                {
                    if (Shot.IsValid() && !Shot->IsActorBeingDestroyed())
                    {
                        Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                    }
                }
                Run.Advance(.01f);
            }
        }
        float Health(int32 Id) const
        {
            FPureMassView View;
            return Mass->Resolve(Id, View) ? View.Health : 0.f;
        }
        float StatusDPS(int32 Id) const
        {
            auto& M = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
            return M.GetFragmentDataChecked<FPureMassFragment>(Mass->GetEntity(Id)).StatusDPS;
        }
    };
} // namespace AbilityDistributionPayloadTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScatterDirectionContractTest, "test_25d.DynamicMage.Distribution.AngleAndValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FScatterDirectionContractTest::RunTest(const FString&)
{
    auto* C = NewObject<UAbilityScatterNodeConfig>();
    auto Directions = AbilityDistributionNodes::BuildDirections(FVector::ForwardVector, 4.f, *C);
    TestEqual(TEXT("Three configured scatter directions"), Directions.Num(), 3);
    if (Directions.Num() == 3)
    {
        TestTrue(TEXT("Original negative-to-positive angle order is preserved"),
                 Directions[0].Equals(FVector::ForwardVector.RotateAngleAxis(-45.f, FVector::UpVector)) &&
                     Directions[1].Equals(FVector::ForwardVector) &&
                     Directions[2].Equals(FVector::ForwardVector.RotateAngleAxis(45.f, FVector::UpVector)));
    }
    C->bLimitCountToLevel = true;
    C->bOverridePitch = true;
    C->PitchOverride = 35.f;
    Directions = AbilityDistributionNodes::BuildDirections(FVector::ForwardVector, 2.f, *C);
    TestEqual(TEXT("Level count restriction happens before angle distribution"), Directions.Num(), 2);
    for (FVector D : Directions)
    {
        TestTrue(TEXT("Pitch override survives the angular fan"),
                 FMath::IsNearlyEqual(D.Z, FMath::Sin(FMath::DegreesToRadians(35.f)), 1.e-6));
    }
    FString Error;
    C->Count = 0;
    TestFalse(TEXT("Zero count is rejected"), C->Validate(Error));
    C->Count = 65;
    TestFalse(TEXT("Unbounded count is rejected"), C->Validate(Error));
    C->Count = 3;
    C->Spread = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Nonfinite spread is rejected"), C->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScatterActualHomingTest, "test_25d.DynamicMage.Distribution.RealScatterHoming",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FScatterActualHomingTest::RunTest(const FString&)
{
    AbilityDistributionPayloadTests::FFixture F;
    F.Type->Health = 1000.f;
    const int32 Id = F.Mass->SpawnMonster(F.Type, FVector(400, 100, 0));
    auto D = F.Flight(false);
    D.Motion = EAbilityProjectileMotion::NativeHoming;
    D.HomingAccelerationMin = D.HomingAccelerationMax = 2000.f;
    auto* G = F.Graph(false, D);
    const FGuid DeliveryNode = G->EntryNodeId;
    auto Scatter = F.Node(G, TEXT("Distribution.Scatter"), NewObject<UAbilityScatterNodeConfig>(G));
    G->EntryNodeId = Scatter.NodeId;
    G->Edges.Add({Scatter.NodeId, DeliveryNode, TEXT("Launch")});
    auto I = F.Input();
    FString Error;
    FAbilityTargetToken Target;
    AbilityExecutionIdentity::CaptureTarget(I.SourceToken, FPureMassCombatTarget(Id), Target, Error);
    I.LiveTargetToken = Target;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    int32 Launches = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& E)
        {
            Launches += E.OutputPort == TEXT("Launch") ? 1 : 0;
        });
    TestTrue(TEXT("Runtime scatter+homing graph starts"), Run->StartWithInput(G, I, {}, F.Registry(), Error));
    AddInfo(Error);
    int32 Shots = 0;
    for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
    {
        Shots += !It->IsActorBeingDestroyed() ? 1 : 0;
    }
    TestEqual(TEXT("Distribution produces three actual physical projectiles through Delivery"), Shots, 3);
    TestEqual(TEXT("Distribution publishes one Launch per direction"), Launches, 3);
    F.Fly(*Run, 5.f);
    TestEqual(TEXT("Three real homing flights each apply one scaled payload"), F.Health(Id), 850.f);
    TestTrue(TEXT("All scatter deliveries release their invocation resources"), Run->IsFinished());
    Run->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreparedContactDamageTest, "test_25d.DynamicMage.InputPayload.ContactPreparation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPreparedContactDamageTest::RunTest(const FString&)
{
    FDamageEffectParams P;
    P.BaseDamage = 25.f;
    P.DebuffDamage = 8.f;
    P.DeathImpulseMagnitude = 900.f;
    P.KnockbackForceMagnitude = 500.f;
    P.KnockbackChance = 80.f;
    FMath::RandInit(91827);
    const bool bKnock = FMath::RandRange(0, 100) < P.KnockbackChance;
    const int32 ExpectedNext = FMath::Rand();
    FMath::RandInit(91827);
    FDamageEffectParams Prepared;
    TestTrue(TEXT("Contact preparation accepts finite original payload"),
             AbilityDamageService::PrepareContactDamage(P, FVector::RightVector, 2.f, .25f, Prepared));
    TestEqual(TEXT("Main cumulative scale is consumed once in a local copy"), Prepared.BaseDamage, 50.f);
    TestEqual(TEXT("Debuff cumulative scale is independent"), Prepared.DebuffDamage, 2.f);
    TestEqual(TEXT("Original base params remain unmodified"), P.BaseDamage, 25.f);
    TestTrue(TEXT("Death impulse is prepared from contact direction"),
             Prepared.DeathImpulse.Equals(FVector(0, 900, 0)));
    auto Rotation = FVector::RightVector.Rotation();
    Rotation.Pitch = 45.f;
    TestTrue(TEXT("Knockback uses the existing one-roll contract"),
             Prepared.KnockbackForce.Equals(bKnock ? Rotation.Vector() * 500.f : FVector::ZeroVector));
    TestEqual(TEXT("Contact preparation consumes exactly one knockback RNG draw"), FMath::Rand(), ExpectedNext);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureDeliveryNoForceRandomTest, "test_25d.DynamicMage.InputPayload.PureContactNoRNG",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPureDeliveryNoForceRandomTest::RunTest(const FString&)
{
    AbilityDistributionPayloadTests::FFixture F;
    const int32 Id = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
    auto D = F.Flight(true);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    auto I = F.Input();
    I.DamageParams.KnockbackChance = 80.f;
    I.DamageParams.KnockbackForceMagnitude = 500.f;
    FString Error;
    TestTrue(TEXT("Pure delivery without a damage owner starts"),
             Run->StartWithInput(F.Graph(false, D), I, {}, F.Registry(), Error));
    FMath::RandInit(6421);
    const int32 ExpectedNext = FMath::Rand();
    FMath::RandInit(6421);
    F.Fly(*Run, 2.f);
    TestEqual(TEXT("Pure contact never rolls knockback before a payload owner exists"), FMath::Rand(), ExpectedNext);
    TestEqual(TEXT("Pure delivery never applies implicit damage"), F.Health(Id), 100.f);
    Run->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryScalarValidationTest, "test_25d.DynamicMage.InputPayload.ScalarValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeliveryScalarValidationTest::RunTest(const FString&)
{
    AbilityDistributionPayloadTests::FFixture F;
    FAbilityDeliveryRequest R;
    R.Source = F.SourceToken;
    R.Origin = FVector(60, 0, 0);
    R.RootCastId = FGuid::NewGuid();
    R.StepId = FGuid::NewGuid();
    R.Settings = F.Flight(true);
    R.DamageScale = std::numeric_limits<float>::quiet_NaN();
    auto* InvalidMain = NewObject<UAbilityDeliveryInstance>(F.World);
    FString Error;
    TestFalse(TEXT("Nonfinite cumulative main scale is rejected before creating an actor"),
              InvalidMain->Begin(R, Error));
    InvalidMain->Cancel();
    R.DamageScale = 1.f;
    R.DebuffDamageScale = -1.f;
    auto* InvalidDebuff = NewObject<UAbilityDeliveryInstance>(F.World);
    TestFalse(TEXT("Negative cumulative debuff scale is rejected before creating an actor"),
              InvalidDebuff->Begin(R, Error));
    InvalidDebuff->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputPiercingPayloadTest, "test_25d.DynamicMage.InputPayload.PiercingParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInputPiercingPayloadTest::RunTest(const FString&)
{
    for (const bool bExternal : {false, true})
    {
        AbilityDistributionPayloadTests::FFixture F;
        const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
        const int32 B = F.Mass->SpawnMonster(F.Type, FVector(400, 0, 0));
        const int32 C = F.Mass->SpawnMonster(F.Type, FVector(600, 0, 0));
        auto D = F.Flight(bExternal);
        D.ImpactPolicy = EAbilityProjectileImpactPolicy::Pierce;
        D.ImpactLimit = 3;
        D.DamageFalloff = .5f;
        auto* G = F.Graph(bExternal, D);
        auto I = F.Input();
        I.DamageParams.DebuffChance = 100.f;
        I.DamageParams.DebuffDuration = 2.f;
        I.DamageParams.DebuffFrequency = .5f;
        I.DamageParams.DebuffDamage = 8.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TArray<FAbilityNodeInput> Hits;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& E)
            {
                if (E.OutputPort == TEXT("Hit"))
                {
                    Hits.Add(E.Input);
                }
            });
        FString Error;
        TestTrue(TEXT("Piercing payload graph starts"), Run->StartWithInput(G, I, {}, F.Registry(), Error));
        FMath::RandInit(12345);
        F.Fly(*Run, 4.f);
        TestEqual(TEXT("First physical payload has cumulative scale 2"), F.Health(A), 50.f);
        TestEqual(TEXT("Second physical payload adds one piercing falloff"), F.Health(B), 75.f);
        TestEqual(TEXT("Third physical payload adds exactly two falloffs"), F.Health(C), 87.5f);
        TestEqual(TEXT("Piercing emits three original-life impacts"), Hits.Num(), 3);
        for (int32 N = 0; N < Hits.Num(); ++N)
        {
            TestEqual(TEXT("Hit keeps unscaled original base params"), Hits[N].DamageParams.BaseDamage, 25.f);
            TestEqual(TEXT("Hit carries complete cumulative main scale"), Hits[N].DamageScale,
                      2.f * FMath::Pow(.5f, N));
            TestEqual(TEXT("Piercing does not scale independent debuff payload"), Hits[N].DebuffDamageScale, .25f);
        }
        TestEqual(TEXT("Independent debuff scale reaches first real Mass status"), F.StatusDPS(A), 4.f);
        TestEqual(TEXT("Piercing main falloff does not change second debuff DPS"), F.StatusDPS(B), 4.f);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputReturningPayloadTest, "test_25d.DynamicMage.InputPayload.ReturningParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInputReturningPayloadTest::RunTest(const FString&)
{
    for (const bool bExternal : {false, true})
    {
        AbilityDistributionPayloadTests::FFixture F;
        const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
        const int32 B = F.Mass->SpawnMonster(F.Type, FVector(400, 0, 0));
        auto D = F.Flight(bExternal);
        D.Motion = EAbilityProjectileMotion::Returning;
        D.MaxDistance = 600.f;
        D.ImpactPolicy = EAbilityProjectileImpactPolicy::ReturnPerPhase;
        D.ImpactLimit = 2;
        D.DamageFalloff = .5f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Returning payload graph starts"),
                 Run->StartWithInput(F.Graph(bExternal, D), F.Input(), {}, F.Registry(), Error));
        F.Fly(*Run, 6.f);
        TestEqual(TEXT("A receives scaled outbound 50 and return 25 once each"), F.Health(A), 25.f);
        TestEqual(TEXT("B receives the same per-phase scale"), F.Health(B), 25.f);
        TestTrue(TEXT("Returning external/internal graph finishes"), Run->IsFinished());
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputPropagationPayloadTest, "test_25d.DynamicMage.InputPayload.PropagationParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInputPropagationPayloadTest::RunTest(const FString&)
{
    for (const bool bExternal : {false, true})
    {
        AbilityDistributionPayloadTests::FFixture F;
        const int32 A = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
        const int32 B = F.Mass->SpawnMonster(F.Type, FVector(400, 0, 0));
        const int32 C = F.Mass->SpawnMonster(F.Type, FVector(600, 0, 0));
        auto I = F.Input();
        I.DamageParams.DebuffChance = 100.f;
        I.DamageParams.DebuffDuration = 2.f;
        I.DamageParams.DebuffFrequency = .5f;
        I.DamageParams.DebuffDamage = 8.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Sequential external/internal payload starts"),
                 Run->StartWithInput(F.Graph(bExternal, F.Flight(bExternal), true), I, {}, F.Registry(), Error));
        FMath::RandInit(12345);
        F.Fly(*Run, 5.f);
        TestEqual(TEXT("First jump consumes initial cumulative scale"), F.Health(A), 50.f);
        TestEqual(TEXT("Second jump consumes one hop falloff"), F.Health(B), 75.f);
        TestEqual(TEXT("Third jump consumes two hop falloffs"), F.Health(C), 87.5f);
        TestEqual(TEXT("First jump debuff scale"), F.StatusDPS(A), 4.f);
        TestEqual(TEXT("Hop falloff applies independently to debuff scale"), F.StatusDPS(B), 2.f);
        TestEqual(TEXT("Third jump debuff scale"), F.StatusDPS(C), 1.f);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputActorPayloadTest, "test_25d.DynamicMage.InputPayload.RealActorParity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInputActorPayloadTest::RunTest(const FString&)
{
    for (const bool bExternal : {false, true})
    {
        FAbilityGraphTestFixture F;
        F.Target->SetActorLocation(FVector(300, 0, 0));
        auto* TargetASC = F.Target->GetAbilitySystemComponent();
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        TargetASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        auto* G = NewObject<UAbilityGraphDefinition>(F.World);
        G->SchemaVersion = 2;
        G->CompatibilityProfile = TEXT("UnifiedV2");
        auto Add = [&](FName Type, UAbilityNodeConfig* Config)
        {
            FAbilityGraphNode N;
            N.NodeId = FGuid::NewGuid();
            N.TypeId = Type;
            N.Config = Config;
            G->Nodes.Add(N);
            return N.NodeId;
        };
        auto* D = NewObject<UAbilityDeliveryNodeConfig>(G);
        D->Delivery.ProjectileClass = AMyProjectile::StaticClass();
        D->Delivery.Speed = 300.f;
        D->Delivery.CollisionRadius = 8.f;
        D->Delivery.bApplyDamage = !bExternal;
        const FGuid Projectile = Add(TEXT("Delivery.ProjectileAwait"), D);
        G->EntryNodeId = Projectile;
        if (bExternal)
        {
            const FGuid Bridge = Add(TEXT("Target.ResolveImpact"), NewObject<UAbilityResolveImpactNodeConfig>(G));
            const FGuid Damage = Add(TEXT("Effect.InputDamage"), NewObject<UAbilityInputDamageNodeConfig>(G));
            G->Edges = {{Projectile, Bridge, TEXT("Hit")}, {Bridge, Damage, TEXT("TargetFound")}};
        }
        FAbilityNodeInput I;
        I.SourceToken = F.SourceToken;
        I.LiveTargetToken = F.TargetToken;
        I.Origin = FVector(60, 0, 0);
        I.Direction = FVector::ForwardVector;
        I.DamageParams.DamageGameplayEffectClass = UPureMassIncomingDamageEffect::StaticClass();
        I.DamageParams.DamageType = UPureMassIncomingDamageEffect::GetDamageTag();
        I.DamageParams.BaseDamage = 25.f;
        I.DamageScale = 2.f;
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FAbilityNodeRegistry Registry;
        Registry.RegisterBuiltins();
        AbilityInputDamageNodes::Register(Registry);
        FString Error;
        TestTrue(TEXT("Actor internal/external payload graph starts"), Run->StartWithInput(G, I, {}, Registry, Error));
        for (int32 Frame = 0; Frame < 200 && !Run->IsFinished(); ++Frame)
        {
            TArray<TWeakObjectPtr<AMyProjectile>> Shots;
            for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
            {
                Shots.Add(*It);
            }
            for (auto Shot : Shots)
            {
                if (Shot.IsValid() && !Shot->IsActorBeingDestroyed())
                {
                    Shot->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
            }
            Run->Advance(.01f);
        }
        TestEqual(TEXT("Real Actor GE receives one complete cumulative damage payload"),
                  TargetASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()), 50.f);
        Run->Cancel();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropagationRejectsStreamTest,
                                 "test_25d.DynamicMage.InputPayload.PropagationRejectsStream",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropagationRejectsStreamTest::RunTest(const FString&)
{
    auto* C = NewObject<UAbilityPropagationNodeConfig>();
    C->Delivery.ProjectileClass = AMyProjectile::StaticClass();
    C->Delivery.ImpactPolicy = EAbilityProjectileImpactPolicy::Pierce;
    C->Delivery.ImpactLimit = 3;
    FString Error;
    TestFalse(TEXT("Single-hit sequential propagation rejects unsupported multi-impact delivery"), C->Validate(Error));
    C->Delivery.Motion = EAbilityProjectileMotion::Returning;
    C->Delivery.ImpactPolicy = EAbilityProjectileImpactPolicy::ReturnPerPhase;
    TestFalse(TEXT("Sequential propagation cannot claim complete visited semantics for returning streams"),
              C->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOriginalFixedDamageConfigTest,
                                 "test_25d.DynamicMage.InputPayload.FixedConfigUnchanged",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOriginalFixedDamageConfigTest::RunTest(const FString&)
{
    AbilityDistributionPayloadTests::FFixture F;
    const int32 Id = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
    auto* G = NewObject<UAbilityGraphDefinition>(F.World);
    G->SchemaVersion = 2;
    G->CompatibilityProfile = TEXT("UnifiedV2");
    auto* C = NewObject<UAbilityDamageNodeConfig>(G);
    C->Damage = 33.f;
    C->DamagePerLevel = 0.f;
    C->DamageType = FMyGameplayTags::Get().Damage_Fire;
    const auto Root = F.Node(G, TEXT("Effect.Damage"), C);
    G->EntryNodeId = Root.NodeId;
    auto I = F.Input();
    I.DamageScale = 9.f;
    FString Error;
    FAbilityTargetToken Target;
    AbilityExecutionIdentity::CaptureTarget(I.SourceToken, FPureMassCombatTarget(Id), Target, Error);
    I.LiveTargetToken = Target;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Original fixed Damage node starts"), Run->StartWithInput(G, I, {}, F.Registry(), Error));
    TestEqual(TEXT("Original config mode retains its exact config value"), F.Health(Id), 67.f);
    Run->Cancel();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputPayloadLethalForceTest, "test_25d.DynamicMage.InputPayload.LethalForce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInputPayloadLethalForceTest::RunTest(const FString&)
{
    for (const bool bExternal : {false, true})
    {
        AbilityDistributionPayloadTests::FFixture F;
        F.Type->Health = 40.f;
        const int32 Id = F.Mass->SpawnMonster(F.Type, FVector(200, 0, 0));
        auto* Crowd = F.World->SpawnActor<APureMassCrowd>();
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        FString Error;
        TestTrue(TEXT("Lethal projectile payload starts"),
                 Run->StartWithInput(F.Graph(bExternal, F.Flight(bExternal)), F.Input(), {}, F.Registry(), Error));
        F.Fly(*Run, 2.f);
        FPureMassView View;
        TestFalse(TEXT("Scaled single physical hit kills the 40HP original entity"), F.Mass->Resolve(Id, View));
        auto* Pending = FindFProperty<FArrayProperty>(APureMassCrowd::StaticClass(), TEXT("PendingDeaths"));
        if (TestNotNull(TEXT("Death presentation queue is observable through reflected state"), Pending))
        {
            FScriptArrayHelper Queue(Pending, Pending->ContainerPtrToValuePtr<void>(Crowd));
            TestEqual(TEXT("One physical payload queues death presentation once"), Queue.Num(), 1);
            if (Queue.Num() == 1)
            {
                const auto* Death = reinterpret_cast<const FPureMassDeathPresentation*>(Queue.GetRawPtr(0));
                TestTrue(TEXT("Actual ragdoll queue receives the contact death impulse once"),
                         FVector(Death->DeathImpulse).Equals(FVector(900, 0, 0)));
            }
        }
        Run->Cancel();
    }
    return true;
}
#endif
