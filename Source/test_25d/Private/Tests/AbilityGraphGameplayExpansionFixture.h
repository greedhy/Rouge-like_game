// 测试链：技能图扩展用例 → 临时Actor/Mass世界 → 正式投递与命中；不保存资产。
#pragma once
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

namespace GraphGameplayExpansionTests
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
} // namespace GraphGameplayExpansionTests
