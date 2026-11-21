// 调用链：GA/制作校验 -> GraphDefinition::Validate -> 注册表、配置与 DAG 预算检查。
// 此 DA 不持有施法状态；Validate 为只读入口，失败时不得进入费用提交或节点调度。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityGraphDefinition.generated.h"

class FAbilityNodeRegistry;
class UAbilityGraphDefinition;
struct FAbilityRuntimeRecipeContribution;

namespace AbilityRuntimeRecipes
{
    TEST_25D_API bool Compile(UObject* Outer, const UAbilityGraphDefinition* Base,
                              const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                              UAbilityGraphDefinition*& OutGraph, FString& Error);
}

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
    FAbilitySustainedBudget SustainedBudget;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PrototypeCompatibility")
    FAbilityPrototypeHelperBudget PrototypeHelperBudget;
    UPROPERTY(VisibleAnywhere, Category = "PrototypeCompatibility")
    FAbilityOriginalGraphProjection OriginalProjection;
    /** 所有局内插入节点共用一份普通有限预算，不按每个来源重置额度。 */
    UPROPERTY(VisibleAnywhere, Category = "RuntimeRecipe")
    bool bHasRuntimeFinite = false;
    UPROPERTY(VisibleAnywhere, Category = "RuntimeRecipe")
    FAbilityGraphBudget RuntimeFiniteBudget;

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

private:
    friend bool AbilityRuntimeRecipes::Compile(UObject* Outer, const UAbilityGraphDefinition* Base,
                                               const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                                               UAbilityGraphDefinition*& OutGraph, FString& Error);

    /** 编译快照的私有内容封印；普通UPROPERTY确保UE5.8持久化Duplicate档案也保留，旧资产缺省空。
     *  Compile输出本身为RF_Transient；本字段不复制网络、不进入配方摘要或游戏存档通路。
     *  仅Compile能重置私有Working并重新封印；防服务器误改快照，不是任意C++反射沙箱。 */
    UPROPERTY()
    FString RuntimeCompilationSeal;
};
