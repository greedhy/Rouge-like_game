// 主要调用链：行为树 ExecuteTask → UBTTask_BlueprintBase；实际攻击由上层或蓝图任务逻辑提供。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/BTTask_Attack.h"

// 将任务执行交给蓝图任务基类；此处没有独立的攻击或伤害实现。
EBTNodeResult::Type UBTTask_Attack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    return Super::ExecuteTask(OwnerComp, NodeMemory);
}
