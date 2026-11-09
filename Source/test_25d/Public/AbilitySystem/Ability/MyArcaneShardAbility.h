// 调用链：GAS -> 第二次按键/鼠标位置 -> 蒙太奇 Notify -> Shape.ArcaneShards -> 费用/冷却及资源收尾。
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

/** 原蓝图仍保留身份、费用GE、伤害曲线及图供审查；本原生激活入口不重复调用旧事件图。 */
UCLASS()
class TEST_25D_API UMyArcaneShardAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UMyArcaneShardAbility();
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
    UAbilityArcaneShardNodeConfig* MakeShardConfig(UObject* Outer) const;
    UPROPERTY()
    TObjectPtr<UAbilityArcaneShardContext> PreparedPoints;
    UPROPERTY()
    TObjectPtr<UAbilityGraphInstance> ShardGraph;
    FAbilityNodeInput ShardInput;
    FDelegateHandle GraphFinishedHandle;
    bool bConfirmedTarget = false;
    bool bShardStarted = false;
};
