// 主要调用链：行为树 ExecuteTask → UBTTask_BlueprintBase；实际攻击由上层或蓝图任务逻辑提供。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlueprintBase.h"
#include "BTTask_Attack.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UBTTask_Attack : public UBTTask_BlueprintBase
{
    GENERATED_BODY()
    // 将任务执行交给蓝图任务基类；此处没有独立的攻击或伤害实现。
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
