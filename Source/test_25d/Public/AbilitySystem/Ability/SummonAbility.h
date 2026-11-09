// 调用链：GAS原生激活 -> 地面预警/蒙太奇 -> 一次Notify -> Spawn.Summon图 -> 既有Pawn/AI/计数。

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "SummonAbility.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class UNiagaraComponent;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UAbilityTask_RunGraph;
class UAbilitySummonNodeConfig;

UCLASS()
class TEST_25D_API USummonAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    USummonAbility();
    /** 原生替代已审计的34节点蓝图入口；保留原类/参数/标签，不再同时执行K2激活图。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
    UFUNCTION(BlueprintCallable)
    TArray<FVector> GetSpawnLocations();

    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    int32 NumMinions = 5; // 召唤的数量

    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    TArray<TSubclassOf<APawn>> MinionClasses; //召唤生成的角色类

    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    float MinSpawnDistance = 50.f; //召唤物距离召唤师最近的距离

    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    float MaxSpawnDistance = 250.f; //召唤物距离召唤师最远的距离

    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    float SpawnSpread = 90.f; //召唤物在召唤师前面的角度范围

    UFUNCTION(BlueprintPure, Category = "Summoning")
    TSubclassOf<APawn> GetRandomMinionClass(); //获取随机的召唤物类

    /** 既有蓝图和新原生前端均可复用同一已授权生成动作，不在这里提交费用。 */
    UFUNCTION(BlueprintCallable, Category = "Summoning")
    APawn* SpawnSummonedPawn(TSubclassOf<APawn> Class, const FTransform& SpawnTransform);
    UPROPERTY(EditDefaultsOnly, Category = "Summoning|Presentation")
    TObjectPtr<UAnimMontage> SummonMontage;
    UPROPERTY(EditDefaultsOnly, Category = "Summoning|Presentation")
    TObjectPtr<UNiagaraSystem> SummonGroundEffect;
    UPROPERTY(EditDefaultsOnly, Category = "Summoning|Presentation")
    FGameplayTag SummonEventTag;
    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    float SummonSpawnInterval = .2f;
    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    float SummonSpawnHeight = 60.f;
    /** 历史GA没有Commit，默认保留；新统一图入口仍统一提交一次。 */
    UPROPERTY(EditDefaultsOnly, Category = "Summoning")
    bool bCommitOnActivation = false;

protected:
    UFUNCTION()
    void OnSummonNotify(FGameplayEventData Payload);
    UFUNCTION()
    void OnSummonMontageEnded();
    UFUNCTION()
    void OnSummonMontageCancelled();
    UFUNCTION()
    void OnSummonGraphFinished(bool bCancelled);
    FAbilitySourceToken SummonSource;
    UPROPERTY(Transient)
    TObjectPtr<UAbilitySummonNodeConfig> SummonSnapshot;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_PlayMontageAndWait> SummonMontageTask;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_WaitGameplayEvent> SummonEventTask;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_RunGraph> SummonGraphTask;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraComponent>> SummonWarnings;
    bool bSummonNotifyConsumed = false;
    bool bSummonEnding = false;
};
