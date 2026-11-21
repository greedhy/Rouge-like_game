// 调用链：GAS -> 第二次按键/鼠标位置 -> Notify -> ShardPoints/Cue/Query/批伤害 -> 费用/冷却与资源收尾。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "MyArcaneShardAbility.generated.h"
class APointCollection;
class UAnimMontage;
class UAbilityGraphInstance;
class UAbilityArcaneShardContext;
class UAbilityArcaneShardNodeConfig;
enum class EAbilityGraphEndReason : uint8;

/** 原蓝图仍保留身份、费用GE、伤害曲线及图供审查；本原生激活入口不重复调用旧事件图。 */
UCLASS()
class TEST_25D_API UMyArcaneShardAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UMyArcaneShardAbility();
    /** 导出碎片顺序/范围配置；选点、Notify 和分阶段费用继续由原前端管理。 */
    virtual UAbilityGraphDefinition* BuildRuntimeBaseGraph(UObject* Outer, FString& Error) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Shards")
    TSubclassOf<APointCollection> ShardPointCollectionClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Shards")
    TObjectPtr<UAnimMontage> ShardMontage;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Shards")
    float ShardInterval = .2f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Shards")
    int32 MaxShardPoints = 11;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;

protected:
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

private:
    UFUNCTION()
    void OnConfirmPress(float TimeWaited);
    UFUNCTION()
    void OnShardTargetData(const FGameplayAbilityTargetDataHandle& Data);
    UFUNCTION()
    void OnShardNotify(FGameplayEventData Payload);
    UFUNCTION()
    void OnMontageInterrupted();
    UFUNCTION()
    void OnMontageFinished();
    /** 清理仅归属本施法的魔法圈和点集合，不撤销已施加的目标状态。 */
    void HideCircle();
    /** 先保留同步完成结果，待原“首片之后费用”阶段执行后再提交尾段冷却/结束。 */
    void OnShardGraphFinished(EAbilityGraphEndReason Reason);
    UAbilityArcaneShardNodeConfig* MakeShardConfig(UObject* Outer) const;
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardContext> PreparedPoints;
    UPROPERTY()
    TObjectPtr<UAbilityGraphInstance> ShardGraph;
    FAbilityNodeInput ShardInput;
    FDelegateHandle GraphFinishedHandle;
    bool bConfirmedTarget = false;
    bool bShardStarted = false;
    bool bShardCostStageFinished = false;
    TOptional<EAbilityGraphEndReason> PendingShardEnd;
};
