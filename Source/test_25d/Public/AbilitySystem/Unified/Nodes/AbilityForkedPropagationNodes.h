// 调用链：运行时配方替换投递 -> 有限BFS传播 -> 真实单次投递回执 -> 独立Hit载荷；不激活完整旧GA。
#pragma once

#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilityForkedPropagationNodes.generated.h"

/** 分支只添加BFS预算；真实弹体/原生准备帧合同继续复用已有投递配置。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityForkedPropagationNodeConfig : public UAbilityPropagationNodeConfig
{
    GENERATED_BODY()
public:
    UAbilityForkedPropagationNodeConfig();

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxDepth = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesPerTarget = 2;
    /** 旧资产缺省false保持FIFO；工作台创建新Forked时显式开启并发。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bConcurrentBranches = false;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;

    /** 继承有限单次接触投递约束；新分支不允许内部伤害与外部载荷同时拥有伤害。 */
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityForkedPropagationNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每次调用独占BFS队列；只有真实命中回执允许从历史命中点扩展新目标。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 同时推进独立投递；每个父命中独立计时，旧兼容模式额外保留全局FIFO间隔。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 先封回执入口，再解绑/取消投递及释放精确租约；不发布命中或正常到期后继。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    struct FPending
    {
        TOptional<FAbilityTargetToken> Target;
        FVector Origin = FVector::ZeroVector;
        FVector Direction = FVector::ForwardVector;
        int32 Depth = 0;
        double ReadyAt = 0.;
    };
    struct FFlight
    {
        FPending Step;
        TWeakObjectPtr<UAbilityDeliveryInstance> Delivery;
        FGuid Lease;
    };
    /** 到期作业共用原根预算；并发位不足保持排队，累计生成额度耗尽才终结。 */
    bool LaunchReady(FString& Error);
    /** 原生class/装备参数从同根冻结帧解析；每颗弹体分别登记身份与精确租约。 */
    bool Launch(const FPending& Step, FString& Error);
    /** 唯一终态回执；外部Hit载荷先同步消费，再从历史接触点选择下一层，不要求受击者仍活着。 */
    void Delivered(const FAbilityDeliveryResult& Result);
    /** 清空BFS后正常闭合一次；历史接触位置不能升级为新的活目标权限。 */
    void Complete();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityForkedPropagationNodeConfig> Settings;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAbilityDeliveryInstance>> ActiveDeliveries;
    TMap<FGuid, FFlight> Flights;
    TArray<FPending> Pending;
    /** 排除已入队原生命，保证两兄弟不会争抢同一候选；不能用作物理忽略集。 */
    TArray<FAbilityTargetToken> Reserved;
    /** 只排除已确认命中的原生命；当前尚未命中的预留目标仍必须能被真实弹体碰撞。 */
    TArray<FAbilityTargetToken> Confirmed;
    FVector LastImpact = FVector::ZeroVector;
    double Clock = 0.;
    double NextSerialLaunch = 0.;
    int32 DeliveriesStarted = 0;
};

namespace AbilityForkedPropagationNodes
{
    /** 登记独立传播策略；Hit只交付历史接触，伤害/状态由后继节点唯一负责。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityForkedPropagationNodes
