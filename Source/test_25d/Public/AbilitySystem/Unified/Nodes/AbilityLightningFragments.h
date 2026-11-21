// 调用链：第六批闪电前端/运行时默认导出 -> 步进、查询、载荷与光束节点 -> 原共享伤害/复制表现。
// 本入口只构造本次内存定义；策略和帧属于同一个GraphInstance，不生成另一完整GA。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilityLightningFragments.generated.h"

class UAbilityGraphDefinition;
class UAbilityLightningNodeConfig;
class FAbilityNodeRegistry;

/** 有限传导宿主只拥有次数、节拍和访问历史；查询/数值/表现各有独立配置。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLightningControlConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bSequential = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpDelay = .1f;
    virtual bool Validate(FString& Error) const override;
    /** 显式有限128目标宿主复用持续退役/速率额度，不能凭字符串豁免普通有界图。 */
    virtual bool SupportsSustainedRoot(FString& Error) const override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLightningQueryConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpRadius = 400.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLightningDamageConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageMultiplier = 1.f;
    virtual bool Validate(FString& Error) const override;
    virtual bool TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier) override;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLightningCueConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> BeamSystem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Lifetime = 1.5f;
    virtual bool Validate(FString& Error) const override;
};

struct FAbilityLightningSelectedTarget
{
    FAbilityTargetToken Target;
    FVector Position = FVector::ZeroVector;
};

/** 同根只读步参数；弱生命不阻止对象池回收，宿主通过精确资源索引接受本步一次查询。 */
UCLASS(Transient)
class TEST_25D_API UAbilityLightningStepFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
private:
    friend class UAbilityLightningStepsExecution;
    friend class UAbilityLightningTargetsExecution;
    friend class UAbilityLightningDamageExecution;
    friend class UAbilityLightningCueExecution;
    FAbilityTargetToken First;
    TSet<FPureMassCombatTarget> Visited;
    FVector From = FVector::ZeroVector;
    uint64 Sequence = 0;
    int32 MaxTargets = 1;
    bool bSequential = false;
};

/** 查询先冻结全部原生命；载荷另建结果帧保留实际结算前的位置，致死后仍可绘制接触线段。 */
UCLASS(Transient)
class TEST_25D_API UAbilityLightningTargetsFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    const UAbilityLightningStepFrame* GetStep() const;
    /** 原查询捕获的权限集合；结果帧保留它，公开TargetSet只允许选择/重排其中的原生命。 */
    const TArray<FAbilityLightningSelectedTarget>& GetCapturedTargets() const;

private:
    friend class UAbilityLightningTargetsExecution;
    friend class UAbilityLightningDamageExecution;
    friend class UAbilityLightningCueExecution;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityLightningStepFrame> Step;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityLightningTargetsFrame> CapturedFrame;
    TArray<FAbilityLightningSelectedTarget> Targets;
    bool bDamageReceipt = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLightningStepsExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 首步同步发布；后继仍由同一图的迭代drainer执行，不递归激活GA。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 每帧最多一步，尾部重新等待完整间隔，不消费长帧剩余时间。 */
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 查询唯一回执固定本步访问历史；后续载荷替换不改变传导算法。 */
    bool AcceptSelection(const UAbilityLightningStepFrame& Frame,
                         const TArray<FAbilityLightningSelectedTarget>& Targets);
    /** 只积累原始结果；等本体Cue完成后从宿主发布，独立OnHit不会拖延下一跳。 */
    void RecordDamage(const UAbilityLightningStepFrame& Frame, FAbilityNodeInput Payload);

private:
    /** 建立一个原根只读步骤并通过已有continuation/drainer推进本体，不新建子执行器。 */
    bool EmitStep(FString& Error);
    /** 本体结束才发布历史结果、释放精确帧索引，然后正常结束或重置下一完整等待。 */
    void StepFinished();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityLightningControlConfig> Settings;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityLightningStepFrame> CurrentFrame;
    TSet<FPureMassCombatTarget> Visited;
    TArray<FAbilityNodeInput> PendingResults;
    FVector LastImpact = FVector::ZeroVector;
    uint64 Sequence = 0;
    float TimeUntilJump = 0.f;
    bool bWaiting = false;
    bool bSelectionAccepted = false;
    bool bEmptySelection = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLightningTargetsExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** Volley在任何GE前捕获完整邻域；Sequence每步只捕获最近可见的未访问原生命。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLightningDamageExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 批目标逐生命重验；主伤害只在局部消费累计倍率，原参数/预设力不写回、不额外掷随机数。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLightningCueExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 默认使用载荷保留的历史接触点；替换状态载荷则只显示仍有效的选中生命，不重新查询邻域。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

namespace AbilityLightningFragments
{
    /** 从旧真实Volley/Sequential参数构造独立行为轴；不启用未被前端使用的BFS。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const UAbilityLightningNodeConfig& Settings);
    /** 只登记细片段；旧完整策略继续作为兼容/对照入口。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityLightningFragments
