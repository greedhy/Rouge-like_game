// 调用链：环境初始化 -> RegisterBuiltins/Register；图校验与节点分派 -> Find。
// 描述只声明已实现的能力；传统 GA 的目录登记不会自动产生可组合节点。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"

class UAbilityNodeExecution;

/** 原生能力描述；ConfigClass 不拥有运行配置，目标标志指向同一已捕获目标的可支持形式。 */
struct TEST_25D_API FAbilityNodeDescriptor
{
    FName TypeId;
    TSubclassOf<UAbilityNodeConfig> ConfigClass;
    TSubclassOf<UAbilityNodeExecution> ExecutionClass;
    TArray<FAbilityPortDescriptor> Ports;
    EAbilityPortPayload InputPayload = EAbilityPortPayload::LiveTarget;
    EAbilityExecutionLifetime Lifetime = EAbilityExecutionLifetime::BoundToAbility;
    TArray<FName> OutputPorts;
    bool bSupportsActorTargets = false;
    bool bSupportsMassTargets = false;
    bool bSupportsSnapshotTargets = false;
    bool bIsAsync = false;
};

/** 在图执行前完成注册；Find 返回的指针只在后续 Register 修改映射前有效。 */
class TEST_25D_API FAbilityNodeRegistry
{
public:
    /** 失败时不写入映射；拒绝重复 TypeId、不可实例化配置、无目标能力或无效端口。 */
    bool Register(const FAbilityNodeDescriptor& Descriptor, FString& Error);

    /** 未注册 TypeId 返回空，不回退到旧 GA 或按类名猜测执行器。 */
    const FAbilityNodeDescriptor* Find(FName TypeId) const;

    /** 注册 Delay 与 Petrify 的真实能力；重复初始化不覆盖已有注册。 */
    void RegisterBuiltins();

    /** 只承认明确列出的兼容族，防止任意字符串绕过流式预算保护。 */
    static bool IsLegacyProfile(FName Profile);

private:
    TMap<FName, FAbilityNodeDescriptor> Descriptors;
};
