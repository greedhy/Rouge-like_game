// 调用链：GA/World 持有者 -> StartWithInput/Advance -> 注册策略 -> 有界类型事件队列 -> 后继调用。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilityGraphInstance.generated.h"

class UAbilityNodeExecution;
class UAbilityExecutionParameters;

/** 只读准入区分临时并发/速率背压与累计额度耗尽；不创建租约、不改变预算。 */
enum class EAbilityPhysicalAdmission : uint8
{
    Ready,
    Wait,
    Exhausted
};

/** 队列载荷中的 UObject 引用必须在观察者回调和后继创建期间保持可达。 */
USTRUCT()
struct FAbilityQueuedReferences
{
    GENERATED_BODY()
    UPROPERTY()
    FDamageEffectParams Damage;
    UPROPERTY()
    TObjectPtr<UObject> Context;
    UPROPERTY()
    TObjectPtr<UAbilityExecutionParameters> ParameterFrame;
};

/** 服务器内部事件；V1 兼容字段保留，V2 业务载荷位于 Input。 */
struct TEST_25D_API FAbilityGraphEvent
{
    FGuid ExecutionId;
    FGuid NodeInvocationId;
    uint64 EventSequence = 0;
    FName OutputPort;
    /** 观察者使用的稳定策略身份；不复制临时执行对象/图来解释事件。 */
    FName NodeType;
    FAbilitySourceToken SourceToken;
    FAbilityTargetToken TargetToken;
    FVector Location = FVector::ZeroVector;
    EAbilityGraphEndReason EndReason = EAbilityGraphEndReason::Completed;
    FAbilityNodeInput Input;
    bool bChildrenReserved = false;
    /** 仅Enqueue依据已验证映射重新计算；外部事件不能自行声明原预算身份。 */
    bool bOriginalPublication = false;
    bool bFinitePublication = false;
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
    /** 外部接触/计时边界只排空现有事件，不推进时间；重入请求由同一迭代drainer处理。 */
    bool FlushPublishedEvents(bool bRequireImmediate = false);
    /** 原型接触只同步结算本体载荷；逻辑子片段留到本轮 Advance 结束，仍使用同一队列与额度。 */
    bool FlushImmediatePayloads(bool bRequireImmediate = true);
    /** 先关闭事件入口再停止策略；重复取消不再次触发生命周期回调。 */
    void Cancel(EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Cancelled);
    /** 原生事件入口要求同一执行身份与递增序号，终态及重放不能重新进入队列。 */
    bool EnqueueEvent(const FAbilityGraphEvent& Event);
    /** 自动分配单调序号，端口决定是否关闭当前调用；不会同步递归执行子节点。 */
    bool Publish(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload,
                 EAbilityGraphEndReason Reason = EAbilityGraphEndReason::Completed);
    /** Schema2非终态输出等待全部后继完成后继续；回调只捕获弱对象，取消丢弃，不递归或推进时钟。 */
    bool PublishWithContinuation(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload,
                                 TFunction<void()> Continuation);
    /** 弱索引不取得资源所有权；帧必须属于当前根，真实宿主仍由注册节点持有并清理。 */
    bool RegisterRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot, UObject* Resource);
    /** 只查当前根同一帧的资源；结束、失效或其它施法的帧返回空。 */
    UObject* FindRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot) const;
    /** 只有准确注册值能解绑，旧回调不能移除后来替换的宿主。 */
    bool UnregisterRuntimeResource(const UAbilityExecutionParameters& Frame, FName Slot, UObject* ExactResource);
    /** 预留所有出边的调用/活动额度后返回成功；旧子生成拒绝不取消已运行效果。 */
    bool RequestChild(const FGuid& InvocationId, FName Port, const FAbilityNodeInput& Payload);
    /** 为本图所有分支累计预留实际弹体/投递生成量；拒绝零负数、来源失效和超过根预算，不回退额度。 */
    bool ReservePhysicalSpawn(int32 Count = 1);
    /** 普通局内新增节点必须给出实际调用；私有有限域不会借宿主兼容/持续的物理额度。 */
    bool ReservePhysicalSpawn(const FGuid& InvocationId, int32 Count = 1);
    /** 同根兼容查询首次捕获、后续只验证原生命；目标池复用不会被后续脉冲重新认领。 */
    bool ResolveOrCaptureOriginalTarget(const FPureMassCombatTarget& Target, FAbilityTargetToken& OutTarget,
                                        FVector& OutLocation, FString& Error);
    /** 配置阶段声明的真实后继；没有消费者的批结果不制造逐目标队列事件。 */
    bool HasSuccessor(const FGuid& InvocationId, FName Port) const;
    /** 持续投递使用精确资源租约；释放只归还该租约的并发额，不退还速率或有限累计预算。 */
    bool ReservePhysicalSpawnWithLease(FGuid& OutLease, int32 Count = 1);
    /** 新有限域累计64/并发32；租约仍进同一物理账本，取消和归还不能把累计生成额退回。 */
    bool ReservePhysicalSpawnWithLease(const FGuid& InvocationId, FGuid& OutLease, int32 Count = 1);
    EAbilityPhysicalAdmission CheckPhysicalAdmission(const FGuid& InvocationId, int32 Count = 1) const;
    bool ReleasePhysicalSpawnLease(FGuid Lease);
    int32 GetActivePhysicalSpawnCount() const
    {
        return ActivePhysicalSpawnCount;
    }
    uint64 GetPhysicalSpawnCountTotal() const
    {
        return PhysicalSpawnCountTotal;
    }
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
    /** 逻辑片段消费的原兼容预算；辅助查询/表现等不能把它当作自己的累计次数。 */
    int32 GetLogicalInvocationLimit() const
    {
        return Snapshot ? Snapshot->Budget.MaxInvocations : 0;
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
        /** 该调用所属的后继组；父调用的继续函数等待整个组清空。 */
        FGuid ContinuationGroup;
        int32 PendingContinuations = 0;
        bool bTerminal = false;
        bool bConsumed = false;
        /** 透明终态桥可先归还活动位；待消费metadata/组占用仍按原PendingEvents生命周期回收。 */
        bool bActiveReleased = false;
        /** 私有继承的原调用身份；不写入可由调用者构造的业务Input。 */
        FGuid OriginalCall;
        bool bRuntimeFinite = false;
        int32 FiniteDepth = 0;
    };
    /** 原节点累计记录不随helper回收；活动额直到原终态及其已发布事件消费后归还。 */
    struct FOriginalInvocation
    {
        int32 DefinitionIndex = INDEX_NONE;
        int32 Depth = 0;
        int32 PendingEvents = 0;
        bool bTerminal = false;
        bool bConsumed = false;
    };
    struct FContinuation
    {
        FGuid OwnerInvocation;
        int32 Pending = 0;
        TFunction<void()> Resume;
        bool bReady = false;
    };
    /** 只作本根内的弱定位；帧/宿主的强所有权始终归实际策略，最多256项。 */
    struct FRuntimeResourceSlot
    {
        TWeakObjectPtr<const UAbilityExecutionParameters> Frame;
        FName Slot;
        TWeakObjectPtr<UObject> Resource;
    };
    /** 接触边界推迟的逻辑入口；预留额度与组占用随根取消一起清理，不取得目标的新生命。 */
    struct FDeferredLogicalInvocation
    {
        FGuid NodeId;
        int32 Depth = 0;
        FAbilityNodeInput Input;
        FGuid ContinuationGroup;
        FGuid OriginalParent;
        int32 FiniteParentDepth = 0;
    };
    bool InvokeNode(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input, bool bReserved = false,
                    FGuid ContinuationGroup = FGuid(), FGuid OriginalParent = FGuid(), int32 FiniteParentDepth = 0);
    /** 只有内部 PublishWithContinuation 可覆盖组；外部事件继承自身调用的已有组。 */
    bool EnqueueGroupedEvent(const FAbilityGraphEvent& Event, FGuid Group, bool bOverrideGroup);
    /** 保留已接受入口的额度、原生命输入和组占用；不提前调用 Scope 或创建真实投递。 */
    bool DeferLogicalInvocation(const FGuid& NodeId, int32 Depth, const FAbilityNodeInput& Input, bool bAlreadyReserved,
                                FGuid ContinuationGroup, FGuid OriginalParent, int32 FiniteParentDepth);
    /** 在同一有界 drainer 中将预留转为真实调用，回调/GC 期间局部保活，最后归还组占用。 */
    void RunDeferredLogicalInvocation();
    void CompleteContinuationUnit(FGuid Group);
    void RunReadyContinuation();
    /** 在后继创建完毕后释放组计数，不能在弹出终态事件时过早触发父回调。 */
    void RetireConsumedInvocation(const FGuid& Id);
    bool ResolveSource();
    bool ValidateInput(const FAbilityNodeInput& Input, EAbilityPortPayload Required, FString& Error) const;
    void DrainEvents();
    void Finish(EAbilityGraphEndReason Reason);
    void ReleaseInvocation(const FGuid& Id);
    const FAbilityNodeDescriptor* DescriptorFor(const FInvocation& Invocation) const;
    /** 普通图使用原额度；只有验证通过的原型兼容根才使用独立辅助并发额度。 */
    int32 InvocationCapacity() const;
    int32 ActiveCapacity() const;
    int32 FrameEventCapacity() const;
    /** helper使用自身物理队列硬上限；原配置的小事件额度由原事件账本单独解释。 */
    int32 QueueCapacity() const;
    /** 原事件可跨帧排队；同步本体推进中仅选helper，不把旧图业务子调用提前到本体内部。 */
    int32 NextEligibleEventIndex() const;
    bool RetiresConsumedInvocations() const;
    /** 仅执行Validate通过的服务器快照；来源证明不是客户端业务Input中的授权字段。 */
    bool HasOriginalProjection() const;
    /** 识别编译器证明的原节点入口；其他查询、桥接和表现不消耗原累计调用。 */
    bool IsOriginalEntry(const FAbilityGraphNode& Node) const;
    /** 原终态队列和helper的释放独立，不能在Scope.Ready或资源Acquire结束时提前归还。 */
    void ReleaseOriginalInvocation(FGuid OriginalCall);

    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphDefinition> Snapshot;
    UPROPERTY(Transient)
    TMap<FGuid, TObjectPtr<UAbilityNodeExecution>> Executions;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UClass>> RegisteredClasses;
    UPROPERTY(Transient)
    TArray<FAbilityQueuedReferences> QueueReferences;
    UPROPERTY(Transient)
    TArray<FAbilityQueuedReferences> DeferredReferences;
    FAbilityNodeRegistry Registry;
    FAbilityExecutionOptions Options;
    FAbilityNodeInput RootInput;
    FGuid ExecutionId;
    TMap<FGuid, FInvocation> Invocations;
    TArray<FAbilityGraphEvent> Queue;
    /** 每条事件与引用数组并行；组身份不来自外部输入或客户端载荷。 */
    TArray<FGuid> QueueGroups;
    /** 优先标志只由节点 Start/继续函数的实际发布边界生成，外部事件不能指定它。 */
    TArray<bool> QueueScopedFlow;
    TArray<FDeferredLogicalInvocation> DeferredLogicalInvocations;
    TMap<FGuid, FContinuation> Continuations;
    TArray<FGuid> ReadyContinuations;
    TArray<FRuntimeResourceSlot> RuntimeResources;
    TMap<FPureMassCombatTarget, FAbilityTargetToken> OriginalTargetLives;
    uint64 EventCount = 0;
    int32 ActiveCount = 0;
    int32 ReservedInvocations = 0;
    int32 ReservedActive = 0;
    /** 旧LegacyStreaming不回收宏调用记录；细图辅助metadata回收不能退还这份独立累计准入。 */
    int32 PrototypeLogicalInvocations = 0;
    int32 ReservedPrototypeLogicalInvocations = 0;
    /** 均为服务器本次执行私有值，不复制；Finish清空，不随helper回收退还累计准入。 */
    TMap<FGuid, FOriginalInvocation> OriginalInvocations;
    int32 OriginalActiveCount = 0;
    int32 ReservedOriginalInvocations = 0;
    uint64 OriginalEventCount = 0;
    int32 OriginalQueuedEvents = 0;
    /** 与LastDrainFrame/GFrameCounter同步，重复Flush或Advance(0)不能取得新帧额度。 */
    int32 OriginalFrameEventsProcessed = 0;
    /** 全部新增片段聚合的普通有限域；只由节点编译标记和私有继承解释，不从Input接收。 */
    int32 FiniteInvocationCount = 0;
    int32 FiniteActiveCount = 0;
    int32 FiniteReservedInvocations = 0;
    uint64 FiniteEventCount = 0;
    int32 FiniteQueuedEvents = 0;
    int32 FiniteFrameEventsProcessed = 0;
    /** 物理累计不随租约归还重置；Stop/Finish只清本次根的全部资源身份。 */
    int32 FinitePhysicalStarts = 0;
    int32 FiniteActivePhysical = 0;
    TSet<FGuid> FinitePhysicalLeases;
    int32 PhysicalSpawnCount = 0;
    int32 ActivePhysicalSpawnCount = 0;
    uint64 PhysicalSpawnCountTotal = 0;
    TMap<FGuid, int32> PhysicalLeases;
    float InvocationStartTokens = 0.f;
    float PhysicalStartTokens = 0.f;
    bool bStarted = false;
    bool bFinished = true;
    bool bAdvancing = false;
    bool bDrainingEvents = false;
    bool bImmediateDrainRequested = false;
    bool bImmediatePayloadDrain = false;
    bool bAdvancingNode = false;
    uint64 LastDrainFrame = MAX_uint64;
    int32 FrameEventsProcessed = 0;
    /** 兼容片段Start产生的流程按发布顺序优先于已有sibling，仍由同一迭代队列执行。 */
    bool bInsertScopedEvents = false;
    int32 ScopedEventInsertCursor = 0;
    EAbilityGraphEndReason EndReason = EAbilityGraphEndReason::Failed;
};
