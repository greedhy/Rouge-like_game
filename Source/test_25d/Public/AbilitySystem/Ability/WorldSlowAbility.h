// 调用链：独立GAS激活 -> 一次Commit -> Time.WorldSlow注册图 -> 世界时间服务 -> 结束释放。
#pragma once
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "WorldSlowAbility.generated.h"

class UAbilityTask_RunGraph;
struct FWorldSlowActivationState;
/** 独立GA入口的冷却；直接服务调用不强制携带这个GAS费用策略。 */
UCLASS()
class TEST_25D_API UWorldSlowCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UWorldSlowCooldownEffect();
};

UCLASS()
class TEST_25D_API UWorldSlowAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    UWorldSlowAbility();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World Slow")
    FAbilityWorldSlowSettings Settings;
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* SourceTags = nullptr,
                                    const FGameplayTagContainer* TargetTags = nullptr,
                                    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation,
                                 const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) const override;

private:
    void RollbackStartupCooldown(const TSharedPtr<FWorldSlowActivationState>& State) const;
    TSharedPtr<FWorldSlowActivationState> ActivationState;
    UFUNCTION()
    void GraphFinished(bool bCancelled);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityTask_RunGraph> Task;
};
