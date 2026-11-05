#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Player/SwordShieldConfig.h"
#include "SwordShieldAbility.generated.h"
class ASwordShieldPlayer;
UCLASS()
class TEST_25D_API USwordGuardEffect : public UGameplayEffect
{
 GENERATED_BODY()
public:
 USwordGuardEffect();
 static FGameplayTag StateTag();
};
UCLASS()
class TEST_25D_API USwordTimedEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: USwordTimedEffect();
};
UCLASS()
class TEST_25D_API USwordManaEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: USwordManaEffect();
};
UCLASS(Abstract)
class TEST_25D_API USwordShieldAbility : public UMyDamageGameplayAbility
{
 GENERATED_BODY()
public:
 USwordShieldAbility();
 virtual bool CanActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,const FGameplayTagContainer* =nullptr,const FGameplayTagContainer* =nullptr,FGameplayTagContainer* =nullptr) const override;
 virtual bool CheckCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual bool CheckCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayTagContainer*) const override;
 virtual void ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo) const override;
 virtual void ActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,const FGameplayEventData*) override;
 virtual void EndAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,bool,bool) override;
 virtual FString GetDescription(int32 Level) override;
 virtual bool ShouldCancelOnEquipChange() const override { return true; }
protected:
 enum class EMode:uint8 { Combo,Guard,Spin };
 EMode Mode=EMode::Combo;
 FGameplayTag CooldownIdentity;
private:
 const USwordShieldConfig* Config(const FGameplayAbilityActorInfo*) const;
 void Advance();
 void Hit();
 TWeakObjectPtr<ASwordShieldPlayer> Player;
 FSwordStrike Strike;
 FTimerHandle Timer;
 FActiveGameplayEffectHandle GuardHandle;
 double Started=0,LastComboEnd=-100;
 int32 NextCombo=0,CurrentCombo=0;
 uint8 PreviousMovement=0;
 bool bOwnMovement=false,bHit=false;
 FVector LockedForward=FVector::ForwardVector;
 float GuardTime=1;
};
UCLASS()
class TEST_25D_API USwordComboAbility : public USwordShieldAbility
{ GENERATED_BODY() public: USwordComboAbility(); };
UCLASS()
class TEST_25D_API USwordGuardAbility : public USwordShieldAbility
{ GENERATED_BODY() public: USwordGuardAbility(); };
UCLASS()
class TEST_25D_API USwordSpinAbility : public USwordShieldAbility
{ GENERATED_BODY() public: USwordSpinAbility(); };
