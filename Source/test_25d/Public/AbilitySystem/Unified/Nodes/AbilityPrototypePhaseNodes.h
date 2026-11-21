// 调用链：Scope Ready -> 独立时序/表现租约 -> Action/Period/Expired typed 端口 -> 任意图 payload。
// 节点不施加伤害；原型参数只从本次 immutable frame 读取，Stop 精确关闭自己的表现宿主。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypePhaseNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeEndNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UENUM()
enum class EAbilityPrototypePhase : uint8
{
    Lifetime,
    Field,
    Trap,
    Delay,
    ExtensionBurst,
    ExtensionDelay,
    Surface
};

/** 只选择已实现的时间合同；没有 Duration/Radius 等与 Scope 冲突的数值副本。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypePhaseNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere)
    EAbilityPrototypePhase Phase = EAbilityPrototypePhase::Lifetime;
    /** 未登记的时间策略须在提交前拒绝，不按默认值猜测。 */
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};

/** 有限时间、presence 和周期调度；Action 的查询/伤害不在该执行类中实现。 */
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypePhaseNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 校验帧与时间族，生成仅表现宿主；只有 Lifetime 在入口发布一次 Action。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 按 Actor/Mixed 原 cadence 推进；每次 Action 实际同步部分完成后才发布 Period 或 Expired。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 取消只关闭本次宿主和时钟，不生成正常到期分支。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 仅本片段实际 payload 的 Completed 后继可结束无时钟的 Burst，不等待独立子技能生命周期。 */
    void CompleteFromPayload(const UAbilityPrototypeParameterFrame& CompletedFrame);

private:
    /** 同时撤销精确弱索引与本节点的强所有权；不影响仍在执行的子片段。 */
    void ReleasePresentation(bool bCancelled);
    /** 发布后排空当前同步步骤，不等待命中子原型的整个表现寿命。 */
    bool PublishStep(FName Port);
    /** 仅查询是否存在可触发的原生命；实际范围 payload 仍由独立 Query 节点决定。 */
    bool HasTrapTarget(FString& Error);
    /** 正常到期只有一个入口；Expired 连接图中原 OnExpire，不从 DA 再次生成子节点。 */
    void CompletePhase();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    EAbilityPrototypePhase Phase = EAbilityPrototypePhase::Lifetime;
    float Age = 0.f;
    float Clock = 0.f;
    bool bCompleting = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeEndNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityPrototypePhaseNodes
{
    TEST_25D_API FName TypeId(EAbilityPrototypePhase Phase);
    /** 每个 TypeId 声明同根 compatibility-safe、真实异步寿命和位置端口。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypePhaseNodes
