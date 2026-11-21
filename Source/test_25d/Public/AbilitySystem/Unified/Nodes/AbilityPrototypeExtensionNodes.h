// 调用链：Extension Scope -> 原生命显式解析 -> 现有 Attach/Stack/Displace 执行类 -> 任意类型化后继。
// 空配置只声明消费本次帧，不复制可独立编辑的数值；世界状态继续由原 ExtensionWorld 持有。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityPrototypeExtensionNodes.generated.h"
class FAbilityNodeRegistry;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeLiveNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeAttachNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeStackNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeDisplaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeAreaDisplaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeLiveNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 只复核已捕获的原生命；致死事实只进入 Stack Confirmed 端口，不重新捕获目标。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypeExtensionNodes
{
    /** 由参数注册入口调用；全部沿用已有状态执行器，没有第二份 scheduler。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeExtensionNodes
