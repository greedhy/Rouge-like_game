// 调用链：固定Startup输入 -> GAS一次Commit -> 防御组件执行；组件完成/取消 -> EndAbility。
#pragma once
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "PerfectDodgeAbility.generated.h"

/** 专用原生冷却，无费用；持续时间仍使用GAS世界时间。 */
UCLASS()
class TEST_25D_API UPerfectDodgeCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UPerfectDodgeCooldownEffect();
};

UCLASS()
class TEST_25D_API UPerfectDodgeAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    UPerfectDodgeAbility();
    virtual bool IsIntrinsicAbility() const override
    {
        return true;
    }
    virtual bool IsPressOnlyAbility() const override
    {
        return true;
    }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge")
    FPerfectDodgeSettings DodgeSettings;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge")
    float CooldownSeconds = .80f;
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    const FGameplayTagContainer* SourceTags = nullptr,
                                    const FGameplayTagContainer* TargetTags = nullptr,
                                    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayTagContainer* Tags = nullptr) const override;
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo ActivationInfo) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* Info, const FGameplayAbilitySpec& Spec) override;

private:
    void DodgeFinished(FGuid Execution, bool bCancelled);
    UPROPERTY(Transient)
    TObjectPtr<UPerfectDodgeComponent> Defense;
    FGuid Execution;
    FDelegateHandle FinishedHandle;
};
