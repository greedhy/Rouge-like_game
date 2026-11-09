// 调用链：UnifiedGA 自动化 -> 临时 World/ASC/目标 -> 真实图与状态服务；析构撤销 WorldContext。
#pragma once

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "Character/EnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

/** 仅测试使用的服务器世界；不 BeginPlay，不启动 AI、HUD 或修改用户资产。 */
struct FAbilityGraphTestFixture
{
    UWorld* World = nullptr;
    AEnemyCharacter* Source = nullptr;
    AEnemyCharacter* Target = nullptr;
    UAbilitySystemComponent* ASC = nullptr;
    FAbilitySourceToken SourceToken;
    FAbilityTargetToken TargetToken;
    UAbilityGraphDefinition* Graph = nullptr;

    /** 建立敌对双方及一个 0.25 秒延迟、1 秒石化的独立内存配置。 */
    FAbilityGraphTestFixture()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Source = World->SpawnActor<AEnemyCharacter>();
        Source->Tags.Remove(TEXT("Enemy"));
        Source->Tags.Add(TEXT("Player"));
        Target = World->SpawnActor<AEnemyCharacter>();
        Target->Tags.AddUnique(TEXT("Enemy"));
        Target->SetActorLocation(FVector(150, 0, 0));
        ASC = Source->GetAbilitySystemComponent();
        ASC->InitAbilityActorInfo(Source, Source);
        // 未 BeginPlay 的临时世界不会走组件初始化注册 AttributeSet，夹具显式完成此步骤。
        ASC->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Source));
        Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
        Target->GetAbilitySystemComponent()->AddAttributeSetSubobject(NewObject<UMyAttributeSet>(Target));
        FString Error;
        AbilityExecutionIdentity::CaptureSource(Source, SourceToken, Error);
        AbilityExecutionIdentity::CaptureTarget(SourceToken, FPureMassCombatTarget::FromActor(Target), TargetToken,
                                                Error);
        Graph = NewObject<UAbilityGraphDefinition>(World);
        FAbilityGraphNode Delay;
        Delay.NodeId = FGuid::NewGuid();
        Delay.TypeId = TEXT("Control.Delay");
        auto* DelayConfig = NewObject<UAbilityDelayNodeConfig>(Graph);
        DelayConfig->DelaySeconds = .25f;
        Delay.Config = DelayConfig;
        FAbilityGraphNode Petrify;
        Petrify.NodeId = FGuid::NewGuid();
        Petrify.TypeId = TEXT("Status.Petrify");
        auto* PetrifyConfig = NewObject<UAbilityPetrifyNodeConfig>(Graph);
        PetrifyConfig->DurationSeconds = 1.f;
        Petrify.Config = PetrifyConfig;
        Graph->EntryNodeId = Delay.NodeId;
        Graph->Nodes = {Delay, Petrify};
        FAbilityGraphEdge Edge;
        Edge.From = Delay.NodeId;
        Edge.To = Petrify.NodeId;
        Edge.OutputPort = TEXT("Completed");
        Graph->Edges.Add(Edge);
    }

    /** 释放测试世界；组件 EndPlay/世界清理负责释放状态和委托。 */
    ~FAbilityGraphTestFixture()
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** 查询真实组件，不以图的完成标志代替玩法生效证据。 */
    bool IsPetrified() const
    {
        return Target->GetMesh()->bPauseAnims;
    }
};
