// 调用链：GA/制作校验 -> GraphDefinition::Validate -> 注册表、配置与 DAG 预算检查。
// 此 DA 不持有施法状态；Validate 为只读入口，失败时不得进入费用提交或节点调度。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityGraphDefinition.generated.h"

class FAbilityNodeRegistry;

UCLASS(BlueprintType)
class TEST_25D_API UAbilityGraphDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    int32 SchemaVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FGuid EntryNodeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    TArray<FAbilityGraphNode> Nodes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    TArray<FAbilityGraphEdge> Edges;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FAbilityGraphBudget Budget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    EAbilityGraphBudgetPolicy BudgetPolicy = EAbilityGraphBudgetPolicy::Bounded;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FName CompatibilityProfile = TEXT("UnifiedV1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    EAbilityGraphCancelScope CancelScope = EAbilityGraphCancelScope::Cast;

    /** 替换 Errors，检查静态契约及展开后的调用/事件/深度预算；不修复资产，不创建运行资源。 */
    bool Validate(const FAbilityNodeRegistry& Registry, TArray<FString>& Errors) const;

    /** 编辑器/Python 调用同一原生校验器，返回逐节点错误；不会修复或保存共享配置。 */
    UFUNCTION(BlueprintCallable, Category = "Graph")
    bool ValidateForAuthoring(TArray<FString>& Errors) const;
};
