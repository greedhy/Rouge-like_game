/**
 * 分叉连锁链：GA ActivateAbility -> 鼠标目标任务 ReceiveTarget -> Commit -> Delivery.ForkedChain图策略的广度优先 Queue/Visited -> Actor GE 或 Mass 伤害 -> Presentation Actor。不是 Extension 子图递归。预算限制总目标/深度/兄弟数；结果段复制给客户端，表现 Actor 1.5 秒后销毁，本地组件重建时先回收旧组件。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "Batch6ChainAbility.generated.h"

class UNiagaraSystem;
class UStaticMesh;
class UActorComponent;
USTRUCT(BlueprintType)
struct FBatch6ChainSegment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FVector Start = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FVector End = FVector::ZeroVector;
};

UCLASS()
class TEST_25D_API ABatch6ChainPresentation : public AActor
{
    GENERATED_BODY()
public:
    /** 构造始终相关的复制 Actor 并加载圆柱兜底网格，避免 Niagara 参数不兼容时无连线。 */
    ABatch6ChainPresentation();
    /** 生成/复制后本地 Present；服务器设置 1.5 秒寿命，表现不延长 GA 生命周期。 */
    virtual void BeginPlay() override;
    /** 复制 Segments 与 BeamSystem，二者的 RepNotify 均可触发 Present 重建。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(ReplicatedUsing = Present, BlueprintReadOnly)
    TArray<FBatch6ChainSegment> Segments;
    UPROPERTY(ReplicatedUsing = Present)
    TObjectPtr<UNiagaraSystem> BeamSystem;
    UFUNCTION()
    /** 专用服务器跳过；销毁上次动态组件，按段生成无碰撞圆柱和 Niagara 并写入端点，不造成伤害。 */
    void Present();

private:
    UPROPERTY()
    TObjectPtr<UStaticMesh> FallbackMesh;
    UPROPERTY()
    TArray<TObjectPtr<UActorComponent>> PresentationComponents;
};

/** Shared breadth-first cast budget. Depth zero is the initial target. */
struct FBatch6ChainBudget
{
    /** 广度优先队列入队前检查总数、深度与分支上限，并将策划值夹到安全范围。 */
    static bool Allows(int32 Used, int32 Depth, int32 Siblings, int32 Total, int32 MaxDepth, int32 Branches)
    {
        return Used < FMath::Clamp(Total, 1, 128) && Depth <= FMath::Clamp(MaxDepth, 0, 16) &&
               Siblings < FMath::Clamp(Branches, 1, 8);
    }
};

UCLASS()
class TEST_25D_API UBatch6ChainAbility : public UPrototypeAbility
{
    GENERATED_BODY()
public:
    /** 构造每 Actor 实例化、本地预测输入 GA，默认关闭基础 Debuff/击退概率。 */
    UBatch6ChainAbility();
    /** UI 生成当前等级伤害、总目标、深度、分叉数和费用说明。 */
    virtual FString GetDescription(int32 Level) override;
    /** 服务器 Commit 回调，使用 UPureMassAbility 的原生扣蓝 GE。 */
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    int32 MaxTargets = 12;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    int32 MaxDepth = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    int32 BranchesPerTarget = 2;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    float JumpRadius = 400;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    float CastRange = 1200;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    TObjectPtr<UNiagaraSystem> BeamSystem;
    /** 清目标去重标记，创建鼠标目标任务并设置三秒超时；实际攻击在服务器 ReceiveTarget 中完成。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    /** 无论成功取消都清除本技能目标超时，再交父类清理任务。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;

private:
    /** 本次异步选点固定来源，结束时取消同步图，防止伤害回调继续分叉。 */
    FAbilitySourceToken ActivationSource;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphInstance> Batch6Execution;
    bool bResolved = false;
    FTimerHandle Timeout;
    UFUNCTION()
    /** 服务器消费一次目标请求，查询瞄准点附近首目标并校验距离遮挡；Commit 后广度优先结算、生成复制连线宿主并结束。 */
    void ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data);
    /** 目标无效或超时统一取消入口，仅结束仍激活的技能。 */
    void Abort();
};
