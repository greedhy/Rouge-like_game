#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "MyArcaneNova.generated.h"

/** Instant, server-authoritative pulse. The GA Blueprint supplies tags, GEs and balance values. */
UCLASS()
class TEST_25D_API UMyArcaneNova : public UMyDamageGameplayAbility
{
    GENERATED_BODY()

public:
    UMyArcaneNova();
    virtual FString GetDescription(int32 Level) override;
    virtual FString GetNextLevelDescription(int32 Level) override;

    UFUNCTION(BlueprintPure, Category = "Arcane Nova")
    float GetNovaDamage(int32 Level) const;

    /** Actor-centre distance is the final range check; overlap results are deduplicated. */
    UFUNCTION(BlueprintCallable, Category = "Arcane Nova")
    void GatherNovaTargets(AActor* SourceAvatar, TArray<AActor*>& OutTargets) const;

    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayTagContainer* SourceTags = nullptr,
                                    const FGameplayTagContainer* TargetTags = nullptr,
                                    FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Nova", meta = (ClampMin = "1.0"))
    float NovaRadius = 450.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Nova", meta = (ClampMin = "0.0"))
    float BonusDamagePerLevel = 15.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arcane Nova")
    FGameplayTag NovaCueTag;
};
