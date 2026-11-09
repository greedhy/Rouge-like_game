// 调用链：ArcaneShards GA 通知/任意 Schema2 图 -> 本策略 -> 原点集合、Cue、Actor/Mass 伤害服务。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "TimerManager.h"
#include "AbilityArcaneShardNode.generated.h"
class APointCollection;
class FAbilityNodeRegistry;
struct FAbilityDamageResult;

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

private:
    /** 定时回调先核对来源生命，再发布本点事件和原伤害通路。 */
    void SpawnNext();
    void CompleteTail();
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardContext> Prepared;
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardNodeConfig> Settings;
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
} // namespace AbilityArcaneShardNodes
