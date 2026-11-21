// 调用链：片段准备节点绑定根执行 -> 同根事件/策略只读共享 -> 队列与执行对象保活 -> 对象回收。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilityExecutionParameters.generated.h"

/** 不可变参数帧的生命周期基类；派生类只能持有值/配置，不得拥有场景Actor、定时器或效果句柄。 */
UCLASS(Abstract, Transient)
class TEST_25D_API UAbilityExecutionParameters : public UObject
{
    GENERATED_BODY()
public:
    /** 仅新建帧绑定一次原根/来源；后继不能把共享参数帧重新绑定给另一施法者或另一根。 */
    bool BindExecution(FGuid InRoot, const FAbilitySourceToken& InSource);
    /** 图校验共享帧所属身份；活源权限仍由 GraphInstance 每次独立重新验证。 */
    bool BelongsTo(FGuid InRoot, const FAbilitySourceToken& InSource) const;

private:
    FGuid Root;
    FAbilitySourceToken Source;
};
