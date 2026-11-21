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
    /** 持续根与可作为持续后继分别登记，避免任意兼容字符串开放无界生成策略。 */
    bool bSustainedRoot = false;
    bool bSustainedSafe = false;
    /** 仅显式跟踪策略在 Advance 管理当前锚生命/转移；Start 的 LiveTarget 准入仍严格验证。 */
    bool bOwnsTargetTracking = false;
    /** 兼容根/辅助安全/逻辑片段入口独立登记；原完整Legacy宏不能借此宣称已拆分。 */
    bool bPrototypeCompatibilityRoot = false;
    bool bPrototypeCompatibilitySafe = false;
    bool bPrototypeLogicalEntry = false;
    /** 仅显式无业务端点可声明原预算事件；普通helper不能伪造原终结。 */
    bool bOriginalProjectionEndpoint = false;
    /** 新增运行节点的独立准入：真实生成量须经同根预算，不能因Profile或Type已注册而豁免。 */
    bool bRuntimeFiniteSafe = false;
    /** 精确列出可能无限发布的端口；有限Emitter/Lightning不能因“重复”或持续预算自动取得此权限。 */
    TArray<FName> PotentiallyUnboundedOutputPorts;
    /** 运行时新增的纯动作独立审计：同步结束，不拥有新的执行调度/物理生成；目标状态到期沿用目标所有者。 */
    bool bRuntimePurePayload = false;
    /** 原策略允许被纯动作替换的显式槽位；不是给任意旧类型字符串授予持续执行权限。 */
    bool bRuntimePayloadReplacementSlot = false;
};

/** 在图执行前完成注册；Find 返回的指针只在后续 Register 修改映射前有效。 */
class TEST_25D_API FAbilityNodeRegistry
{
public:
    /** 失败时不写入映射；拒绝重复 TypeId、不可实例化配置、无目标能力或无效端口。 */
    bool Register(const FAbilityNodeDescriptor& Descriptor, FString& Error);

    /** 未注册 TypeId 返回空，不回退到旧 GA 或按类名猜测执行器。 */
    const FAbilityNodeDescriptor* Find(FName TypeId) const;
    /** 排序后的只读能力ID，供覆盖审计；不允许外部修改实际注册映射。 */
    TArray<FName> GetRegisteredTypeIds() const;

    /** 注册 Delay 与 Petrify 的真实能力；重复初始化不覆盖已有注册。 */
    void RegisterBuiltins();

    /** 只承认明确列出的兼容族，防止任意字符串绕过流式预算保护。 */
    static bool IsLegacyProfile(FName Profile);
    static bool IsPrototypeCompatibilityProfile(FName Profile);

private:
    TMap<FName, FAbilityNodeDescriptor> Descriptors;
};
