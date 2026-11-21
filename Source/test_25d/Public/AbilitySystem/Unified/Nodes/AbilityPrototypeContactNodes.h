// 调用链：批接触 receipt -> 可替换 Slow/Impact typed 节点 -> 归还同图 continuation -> 下一个目标。
// 配置不拥有第二份数值；参数来自本片段帧，表现通过图的弱索引取得准确宿主，不修改帧生命周期。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPrototypeContactNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeSlowNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeImpactNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeSlowNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 致死接触保留历史结果但不施加新状态；活目标使用现有共享 Slow 服务。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeImpactNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 一次历史接触生成一次表现；已关闭的宿主不重开，缺失目标不改变已确认位置。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypeContactNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
