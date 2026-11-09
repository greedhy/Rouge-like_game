// 调用链：自动化ASC授予 -> 原FireBolt/单发前端 -> 共享动作；只固定资产无关的测试配置。
#pragma once
#include "AbilitySystem/Ability/MyFireBolt.h"
#include "AbilitySystem/Ability/SummonAbility.h"
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Character/SwordShieldPlayer.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilityTraditionalTestAbility.generated.h"

/** 未PossessedBy的测试世界显式建立真实玩家ASC绑定，不触发HUD或存档。 */
UCLASS(Transient, NotBlueprintable)
class AAbilityTraditionalSwordTest : public ASwordShieldPlayer
{
    GENERATED_BODY()
public:
    void AttachState(AMyPlayerState* State)
    {
        SetPlayerState(State);
        AbilitySystemComponent = State->GetAbilitySystemComponent();
        AttributeSet = State->GetAttributeSet();
        AbilitySystemComponent->InitAbilityActorInfo(State, this);
    }
};

/** 保留生产CanActivate，仅补足测试实例的伤害依赖。 */
UCLASS()
class UAbilityTraditionalSwordHostTest : public USwordComboAbility
{
    GENERATED_BODY()
public:
    void ConfigureDamage()
    {
        DamageEffectClass = UGameplayEffect::StaticClass();
        DamageType = FGameplayTag::RequestGameplayTag(TEXT("Damage.Physics"));
    }
};

UCLASS()
class UAbilityTraditionalFireBoltTest : public UMyFireBolt
{
    GENERATED_BODY()
public:
    UAbilityTraditionalFireBoltTest()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
        ProjectileClass = AMyProjectile::StaticClass();
        HomingAccelerationMin = HomingAccelerationMax = 2300.f;
        Damage = FScalableFloat(42.f);
    }
    void SingleShot(const FVector& Target, bool bPitch, float Pitch)
    {
        SpawnProjectile(Target, {}, NAME_None, bPitch, Pitch);
    }
};

/** 仅暴露生产通知/推进诊断，激活仍由生产USummonAbility实现。 */
UCLASS()
class UAbilityTraditionalSummonTest : public USummonAbility
{
    GENERATED_BODY()
public:
    UAbilityTraditionalSummonTest()
    {
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    }
    void Notify()
    {
        FGameplayEventData Event;
        Event.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Montage.Attack.2"));
        OnSummonNotify(Event);
    }
    void Step(float Delta)
    {
        if (SummonGraphTask)
        {
            SummonGraphTask->TickTask(Delta);
        }
    }
    bool HasSummonWork() const
    {
        return SummonGraphTask || SummonMontageTask || SummonEventTask || SummonSnapshot;
    }
};
