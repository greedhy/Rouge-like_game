// 调用链：统一GE自动化 -> 任意Tag SetByCaller即时GE/独立护甲GE -> 真实属性验证。
#pragma once
#include "GameplayEffect.h"
#include "AbilitySystem/Ability/MyHitReactAbility.h"
#include "AbilitySystem/Ability/MyAttributeEventAbility.h"
#include "Character/EnemyCharacter.h"
#include "AbilityGameplayEffectTestTypes.generated.h"
UCLASS()
class UUtilityInstantTestEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UUtilityInstantTestEffect();
};
UCLASS()
class UUtilityOwnedTestEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UUtilityOwnedTestEffect();
};
UCLASS()
class UUtilityHitReactTestAbility : public UMyHitReactAbility
{
    GENERATED_BODY()
public:
    UUtilityHitReactTestAbility();
};
UCLASS()
class AUtilityClientListenerTestActor : public AEnemyCharacter
{
    GENERATED_BODY()
public:
    void UseClientRole()
    {
        SetRole(ROLE_AutonomousProxy);
    }
};
UCLASS()
class UUtilityClientListenerTestAbility : public UMyAttributeEventAbility
{
    GENERATED_BODY()
public:
    UUtilityClientListenerTestAbility();
    /** 只驱动GAS前端激活，不模拟RPC传输；世界、ASC权限、预测键和Wait任务都使用真实对象。 */
    void ActivateClientForTest(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FPredictionKey Key)
    {
        FGameplayAbilityActivationInfo Activation;
        Activation.SetPredicting(Key);
        CallActivateAbility(Handle, Info, Activation);
    }
};
