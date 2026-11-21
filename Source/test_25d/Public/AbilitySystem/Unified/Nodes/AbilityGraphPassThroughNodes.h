// 调用链：状态模板包装已注册片段 -> 编译器连接 Continue -> 同一 GraphInstance 执行 Apply 子树并恢复原载荷。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityGraphPassThroughNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityGraphDefinition;

/** 每个注册类型有独立的载荷合同；配置中的类型必须匹配，防止位置输入冒充活目标。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityGraphPassThroughConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category = "Graph")
    EAbilityPortPayload Payload = EAbilityPortPayload::Origin;

    virtual bool MatchesType(FName TypeId) const override;
};

/** 不推进时间；由当前调用组持有保存的输入，Stop 可在效果回调内同步发生。 */
UCLASS(Transient)
class TEST_25D_API UAbilityGraphPassThroughExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;

private:
    /** 所有效果后继退组后，向同一根发布原输入，不重新捕获目标。 */
    void Resume(EAbilityPortPayload Payload);
};

namespace AbilityGraphPassThroughNodes
{
    /** 复制并验证效果片段；返回副本内续接节点的原始ID，宿主编译时统一重映射。 */
    TEST_25D_API bool WrapBranch(UObject* Outer, const UAbilityGraphDefinition* EffectBranch,
                                 EAbilityPortPayload Payload, UAbilityGraphDefinition*& OutGraph,
                                 FGuid& OutContinuationNode, FName& OutContinuationPort, FString& Error);
    /** 注册有限运行域可用的五种输入；准备库存的资源归还责任不能通过此适配器转发。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityGraphPassThroughNodes
