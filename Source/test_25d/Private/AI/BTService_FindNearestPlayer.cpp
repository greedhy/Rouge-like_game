// 主要调用链：行为树 TickNode → 枚举敌对标签角色 → 遭遇/存活过滤 → 最近目标与距离写入黑板。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/BTService_FindNearestPlayer.h"
#include "AIController.h"
#include "BehaviorTree/BTFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonRunDirector.h"

// 按阵营标签枚举目标，剔除死亡和跨遭遇目标，将最近目标及距离写入黑板。
void UBTService_FindNearestPlayer::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    APawn* OwningPawn = AIOwner ? AIOwner->GetPawn() : nullptr;
    if (!IsValid(OwningPawn))
        return;
    FName TargetTag = OwningPawn->ActorHasTag(FName("Player")) ? FName("Enemy") : FName("Player");
    TArray<AActor*> ActorsWithTag;
    UGameplayStatics::GetAllActorsWithTag(OwningPawn, TargetTag, ActorsWithTag);

    float ClosestDistance = TNumericLimits<float>::Max(); //默认设置float最大值
    AActor* ClosestActor = nullptr;
    for (auto* Actor : ActorsWithTag)
    {
        if (!IsValid(Actor) || (Actor->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Actor)) ||
            !ADungeonRunDirector::AllowsCombat(OwningPawn, Actor))
            continue;
        float Distance = OwningPawn->GetDistanceTo(Actor);
        if (Distance < ClosestDistance)
        {
            ClosestDistance = Distance;
            ClosestActor = Actor;
        }
    }
    //遍历所有的角色获取到最近的角色和距离

    //设置黑板数据
    UBTFunctionLibrary::SetBlackboardValueAsObject(this, TargetToFollowSelector, ClosestActor);
    UBTFunctionLibrary::SetBlackboardValueAsFloat(this, DistanceToTargetSelector, ClosestDistance);
}
