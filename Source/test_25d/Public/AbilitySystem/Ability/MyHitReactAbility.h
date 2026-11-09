// 调用链：原受击Tag激活 -> 共享所属GE -> CombatInterface蒙太奇 -> 任一结束/取消回收同一GE句柄。
#pragma once
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayEffectNodes.h"
#include "MyHitReactAbility.generated.h"

UCLASS()
class TEST_25D_API UMyHitReactAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    UMyHitReactAbility();
    UPROPERTY(EditDefaultsOnly, Category = "Hit React")
    TSubclassOf<UGameplayEffect> HitReactEffectClass;
    UPROPERTY(EditDefaultsOnly, Category = "Hit React")
    float HitReactEffectLevel = 1.f;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;

private:
    UFUNCTION()
    void ReactFinished();
    FAbilityGameplayEffectApplication OwnedReact;
    bool bEndingReact = false;
};
