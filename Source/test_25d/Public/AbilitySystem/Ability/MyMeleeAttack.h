// 调用链：GAS/敌人行为树 -> 原生 Activate -> 角色随机攻击蒙太奇 -> Notify -> Melee.Contact 图策略 -> End。

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "MyMeleeAttack.generated.h"

class UAbilityGraphInstance;
/** 旧 GA_MeleeAttack 的原生前端；资产保留身份/伤害曲线，实际接触动作可被其它图复用。 */
UCLASS()
class TEST_25D_API UMyMeleeAttack : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    UPROPERTY(EditDefaultsOnly, Category = "Melee")
    float ContactRadius = 45.f;

private:
    /** 每次激活只接受一个命中事件；任务取消由 GAS 随 GA 清理。 */
    UFUNCTION()
    void OnContact(FGameplayEventData Payload);
    UFUNCTION()
    void OnAnimationEnded();
    UFUNCTION()
    void OnAnimationInterrupted();
    UPROPERTY()
    FTaggedMontage SelectedMontage;
    UPROPERTY()
    TObjectPtr<UAbilityGraphInstance> ContactGraph;
    FAbilitySourceToken ContactSource;
    bool bContactConsumed = false;
};
