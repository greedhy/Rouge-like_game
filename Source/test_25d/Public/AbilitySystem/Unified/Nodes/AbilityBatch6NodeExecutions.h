// 调用链：Batch6 GA或任意Schema2图 -> 注册策略 -> 原伤害/石化服务 -> 类型化完成端口。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityBatch6NodeExecutions.generated.h"
class UNiagaraSystem;
class UMaterialInterface;
class FAbilityNodeRegistry;
struct FBatch6ChainSegment;
struct FAbilityDamageResult;

/** 原整体策略和细节点共用的实际动作；不拥有计时、队列或另一个GA。 */
namespace AbilityLightningActions
{
    TEST_25D_API TArray<FAbilityTargetToken> FindTargets(const FAbilitySourceToken& Source, FVector Center,
                                                         FVector From, float Radius,
                                                         const TSet<FPureMassCombatTarget>& Visited, int32 Limit);
    /** 保留原闪电关闭Debuff/径向参数和预设forces的合同；不走投射物Prepare或额外RNG。 */
    TEST_25D_API FAbilityDamageResult ApplyHit(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                               const FDamageEffectParams& Params, float Scale);
    TEST_25D_API FAbilityNodeInput DamagePayload(const FAbilityNodeInput& Input, const FAbilityTargetToken& Target,
                                                 FVector From, FVector Position, const FAbilityDamageResult& Result);
    TEST_25D_API void Present(AActor& Caster, FVector Origin, UNiagaraSystem* Beam,
                              TArray<FBatch6ChainSegment>&& Segments, float Lifetime = 1.5f);
} // namespace AbilityLightningActions

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityForkedChainNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxDepth = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 BranchesPerTarget = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpRadius = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> BeamSystem;
    /** 保留旧整数/跳跃距离夹取规则；拒绝非有限的伤害缩放。 */
    virtual bool Validate(FString& Error) const override;
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityAreaPetrifyNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UMaterialInterface> StoneMaterial;
    /** 参数必须有限；目标数量沿用旧范围查询，不引入每目标图调用上限。 */
    virtual bool Validate(FString& Error) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityForkedChainNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 每调用独占BFS队列和去重集合，不激活完整GA，命中前重新验证生命身份。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};

/** 两种闪电共享的伤害、范围与表现配置。 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityLightningNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpRadius = 400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> BeamSystem;

    virtual bool Validate(FString& Error) const override;
};

/** 一次选取主目标附近的敌人，由施法原点同时发出所有光束。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityForkedVolleyNodeConfig : public UAbilityLightningNodeConfig
{
    GENERATED_BODY()
};

/** 每跳只选择一个新目标，延迟以图的游戏时间推进。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilitySequentialChainNodeConfig : public UAbilityLightningNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float JumpDelay = 0.1f;

    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityLightningNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 首击同步发生；叉状先捕获全部目标，逐跳只保留最近一次命中位置。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    /** 时间到达时最多推进一跳，丢弃多出的帧时间。 */
    virtual void Advance(float DeltaSeconds) override;

private:
    TArray<FAbilityTargetToken> FindTargets(AActor& Caster, FVector Center, FVector From, int32 Limit) const;
    bool Hit(const FAbilityTargetToken& Target, FVector From, float Scale, TArray<FBatch6ChainSegment>& Segments);

    UPROPERTY(Transient)
    TObjectPtr<UAbilityLightningNodeConfig> Settings;
    TSet<FPureMassCombatTarget> Visited;
    FVector LastImpact = FVector::ZeroVector;
    float TimeUntilJump = 0.f;
};

UCLASS(Transient)
class TEST_25D_API UAbilityAreaPetrifyNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 原Gather/Visible/Enemy规则统一复用Status服务；状态由目标组件继续持有。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
namespace AbilityBatch6Nodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
