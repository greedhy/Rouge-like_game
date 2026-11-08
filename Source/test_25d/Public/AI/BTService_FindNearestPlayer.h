// 主要调用链：行为树 TickNode → 枚举敌对标签角色 → 遭遇/存活过滤 → 最近目标与距离写入黑板。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlueprintBase.h"
#include "BTService_FindNearestPlayer.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UBTService_FindNearestPlayer : public UBTService_BlueprintBase
{
    GENERATED_BODY()

protected:
    // 按阵营标签枚举目标，剔除死亡和跨遭遇目标，将最近目标及距离写入黑板。
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    FBlackboardKeySelector TargetToFollowSelector;

    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    FBlackboardKeySelector DistanceToTargetSelector;
};
