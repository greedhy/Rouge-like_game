// 调用链：GAS激活/初始费用 -> 注册轨道策略；公开计数接口只读取本次策略的资源状态。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyProjectileSpell.h"
#include "MyOrbitingFireBoltAbility.generated.h"

class AMyOrbitingFireBolt;
class UOrbitingFireBoltConfig;
class UAbilityGraphInstance;
class UAbilityOrbitContext;

UCLASS()
class TEST_25D_API UMyOrbitingFireBoltAbility : public UMyProjectileSpell
{
    GENERATED_BODY()
public:
    UMyOrbitingFireBoltAbility();
    virtual bool IsInputToggleAbility() const override
    {
        return true;
    }
    virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                           FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                           const FGameplayAbilityActivationInfo ActivationInfo) const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "OrbitingFireBolt")
    TObjectPtr<UOrbitingFireBoltConfig> Config;
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayTagContainer* SourceTags = nullptr,
                                    const FGameplayTagContainer* TargetTags = nullptr,
                                    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    virtual FString GetDescription(int32 Level) override;
    virtual FString GetNextLevelDescription(int32 Level) override;
    UFUNCTION(BlueprintPure, Category = "OrbitingFireBolt")
    int32 GetLiveFireBoltCount() const;
    UFUNCTION(BlueprintPure, Category = "OrbitingFireBolt")
    int32 GetLaunchedFireBoltCount() const;
    UFUNCTION(BlueprintPure, Category = "OrbitingFireBolt")
    AActor* GetAttackTarget() const;

private:
    bool SpendMana(float Amount) const;
    UPROPERTY()
    TObjectPtr<UAbilityGraphInstance> OrbitGraph;
    UPROPERTY()
    TObjectPtr<UAbilityOrbitContext> OrbitContext;
    FDelegateHandle GraphFinishedHandle;
};
