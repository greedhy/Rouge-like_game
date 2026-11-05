#pragma once
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/StanderWeaponActionConfig.h"
#include "StanderWeaponActionAbility.generated.h"
class AStanderPlayer;
class AStanderWeaponProjectile;
UCLASS()
class TEST_25D_API UStanderWeaponActionAbility : public UMyDamageGameplayAbility
{
 GENERATED_BODY()
public:
 UStanderWeaponActionAbility();
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UStanderWeaponActionConfig> ActionConfig;
 UFUNCTION(BlueprintCallable, Category="Weapon Authoring") void InitializeActionIdentity();
 UFUNCTION(BlueprintCallable, Category="Weapon Authoring") static bool RebuildActionMontage(UAnimMontage* Montage);
 virtual void PostLoad() override;
 virtual bool CanActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,const FGameplayTagContainer* =nullptr,const FGameplayTagContainer* =nullptr,FGameplayTagContainer* =nullptr) const override;
 virtual bool CheckCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual bool CheckCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual void ActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,const FGameplayEventData*) override;
 virtual void EndAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,bool,bool) override;
 virtual FString GetDescription(int32 Level) override;
 virtual FString GetNextLevelDescription(int32 Level) override { return FString(); }
 virtual bool ShouldCancelOnEquipChange() const override { return true; }
 virtual bool IsEquipmentGrantedOnly() const override { return true; }
private:
 void Advance();
 void Execute(const FStanderWeaponActionEvent& Event);
 void Move(const FStanderWeaponActionEvent& Event, float Position);
 TWeakObjectPtr<AStanderPlayer> Player;
 TArray<TWeakObjectPtr<AStanderWeaponProjectile>> Projectiles;
 FTimerHandle Timer;
 FActiveGameplayEffectHandle GuardHandle;
 FVector Forward;
 int32 NextEvent=0;
 uint8 PreviousMovement=0;
 bool OwnMovement=false;
 float LastPosition=0;
 float JumpOffset=0;
 int32 OriginRoom=INDEX_NONE;
 TSet<int32> BlockedMoves;
 float GuardEnds=-1;
};
