// 调用链：Scope.Prototype Ready -> 独立连锁/光束调度 -> Action TargetSet -> 任意效果/投递 -> 下一步骤。
// 所有数值来自同次 immutable 参数帧；节点拥有目标序列、时钟与表现，不内置伤害或状态。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeTargetNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;

/** 无第二份伤害/几何/时序配置；行为值由 Scope 冻结帧提供。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityTargetSequenceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 原型 Beam 的有限目标维持，不冒充原生 Electrocute 的多目标/输入前端。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityTargetChannelNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityTargetSequenceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 保存最初目标与帧，不在 Start 造成接触；第一次 Advance 才执行首步。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 接触 payload 实际完成后固定下一目标，保留 Actor/Mixed 不同的尾帧和追赶规则。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 关闭本次时钟和表现；取消不发到期分支。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 实际效果步骤完成后才更新访问历史和固定下一原生命；不等待子技能整段寿命。 */
    bool Contact();
    /** 正常到期先发布位置/原生命信息，再关闭本次表现；取消走 Stop。 */
    void CompleteSequence();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    TOptional<FAbilityTargetToken> Current;
    TOptional<FAbilityTargetToken> LastContact;
    TArray<FPureMassCombatTarget> Visited;
    FVector ChainOrigin = FVector::ZeroVector;
    FAbilityPrototypePresentationFrame VisualFrame;
    float Age = 0.f;
    float Clock = 0.f;
    bool bCompleting = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityTargetChannelNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 保存原目标和同次冻结帧，生成独立表现所有者。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 先持续费用/寿命，再每帧一次检查射程和 LOS；Action 与 Period 保持原顺序。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 源失效、外部取消、正常到期均只清理本节点持有的资源。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 发布一个实际目标集合并排空就绪同步动作，再发布该脉冲的辅助周期。 */
    bool Pulse();
    void CompleteChannel();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    TOptional<FAbilityTargetToken> Current;
    FAbilityPrototypePresentationFrame VisualFrame;
    float Age = 0.f;
    float Clock = 0.f;
    float ManaClock = 0.f;
    bool bCompleting = false;
};

namespace AbilityPrototypeTargetNodes
{
    /** 登记 Origin 输入与 Action:TargetSet、Period/Expired/Completed:Origin；测试可局部注册。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeTargetNodes
