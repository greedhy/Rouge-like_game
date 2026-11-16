/** 调用链：GA 选点与 Commit -> RunGraph Task -> 叉状或逐跳策略 -> 共享伤害与复制光束。
 *  源技能结束时 GAS 销毁任务并取消剩余跳跃，已出现的光束由其表现 Actor 到期清理。
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
    /** UI 显示当前等级伤害、目标上限与传导方式。 */
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
    /** 原技能默认使用叉状齐射，新技能资产开启逐跳模式。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    bool bSequentialChain = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    float JumpDelay = 0.1f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chain")
    TObjectPtr<UNiagaraSystem> BeamSystem;
    /** 清目标去重标记，创建鼠标目标任务并设置三秒超时；实际攻击在服务器 ReceiveTarget 中完成。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    /** 无论成功取消都清除本技能目标超时，再交父类清理任务。 */
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;

private:
    /** 本次异步选点固定来源，GAS 结束时销毁运行图的 AbilityTask。 */
    FAbilitySourceToken ActivationSource;
    bool bResolved = false;
    FTimerHandle Timeout;
    UFUNCTION()
    /** 服务器验证首目标和费用，然后由 AbilityTask 持有并推进所选图策略。 */
    void ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data);
    UFUNCTION()
    void GraphFinished(bool bCancelled);
    /** 目标无效或超时统一取消入口，仅结束仍激活的技能。 */
    void Abort();
};
