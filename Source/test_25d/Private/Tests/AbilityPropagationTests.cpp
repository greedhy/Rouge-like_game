// 调用链：运行时内存图 -> 逐跳策略 -> 真实火焰投递 -> 命中后等待 -> 下一步；不激活或预制另一个GA。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropagationWaitsForActualHitTest,
                                 "test_25d.DynamicMage.Propagation.RealHitBeforeNextJump",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropagationWaitsForActualHitTest::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Fragile = NewObject<UPureMassMonsterType>(F.World);
    Fragile->Health = 10.f;
    Fragile->XPReward = 0;
    auto* Healthy = NewObject<UPureMassMonsterType>(F.World);
    Healthy->Health = 100.f;
    Healthy->XPReward = 0;
    const int32 First = Mass->SpawnMonster(Fragile, FVector(300, 0, 0));
    const int32 Second = Mass->SpawnMonster(Healthy, FVector(600, 0, 0));
    const int32 Third = Mass->SpawnMonster(Healthy, FVector(900, 0, 0));
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = FVector(60, 0, 0);
    Input.Direction = FVector::ForwardVector;
    Input.EventCenter = FVector(300, 0, 0);
    Input.DamageParams.BaseDamage = 25.f;
    Input.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
    auto* Graph = NewObject<UAbilityGraphDefinition>(F.World);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Propagation.Sequential");
    auto* Config = NewObject<UAbilityPropagationNodeConfig>(Graph);
    Config->Delivery.ProjectileClass = AMyProjectile::StaticClass();
    Config->Delivery.Speed = 300.f;
    Config->Delivery.MaxDistance = 1000.f;
    Config->Delivery.TimeoutSeconds = 5.f;
    Config->Delivery.CollisionRadius = 8.f;
    Config->MaxTargets = 3;
    Config->JumpDelay = .15f;
    Config->JumpRadius = 350.f;
    Node.Config = Config;
    Graph->EntryNodeId = Node.NodeId;
    Graph->Nodes = {Node};
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    AbilityDeliveryNodes::Register(Registry);
    AbilityPropagationNodes::Register(Registry);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    FString Error;
    int32 Hits = 0;
    Run->OnEvent.AddLambda(
        [&](const FAbilityGraphEvent& Event)
        {
            if (Event.OutputPort == TEXT("DamageResolved"))
            {
                ++Hits;
            }
        });
    if (!TestTrue(TEXT("Runtime-created propagation graph starts"),
                  Run->StartWithInput(Graph, Input, {}, Registry, Error)))
    {
        AddInfo(Error);
        return false;
    }
    auto Tick = [&](float Seconds)
    {
        for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds && !Run->IsFinished(); Elapsed += .01f)
        {
            TArray<TWeakObjectPtr<AMyProjectile>> InFlight;
            for (TActorIterator<AMyProjectile> It(F.World); It; ++It)
            {
                InFlight.Add(*It);
            }
            for (auto Projectile : InFlight)
            {
                if (Projectile.IsValid() && !Projectile->IsActorBeingDestroyed())
                {
                    Projectile->ProjectileMovement->TickComponent(.01f, LEVELTICK_All, nullptr);
                }
            }
            Run->Advance(.01f);
        }
    };
    Tick(.2f);
    TestEqual(TEXT("No instant chain hit before first fire arrow arrives"), Hits, 0);
    Tick(.6f);
    FPureMassView View;
    TestFalse(TEXT("First lethal contact removes A"), Mass->Resolve(First, View));
    TestEqual(TEXT("Only A has actually been hit"), Hits, 1);
    TestFalse(TEXT("Lethal first target does not invalidate position-based propagation"), Run->IsFinished());
    Mass->Resolve(Second, View);
    TestEqual(TEXT("B is not damaged by an invisible lightning operation"), View.Health, 100.f);
    Tick(.5f);
    Mass->Resolve(Third, View);
    TestEqual(TEXT("C cannot be hit before B's real flight completes"), View.Health, 100.f);
    Tick(3.f);
    TestEqual(TEXT("One payload per actual target contact"), Hits, 3);
    TestTrue(TEXT("Three-step propagation is bounded and completes"), Run->IsFinished());
    TestTrue(TEXT("B survives and resolves"), Mass->Resolve(Second, View));
    TestEqual(TEXT("B received original fire payload once"), View.Health, 75.f);
    TestTrue(TEXT("C survives and resolves"), Mass->Resolve(Third, View));
    TestEqual(TEXT("C received original fire payload once"), View.Health, 75.f);
    return true;
}
#endif
