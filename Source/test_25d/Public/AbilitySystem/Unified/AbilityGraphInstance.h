// 调用链：GA/World 持有者 -> StartWithInput/Advance -> 注册策略 -> 有界类型事件队列 -> 后继调用。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilityGraphInstance.generated.h"

class UAbilityNodeExecution;

/** 队列载荷中的 UObject 引用必须在观察者回调和后继创建期间保持可达。 */
USTRUCT()
struct FAbilityQueuedReferences
{
    GENERATED_BODY()
    UPROPERTY()
    FDamageEffectParams Damage;
    UPROPERTY()
    TObjectPtr<UObject> Context;
};

/** 服务器内部事件；V1 兼容字段保留，V2 业务载荷位于 Input。 */
struct TEST_25D_API FAbilityGraphEvent
{
    FGuid ExecutionId;
    FGuid NodeInvocationId;
    uint64 EventSequence = 0;
    FName OutputPort;
    FAbilitySourceToken SourceToken;
    FAbilityTargetToken TargetToken;
    FVector Location = FVector::ZeroVector;
    EAbilityGraphEndReason EndReason = EAbilityGraphEndReason::Completed;
    FAbilityNodeInput Input;
    bool bChildrenReserved = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FAbilityGraphFinished, EAbilityGraphEndReason);
DECLARE_MULTICAST_DELEGATE_OneParam(FAbilityGraphEventObserved, const FAbilityGraphEvent&);

/** 所有业务执行类来自注册表；实例只负责身份、预算、队列与资源生命周期。 */
UCLASS(Transient)
class TEST_25D_API UAbilityGraphInstance : public UObject
{
    GENERATED_BODY()
public:
    /** V1 入口保留捕获单目标语义；验证失败不创建执行对象。 */
    bool Start(UAbilityGraphDefinition* Graph, const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
               FString& Error);
    /** 使用内建注册表启动只读配置快照；Schema2 立即有限推进同步根事件。 */
    bool StartWithInput(UAbilityGraphDefinition* Graph, const FAbilityNodeInput& Input,
                        const FAbilityExecutionOptions& Options, FString& Error);
    /** 复制注册表，调用方局部注册表可在启动后销毁。 */
    bool StartWithInput(UAbilityGraphDefinition* Graph, const FAbilityNodeInput& Input,
                        const FAbilityExecutionOptions& Options, const FAbilityNodeRegistry& Registry, FString& Error);
    /** 所属任务/世界每帧调用；检查生命身份并推进本帧开始时已有的策略。 */
    void Advance(float DeltaSeconds);
    /** 先关闭事件入口再停止策略；重复取消不再次触发生命周期回调。 */
    void Cancel(EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Cancelled);
    /** 原生事件入口要求同一执行身份与递增序号，终态及重放不能重新进入队列。 */
    bool EnqueueEvent(const FAbilityGraphEvent& Event);
    /** 自动分配单调序号，端口决定是否关闭当前调用；不会同步递归执行子节点。 */
    bool Publish(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload,
                 EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Completed);
    /** 预留所有出边的调用/活动额度后返回成功；旧子生成拒绝不取消已运行效果。 */
    bool RequestChild(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload);
    /** 结束策略但保留已入队的事件；不自动伪造 Completed 或其它业务端口。 */
    void FinishInvocation(const FGuid& InvocationId, EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Completed);
    bool IsFinished() const
    {
        return bFinished;
    }
    EAbilityGraphEndReason GetEndReason() const
    {
        return EndReason;
    }
    FGuid GetExecutionId() const
    {
        return ExecutionId;
    }
    const FAbilityExecutionOptions& GetOptions() const
    {
        return Options;
    }
    uint64 GetAcceptedEventCount() const
    {
        return EventCount;
    }
    int32 GetActiveInvocationCount() const
    {
        return ActiveCount;
    }

    FAbilityGraphFinished OnFinished;
    FAbilityGraphEventObserved OnEvent;

private:
    struct FInvocation
    {
        FGuid NodeId;
        int32 Depth = 0;
        FAbilityNodeInput Input;
        uint64 LastSequence = 0;
        int32 PendingEvents = 0;
        bool bTerminal = false;
        bool bConsumed = false;
    };
    bool InvokeNode(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input, bool bReserved = false);
    bool ResolveSource();
    bool ValidateInput(const FAbilityNodeInput& Input, EAbilityPortPayload Required, FString& Error) const;
    void DrainEvents();
    void Finish(EAbilityGraphEndReason Reason);
    void ReleaseInvocation(const FGuid& Id);
    const FAbilityNodeDescriptor* DescriptorFor(const FInvocation& Invocation) const;

    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphDefinition> Snapshot;
    UPROPERTY(Transient)
    TMap<FGuid, TObjectPtr<UAbilityNodeExecution>> Executions;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UClass>> RegisteredClasses;
    UPROPERTY(Transient)
    TArray<FAbilityQueuedReferences> QueueReferences;
    FAbilityNodeRegistry Registry;
    FAbilityExecutionOptions Options;
    FAbilityNodeInput RootInput;
    FGuid ExecutionId;
    TMap<FGuid, FInvocation> Invocations;
    TArray<FAbilityGraphEvent> Queue;
    uint64 EventCount = 0;
    int32 ActiveCount = 0;
    int32 ReservedInvocations = 0;
    int32 ReservedActive = 0;
    bool bStarted = false;
    bool bFinished = true;
    bool bAdvancing = false;
    EAbilityGraphEndReason EndReason = EAbilityGraphEndReason::Failed;
};
