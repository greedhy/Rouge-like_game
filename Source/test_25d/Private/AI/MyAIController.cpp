// 主要调用链：构造控制器 → 创建 Blackboard/BehaviorTreeComponent → 供敌人接管与行为树使用。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/MyAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
// 创建并检查黑板和行为树组件，供角色接管时使用。
AMyAIController::AMyAIController()
{
    Blackboard = CreateDefaultSubobject<UBlackboardComponent>("BlackboardComponent");
    check(Blackboard);
    BehaviorTreeComponent = CreateDefaultSubobject<UBehaviorTreeComponent>("BehaviorTreeComponent");
    check(BehaviorTreeComponent);
}
