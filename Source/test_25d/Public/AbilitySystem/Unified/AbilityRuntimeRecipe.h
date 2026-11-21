// 调用链：玩家配方组件/开发面板 -> 编译只读基础图及有序来源修正 -> 本次施法快照 -> 原 GraphInstance。
// 本层只生成和校验配置，不激活 GA、不扣费、不执行伤害；所有 UObject 由调用者/组件的 UPROPERTY 保活。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityRuntimeRecipe.generated.h"

class UAbilityGraphDefinition;
class UGameplayAbility;

/** 仅允许有明确配置合同的操作；不接受客户端反射属性名或任意代码。 */
UENUM(BlueprintType)
enum class EAbilityRecipePatchOperation : uint8
{
    DamageScale,
    RadiusScale,
    DurationScale,
    ReplaceNode,
    AppendBranch,
    ConfigureParameter,
    ReplaceBranch,
    InsertOnEdge
};

/** 目标 ID 精确匹配；未填 ID 时按 TypeId，二者均空时仅匹配基础图入口。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeRecipePatch
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid TargetNodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TargetTypeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityRecipePatchOperation Operation = EAbilityRecipePatchOperation::DamageScale;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Scalar = 1.f;
    /** 仅ConfigureParameter消费；每条patch只设置一个已登记轴，参数名不从客户端反射解析。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityConfigParameter Parameter = EAbilityConfigParameter::Count;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ReplacementTypeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
    TObjectPtr<UAbilityNodeConfig> ReplacementConfig;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UAbilityGraphDefinition> BranchGraph;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName OutputPort;
    /** 精确插入只替换 (TargetNodeId, OutputPort, TargetEdgeTo)；续接节点ID位于重映射前的片段内。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid TargetEdgeTo;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGuid ContinuationNodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ContinuationPort;
};

/** 来源是装备实例/开发修正等稳定身份；相同来源更新替换旧贡献，而不是重复叠加。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeRecipeContribution
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SourceId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Priority = 0;
    /** 服务器工作台的顺序历史；仅严格更高Priority的同类历史可覆盖已有设置/替换。
     * 普通来源缺省false，保留并行来源冲突拒绝；客户端Edit/RPC不携带此字段。
     */
    UPROPERTY()
    bool bOrderedWorkshopEdit = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAbilityRuntimeRecipePatch> Patches;
};

/** 每次施法独占的图配置；不携带 ASC/Avatar 权限，执行时仍须捕获原生命令牌。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeRecipeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityTag;
    UPROPERTY(BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(Transient, BlueprintReadOnly)
    TObjectPtr<UAbilityGraphDefinition> Graph;
    UPROPERTY(BlueprintReadOnly)
    FString ContentHash;
};

namespace AbilityRuntimeRecipes
{
    /** 从真实 GA 默认定义生成独立图；传统专用前端尚无基础图时明确失败，由其动作桥提供定义。 */
    TEST_25D_API UAbilityGraphDefinition* BuildDefaultGraph(UObject* Outer, const UGameplayAbility* Ability,
                                                            FString& Error);
    /** 确定性排序贡献并应用到私有副本；任何非法修正返回 false，OutGraph 清空且不修改输入。 */
    TEST_25D_API bool Compile(UObject* Outer, const UAbilityGraphDefinition* Base,
                              const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                              UAbilityGraphDefinition*& OutGraph, FString& Error);
    /** 返回配置内容摘要，不以瞬态图地址或调用者名字作为身份。 */
    TEST_25D_API FString Fingerprint(const UAbilityGraphDefinition& Graph);
} // namespace AbilityRuntimeRecipes
