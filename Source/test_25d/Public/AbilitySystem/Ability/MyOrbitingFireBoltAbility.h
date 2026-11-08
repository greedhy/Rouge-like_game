#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyProjectileSpell.h"
#include "MyOrbitingFireBoltAbility.generated.h"

class AMyOrbitingFireBolt;
class UOrbitingFireBoltConfig;

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
    AActor* GetAttackTarget() const
    {
        return AttackTarget.Get();
    }

private:
    void Replenish();
    void PayUpkeep();
    bool SpendMana(float Amount) const;
    void LaunchNext();
    void HandleOffensiveAttack(UGameplayAbility* SourceAbility, AActor* Target);
    void HandleDeactivate(const FGameplayTag& AbilityTag);
    bool IsValidEnemy(AActor* Target) const;
    void ClearTarget();
    void ArrangeOrbit();
    UFUNCTION()
    void OnTargetDied(AActor* Target);
    UFUNCTION()
    void OnOwnerDied(AActor* OwnerActor);
    UFUNCTION()
    void OnOwnerDestroyed(AActor* OwnerActor);
    UPROPERTY()
    TArray<TObjectPtr<AMyOrbitingFireBolt>> FireBolts;
    TWeakObjectPtr<AActor> AttackTarget;
    FTimerHandle SpawnTimer;
    FTimerHandle ManaTimer;
    FTimerHandle LaunchTimer;
    FDelegateHandle AttackDelegateHandle;
    FDelegateHandle DeactivateDelegateHandle;
};
