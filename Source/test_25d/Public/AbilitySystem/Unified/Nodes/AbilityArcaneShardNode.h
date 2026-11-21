// 调用链：ArcaneShards GA 通知/任意 Schema2 图 -> 本策略 -> 原点集合、Cue、Actor/Mass 伤害服务。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "TimerManager.h"
#include "AbilityArcaneShardNode.generated.h"
class APointCollection;
class FAbilityNodeRegistry;
struct FAbilityDamageResult;

/** 单个地面点一次查询捕获的原生命；旧友方Actor身份只交给碎片专用GE入口。 */
struct TEST_25D_API FAbilityArcaneShardHit
{
    FAbilityTargetToken Token;
    TOptional<FAbilitySourceToken> LegacyActorLife;
};

/** GA 在动画前冻结地面点；独立图也可由策略自行生成。所有权只属于本次施法。 */
UCLASS(Transient)
class TEST_25D_API UAbilityArcaneShardContext : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY()
    TArray<FVector> Points;
    UPROPERTY()
    TObjectPtr<APointCollection> PointCollection;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityArcaneShardNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<APointCollection> PointCollectionClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxPoints = 11;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Interval = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InitialStartDelay = -.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TailDelay = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag CueTag;
    /** 原生碎片提供身份，旧图默认空。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag EquipmentAbilityTag;
    /** 原蓝图仅查询 Actor；Mass 接入必须明确启用，不冒充旧行为的等价迁移。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = false;
    /** 原蓝图未做阵营过滤；仅新配置可显式启用敌对筛选。来源、ASC、生命和房间权限始终校验。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireHostileActorTargets = false;
    /** 独立组合图默认发布每个伤害结果；原 GA 仅等待片段/完成，不需要逐目标队列事件。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bPublishDamageResults = true;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityArcaneShardNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 复用原 PointCollection 11 个点与随机旋转/地面投影，失败清理本次生成的 Actor。 */
    static UAbilityArcaneShardContext* PreparePoints(UObject* Outer, const FAbilityNodeInput& Input,
                                                     const UAbilityArcaneShardNodeConfig& Config, FString& Error);

protected:
    /** 共用原时钟/清理；细Clock覆写此函数只发布Point，不执行旧宏的任何效果。 */
    virtual void EmitPoint(FVector Location, uint64 Sequence);
    const UAbilityArcaneShardNodeConfig* GetResolvedSettings() const
    {
        return Settings;
    }
    const FDamageEffectParams& GetResolvedDamage() const
    {
        return ResolvedDamage;
    }

private:
    /** 定时回调先核对来源生命，再发布本点事件和原伤害通路。 */
    void SpawnNext();
    void CompleteTail();
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardContext> Prepared;
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardNodeConfig> Settings;
    UPROPERTY()
    FDamageEffectParams ResolvedDamage;
    FTimerHandle SpawnTimer;
    FTimerHandle TailTimer;
    TWeakObjectPtr<UWorld> TimerWorld;
    int32 NextPoint = 0;
};
namespace AbilityArcaneShardNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
    /** 旧 Actor 命中规则专用：重验双方捕获生命后走原 GE，不把友方生命包装为 LiveTarget 能力。 */
    TEST_25D_API FAbilityDamageResult ApplyLegacyActor(const FAbilitySourceToken& Source,
                                                       const FAbilitySourceToken& TargetLife,
                                                       const FDamageEffectParams& Parameters);
    /** 保留原cue入口；调用者仍须在同步回调后复核取消与来源生命。 */
    TEST_25D_API void ExecuteCue(const FAbilitySourceToken& Source, FVector Location,
                                 const UAbilityArcaneShardNodeConfig& Settings);
    /** 在任何GE之前冻结本点所有Actor/Mass原生命；不会把LegacyFriendly授予通用目标权限。 */
    TEST_25D_API TArray<FAbilityArcaneShardHit> CaptureHits(const FAbilitySourceToken& Source, FVector Location,
                                                            const UAbilityArcaneShardNodeConfig& Settings);
    /** 失效生命在执行前跳过，不为已经复用的旧查询发布虚构伤害事件。 */
    TEST_25D_API bool ResolveHitLocation(const FAbilitySourceToken& Source, const FAbilityArcaneShardHit& Hit,
                                         FVector& Location);
    /** 原确定性15度冲量/径向原点与同一GE/Mass入口；累计倍率只作用于本次局部参数。 */
    TEST_25D_API FAbilityDamageResult ApplyHit(const FAbilitySourceToken& Source, const FAbilityArcaneShardHit& Hit,
                                               const FDamageEffectParams& Parameters, FVector Location,
                                               float DamageScale = 1.f, float DebuffDamageScale = 1.f);
    /** 原伤害结果快照不携带LiveTarget授权；旧宏与细批节点发布同一格式。 */
    TEST_25D_API FAbilityNodeInput DamagePayload(const FAbilityNodeInput& Input, FVector Location,
                                                 const FAbilityArcaneShardHit& Hit, const FAbilityDamageResult& Result,
                                                 uint64 Sequence);
} // namespace AbilityArcaneShardNodes
