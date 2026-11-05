#pragma once
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/StanderCombatConfig.h"
#include "StanderAbility.generated.h"
class AStanderPlayer;

UCLASS(Abstract)
class TEST_25D_API UStanderAbility : public UMyDamageGameplayAbility
{
 GENERATED_BODY()
public:
 UStanderAbility();
 virtual bool CanActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,const FGameplayTagContainer* =nullptr,const FGameplayTagContainer* =nullptr,FGameplayTagContainer* =nullptr) const override;
 virtual bool CheckCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual bool CheckCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual void ActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,const FGameplayEventData*) override;
 virtual void EndAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,bool,bool) override;
 virtual FString GetDescription(int32 Level) override;
 virtual bool ShouldCancelOnEquipChange() const override { return true; }
 static bool IsMassCounterDamage(const FGameplayEffectSpec& Spec);
 static bool IsCounterDamage(const FGameplayEffectSpec& Spec);
protected:
 enum class EMode:uint8 { Combo,Guard,Spin,Dash,Lift,ShieldBash };
 EMode Mode=EMode::Combo;
 FGameplayTag CooldownIdentity;
private:
 const UStanderCombatConfig* Config(const FGameplayAbilityActorInfo*) const;
 float Cost(const UStanderCombatConfig*) const;
 float Cooldown(const UStanderCombatConfig*) const;
 void Advance();
 void Hit();
 bool StartStrike(const FSwordStrike& NewStrike);
 void MoveDash(float Position);
 void GuardHit(UAbilitySystemComponent*,const FGameplayEffectSpec&,FActiveGameplayEffectHandle);
 TWeakObjectPtr<AStanderPlayer> Player;
 FSwordStrike Strike;
 FTimerHandle Timer;
 FDelegateHandle GuardListener;
 FActiveGameplayEffectHandle GuardHandle;
 double Started=0,LastComboEnd=-100;
 int32 NextCombo=0,CurrentCombo=0,OriginRoom=INDEX_NONE;
 uint8 PreviousMovement=0;
 bool bOwnMovement=false,bHit=false,bCounter=false,bCounterPending=false,bDashBlocked=false;
 FVector LockedForward=FVector::ForwardVector;
 float DashApplied=0;
};
UCLASS()
class TEST_25D_API UStanderComboAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderComboAbility(); };
UCLASS()
class TEST_25D_API UStanderGuardAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderGuardAbility(); };
UCLASS()
class TEST_25D_API UStanderSpinAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderSpinAbility(); };
UCLASS()
class TEST_25D_API UStanderDashAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderDashAbility(); };

UCLASS()
class TEST_25D_API UStanderLiftAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderLiftAbility(); };
UCLASS()
class TEST_25D_API UStanderShieldBashAbility : public UStanderAbility
{ GENERATED_BODY() public: UStanderShieldBashAbility(); };
