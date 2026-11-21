// 测试链：旧真实BFS伤害对照/运行时分叉图 -> 实际PM接触 -> 可替换伤害或Slow -> Mass属性与状态。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityBehaviorNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "Actor/MyProjectile.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassCrowd.h"
#include "MassEntitySubsystem.h"
#include "EngineUtils.h"
#include "MyGameplayTags.h"
#include "Misc/AutomationTest.h"

namespace AbilityForkedPropagationTests
{
    struct FFixture : FAbilityGraphTestFixture
    {
        UPureMassSubsystem* Mass = nullptr;
        UPureMassMonsterType* Healthy = nullptr;
        FFixture()
        {
            Target->Destroy();
            Mass = World->GetSubsystem<UPureMassSubsystem>();
            Mass->Enable();
            Healthy = NewObject<UPureMassMonsterType>(World);
            Healthy->Health = 200.f;
            Healthy->XPReward = 0;
        }

        int32 Spawn(FVector Position, float Health = 200.f) const
        {
            if (Health == Healthy->Health)
            {
                return Mass->SpawnMonster(Healthy, Position);
            }
            auto* Type = NewObject<UPureMassMonsterType>(World);
            Type->Health = Health;
            Type->XPReward = 0;
            return Mass->SpawnMonster(Type, Position);
        }

        FAbilityNodeInput MakeInput(int32 First, FString& Error) const
        {
            FAbilityNodeInput Payload;
            Payload.SourceToken = SourceToken;
            Payload.Origin = FVector(60, 0, 0);
            Payload.Direction = FVector::ForwardVector;
            Payload.EventCenter = FVector(300, 0, 0);
            Payload.DamageParams.BaseDamage = 40.f;
            Payload.DamageParams.DamageType = FMyGameplayTags::Get().Damage_Fire;
            FAbilityTargetToken FirstToken;
            if (AbilityExecutionIdentity::CaptureTarget(SourceToken, FPureMassCombatTarget(First), FirstToken, Error))
            {
                Payload.LiveTargetToken = FirstToken;
            }
            return Payload;
        }

        FGuid Add(UAbilityGraphDefinition& Definition, FName Type, UAbilityNodeConfig* Config) const
        {
            FAbilityGraphNode Node;
            Node.NodeId = FGuid::NewGuid();
            Node.TypeId = Type;
            Node.Config = Config;
            Definition.Nodes.Add(Node);
            return Node.NodeId;
        }

        UAbilityGraphDefinition* Build(bool bOld, bool bSlow) const
        {
            auto* Definition = NewObject<UAbilityGraphDefinition>(World);
            Definition->SchemaVersion = 2;
            Definition->CompatibilityProfile = TEXT("UnifiedV2");
            FGuid Root;
            if (bOld)
            {
                auto* Config = NewObject<UAbilityForkedChainNodeConfig>(Definition);
                Config->MaxTargets = 5;
                Config->MaxDepth = 2;
                Config->BranchesPerTarget = 2;
                Config->JumpRadius = 350.f;
                Config->DamageFalloff = .5f;
                Root = Add(*Definition, TEXT("Delivery.ForkedChain"), Config);
            }
            else
            {
                auto* Config = NewObject<UAbilityForkedPropagationNodeConfig>(Definition);
                Config->MaxTargets = 5;
                Config->MaxDepth = 2;
                Config->BranchesPerTarget = 2;
                Config->JumpRadius = 350.f;
                Config->JumpDelay = .1f;
                Config->DamageFalloff = .5f;
                Config->Delivery.ProjectileClass = AMyProjectile::StaticClass();
                Config->Delivery.Speed = 400.f;
                Config->Delivery.MaxDistance = 1000.f;
                Config->Delivery.TimeoutSeconds = 5.f;
                Config->Delivery.CollisionRadius = 8.f;
                Config->Delivery.bApplyDamage = false;
                Root = Add(*Definition, TEXT("Propagation.Forked"), Config);
                const auto Resolve = Add(*Definition, TEXT("Target.ResolveImpact"),
                                         NewObject<UAbilityResolveImpactNodeConfig>(Definition));
                FGuid Payload;
                if (bSlow)
                {
                    auto* Slow = NewObject<UAbilitySlowNodeConfig>(Definition);
                    Slow->Multiplier = .4f;
                    Slow->Duration = 8.f;
                    Payload = Add(*Definition, TEXT("Status.Slow"), Slow);
                }
                else
                {
                    Payload = Add(*Definition, TEXT("Effect.InputDamage"),
                                  NewObject<UAbilityInputDamageNodeConfig>(Definition));
                }
                Definition->Edges = {{Root, Resolve, TEXT("Hit")}, {Resolve, Payload, TEXT("TargetFound")}};
            }
            Definition->EntryNodeId = Root;
            return Definition;
        }

