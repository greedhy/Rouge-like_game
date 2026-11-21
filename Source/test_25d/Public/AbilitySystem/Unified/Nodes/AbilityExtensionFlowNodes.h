// 调用链：Extension Scope 的不可变帧 -> 投射/逐跳/地表动作 -> 同图 payload -> 精确资源清理。
// 动作节点不施加伤害，也不激活旧GA；ContactEnd只确认本次本体payload，不等待独立OnHit寿命。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityExtensionFlowNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;
class UPrototypeCastContext;
class UExtensionNode;
class ATerrainSurface;

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityExtensionProjectileNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityExtensionSequenceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityExtensionSurfaceNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityExtensionContactEndNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 仅声明本体接触屏障；准确所有者存入同根同帧弱资源槽，不依赖完整后继子树结束。 */
UCLASS(Abstract, Transient)
class TEST_25D_API UAbilityExtensionContactOwner : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual void CompleteContact() PURE_VIRTUAL(UAbilityExtensionContactOwner::CompleteContact, );
};

UCLASS(Transient)
class TEST_25D_API UAbilityExtensionProjectileNodeExecution : public UAbilityExtensionContactOwner
{
    GENERATED_BODY()
public:
    /** 验证原Projectile帧，取得准确物理租约与纯表现宿主；失败由图统一Stop回收半初始化资源。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 按原Duration尾帧和Range推进扫掠，只发布原生命目标集，不执行伤害。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 解绑自己的弱槽并归还准确物理租约；不回收其它片段或新施法的资源。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 本体payload回执后关闭该单弹；独立OnHit孩子继续归同图管理。 */
    virtual void CompleteContact() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityPrototypePresentationFrame Visual;
    FGuid PhysicalLease;
    FVector Position = FVector::ZeroVector;
    float Age = 0.f;
    float Travel = 0.f;
    bool bAwaitingContact = false;
    bool bCompleting = false;
    /** 正常终态先关闭表现，再排原到期/完成分支；重复调用不重播结束。 */
    void CompleteFlight();
};

UCLASS(Transient)
class TEST_25D_API UAbilityExtensionSequenceNodeExecution : public UAbilityExtensionContactOwner
{
    GENERATED_BODY()
public:
    /** 捕获原目标/起点并立即发布第一跳，保持Extension与Actor/Mixed不同的启动合同。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 只累计原16次Clamp节拍；实际下一跳需等当前本体结算回执。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 关闭自己的连线/接触槽；取消不会补发正常到期孩子。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 保存命中前历史位置，再按严格半径、同距首个选择下一原生命。 */
    virtual void CompleteContact() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityPrototypePresentationFrame Visual;
    TOptional<FAbilityTargetToken> Current;
    TSet<FPureMassCombatTarget> Visited;
    FVector LastPosition = FVector::ZeroVector;
    FVector PendingPosition = FVector::ZeroVector;
    float Age = 0.f;
    float Clock = 0.f;
    int32 Hits = 0;
    int32 StepsInAdvance = 0;
    bool bAwaitingContact = false;
    bool bCompleting = false;
    bool bAdvanceExpired = false;
    /** 只确认本跳位置/可见性及倍率，payload由Contact边决定。 */
    void Contact();
    /** 在同一drainer回执中继续剩余节拍，不递归推进图或刷新预算。 */
    void PumpCadence();
    /** 到期原点使用最后命中位置，EventCenter保留原宿主中心。 */
    void CompleteSequence();
};

/** 真实地表Actor继续拥有接触/状态定时器；本节点只持有其准确实例与原外层寿命。 */
UCLASS(Transient)
class TEST_25D_API UAbilityExtensionSurfaceNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 使用共享构造服务创建配置中的真实地表；其接触/状态时钟继续由原Actor持有。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 观察外层原寿命，到期销毁准确地表并排独立到期孩子。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 取消或正常结束仅销毁本实例，不批量查找/销毁来源的其它地表。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    UPROPERTY(Transient)
    TObjectPtr<ATerrainSurface> Surface;
    float Age = 0.f;
    bool bCompleting = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityExtensionContactEndNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 通过同根/同帧弱槽确认准确接触所有者；只结束本体，不等待整个子图寿命。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityExtensionFlowNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
    /** 保留原Surface入口顺序：地面投射/八个地表额度在逻辑Spawn额度之前消耗。 */
    TEST_25D_API bool PrepareSurfaceEntry(UPrototypeCastContext& Context, const UExtensionNode& Node,
                                          FVector& Position);
    /** 与旧驱动共用真实地表构造，不复制地表接触或状态算法。 */
    TEST_25D_API ATerrainSurface* SpawnSurface(AActor& Caster, const UExtensionNode& Node, FVector Position);
} // namespace AbilityExtensionFlowNodes
