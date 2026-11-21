// 调用链：Recipe编辑/Graph.Validate -> 注册端口lineage -> 每个新纯动作的配置证明。
// 只分析值与拓扑，不创建运行实例、不授予目标生命权限，也不改变原节点行为。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"

class UAbilityGraphDefinition;
class FAbilityNodeRegistry;

/** 同一份DAG中可能无限重复的实际入边类型；不接受来自FAbilityNodeInput的公开权限标记。 */
struct TEST_25D_API FAbilityRuntimeRepeatLineage
{
    TMap<FGuid, TSet<EAbilityPortPayload>> Inputs;
};

namespace AbilityRuntimePayloadPolicy
{
    /** 从精确登记的无限输出种子沿DAG传播；共享节点汇合时保留全部实际入边类型。 */
    TEST_25D_API void BuildLineage(const UAbilityGraphDefinition& Graph, const FAbilityNodeRegistry& Registry,
                                   FAbilityRuntimeRepeatLineage& Out);
    /** 只有当前端口本身是种子或当前节点已被无限祖先重复调用时才返回true。 */
    TEST_25D_API bool IsRepeatedOutput(const FAbilityGraphNode& Node, FName Port, const FAbilityNodeRegistry& Registry,
                                       const FAbilityRuntimeRepeatLineage& Lineage);
    /** 同时验证显式策略trait、配置类型、实际消费和有界参数；同步标志本身不能推导权限。 */
    TEST_25D_API bool ValidatePureNode(const FAbilityGraphNode& Node, EAbilityPortPayload Incoming,
                                       const FAbilityNodeRegistry& Registry, FString& Error);
    /** 图运行前重算全部lineage，拒绝脱离无限端口、混入有限/原调用权限或配置无证明的纯动作标记。 */
    TEST_25D_API void ValidateGraph(const UAbilityGraphDefinition& Graph, const FAbilityNodeRegistry& Registry,
                                    TArray<FString>& Errors);
} // namespace AbilityRuntimePayloadPolicy