        FAbilityNodeRegistry Registry() const
        {
            FAbilityNodeRegistry Result;
            Result.RegisterBuiltins();
            AbilityForkedPropagationNodes::Register(Result);
            return Result;
        }

        /** 仅推进实际ProjectileMovement和所属图；不伪造接触回执或直接施加测试伤害。 */
        void Fly(UAbilityGraphInstance& Run, float Seconds, TSet<TWeakObjectPtr<AMyProjectile>>& Seen) const
        {
            for (float Elapsed = 0.f; Elapsed + UE_SMALL_NUMBER < Seconds && !Run.IsFinished(); Elapsed += .01f)
            {
                ++GFrameCounter;
                TArray<TWeakObjectPtr<AMyProjectile>> Shots;
                for (TActorIterator<AMyProjectile> It(World); It; ++It)
                {
                    if (!It->IsActorBeingDestroyed())
                    {
                        Shots.Add(*It);
                        Seen.Add(*It);
                    }
                }
                for (const auto Shot : Shots)
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

        int32 LiveShots() const
        {
            int32 Count = 0;
            for (TActorIterator<AMyProjectile> It(World); It; ++It)
            {
                Count += !It->IsActorBeingDestroyed() ? 1 : 0;
            }
            return Count;
        }
    };
} // namespace AbilityForkedPropagationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForkedPropagationRealBFS,
                                 "test_25d.DynamicMage.ForkedPropagation.RealReceiptsBreadthFirstAndFalloff",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FForkedPropagationRealBFS::RunTest(const FString&)
{
    using namespace AbilityForkedPropagationTests;
    for (const bool bOld : {true, false})
    {
        FFixture F;
        const int32 A = F.Spawn(FVector(300, 0, 0), 10.f);
        const int32 B = F.Spawn(FVector(550, -150, 0));
        const int32 C = F.Spawn(FVector(550, 150, 0));
        const int32 D = F.Spawn(FVector(800, -300, 0));
        const int32 E = F.Spawn(FVector(800, 300, 0));
        FString Error;
        const auto Payload = F.MakeInput(A, Error);
        if (!TestTrue(TEXT("First real target life is captured"), Payload.LiveTargetToken.IsSet()))
        {
            AddError(Error);
            return false;
        }
        auto* Run = NewObject<UAbilityGraphInstance>(F.World);
        TArray<int32> Hits;
        Run->OnEvent.AddLambda(
            [&](const FAbilityGraphEvent& Event)
            {
                if (Event.NodeInvocationId.IsValid() &&
                    Event.OutputPort == (bOld ? TEXT("DamageResolved") : TEXT("Hit")) && Event.Input.Impact.IsSet())
                {
                    Hits.Add(Event.Input.Impact->ConfirmedTarget.Target.EntityId);
                }
            });
        if (!TestTrue(TEXT("Real old or replaceable fork graph starts"),
                      Run->StartWithInput(F.Build(bOld, false), Payload, {}, F.Registry(), Error)))
        {
            AddError(Error);
            return false;
        }
        TestFalse(TEXT("Start is not accepted with a hidden execution failure"),
                  Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
        TSet<TWeakObjectPtr<AMyProjectile>> Seen;
        if (!bOld)
        {
            TestEqual(TEXT("Only the first actual arrow exists before any real hit"), F.LiveShots(), 1);
            F.Fly(*Run, .2f, Seen);
            TestEqual(TEXT("No fork branch or payload before the first arrow reaches A"), Hits.Num(), 0);
            TestEqual(TEXT("Unreached A keeps its actual health"), F.Health(A), 10.f);
        }
        F.Fly(*Run, 10.f, Seen);
        TestTrue(TEXT("Finite BFS ends after its admitted real contacts"), Run->IsFinished());
        TestEqual(TEXT("First actual hit is lethal and does not prevent expansion"), F.Health(A), 0.f);
        TestEqual(TEXT("Sibling B consumes depth-one falloff once"), F.Health(B), 180.f);
        TestEqual(TEXT("Sibling C consumes depth-one falloff once"), F.Health(C), 180.f);
        TestEqual(TEXT("Grandchild D consumes depth-two falloff once"), F.Health(D), 190.f);
        TestEqual(TEXT("Grandchild E consumes depth-two falloff once"), F.Health(E), 190.f);
        if (TestEqual(TEXT("Exactly one admitted contact per original life"), Hits.Num(), 5))
        {
            TestEqual(TEXT("The actual first contact stays first"), Hits[0], A);
            TestTrue(TEXT("Both depth-one siblings precede every grandchild"),
                     (Hits[1] == B || Hits[1] == C) && (Hits[2] == B || Hits[2] == C) && Hits[1] != Hits[2]);
            TestTrue(TEXT("Only depth-two children occupy the final BFS positions"),
                     (Hits[3] == D || Hits[3] == E) && (Hits[4] == D || Hits[4] == E) && Hits[3] != Hits[4]);
        }
        if (!bOld)
        {
            TestEqual(TEXT("Each real delivery creates one actual projectile"), Seen.Num(), 5);
            TestEqual(TEXT("Actual deliveries consume one shared root physical budget"),
                      Run->GetPhysicalSpawnCountTotal(), uint64(5));
            TestEqual(TEXT("Finished propagation leaves no owned projectile"), F.LiveShots(), 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForkedPropagationStatusOnly,
                                 "test_25d.DynamicMage.ForkedPropagation.ReplaceableSlowHasNoHiddenDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FForkedPropagationStatusOnly::RunTest(const FString&)
{
    using namespace AbilityForkedPropagationTests;
    FFixture F;
    const int32 A = F.Spawn(FVector(300, 0, 0));
    const int32 B = F.Spawn(FVector(550, -150, 0));
    const int32 C = F.Spawn(FVector(550, 150, 0));
    FString Error;
    const auto Payload = F.MakeInput(A, Error);
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    if (!TestTrue(TEXT("The same fork behavior accepts an independent Slow payload"),
                  Run->StartWithInput(F.Build(false, true), Payload, {}, F.Registry(), Error)))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("Status-only fork does not hide a failed Start"),
              Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Failed);
    TSet<TWeakObjectPtr<AMyProjectile>> Seen;
    F.Fly(*Run, 10.f, Seen);
    TestTrue(TEXT("Status-only BFS terminates without a damage receipt"), Run->IsFinished());
    for (const int32 Id : {A, B, C})
    {
        TestEqual(TEXT("Propagation and delivery do not contain an implicit damage owner"), F.Health(Id), 200.f);
        const auto& Fragment =
            F.World->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager().GetFragmentDataChecked<FPureMassFragment>(
                F.Mass->GetEntity(Id));
        if (TestEqual(TEXT("Each actual contact applies the original independent Slow service"),
                      Fragment.SpellSlows.Num(), 1))
        {
            TestTrue(TEXT("Replaced payload keeps its authored multiplier"),
                     FMath::IsNearlyEqual(Fragment.SpellSlows[0].X, .4, 1.e-6));
        }
    }
    TestEqual(TEXT("Only three successful physical contacts were delivered"), Seen.Num(), 3);
    return true;
}
#endif
