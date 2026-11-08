// 主要调用链：构造控制器 → 创建 Blackboard/BehaviorTreeComponent → 供敌人接管与行为树使用。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MyAIController.generated.h"

class UBlackboardComponent;
class UBehaviorTreeComponent;
/**
 * 
 */
UCLASS()
class TEST_25D_API AMyAIController : public AAIController
{
    GENERATED_BODY()
public:
    // 创建并检查黑板和行为树组件，供角色接管时使用。
    AMyAIController();

protected:
    UPROPERTY()
    TObjectPtr<UBehaviorTreeComponent> BehaviorTreeComponent;
};
