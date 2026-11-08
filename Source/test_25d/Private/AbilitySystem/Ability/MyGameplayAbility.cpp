// 主要调用链：GAS授予与激活 → 派生GA复用描述、消耗/冷却查询和攻击报告接口 → ASC与技能UI。
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/Ability/MyGameplayAbility.h"

#include "AbilitySystem/MyAttributeSet.h"

// 生成当前等级的默认富文本描述，供派生技能覆盖。
FString UMyGameplayAbility::GetDescription(int32 Level)
{
    return FString::Printf(
        TEXT("<Default>%s, </><Level>%d</>"),
        L"Default Ability Name - LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum LoremIpsum",
        Level);
}

// 生成下一等级的默认描述，供派生技能覆盖。
FString UMyGameplayAbility::GetNextLevelDescription(int32 Level)
{
    return FString::Printf(TEXT("<Default>Next Level: </><Level>%d</> \n<Default>Causes much more damage. </>"), Level);
}

// 生成包含解锁等级的提示文本。
FString UMyGameplayAbility::GetLockedDescription(int32 Level)
{
    return FString::Printf(TEXT("<Default>Spell Locked Until Level: %d</>"), Level);
}

// 从费用 GE 的法力修饰器尝试求指定等级静态消耗。
float UMyGameplayAbility::GetManaCost(float InLevel) const
{
    float ManaCost = 0.f;
    if (const UGameplayEffect* CostEffect = GetCostGameplayEffect())
    {
        for (FGameplayModifierInfo Mod : CostEffect->Modifiers)
        {
            if (Mod.Attribute == UMyAttributeSet::GetManaAttribute())
            {
                Mod.ModifierMagnitude.GetStaticMagnitudeIfPossible(InLevel, ManaCost);
                break;
            }
        }
    }
    return ManaCost;
}

// 从冷却 GE 尝试求指定等级静态持续时间。
float UMyGameplayAbility::GetCooldown(float InLevel) const
{
    float Cooldown = 0.f;
    if (const UGameplayEffect* CooldownEffect = GetCooldownGameplayEffect())
    {
        CooldownEffect->DurationMagnitude.GetStaticMagnitudeIfPossible(InLevel, Cooldown);
    }
    return Cooldown;
}
// 调用父类并为活动技能转发带预测键的按下事件。
void UMyGameplayAbility::InputPressed(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo,
                                      const FGameplayAbilityActivationInfo ActivationInfo)
{
    Super::InputPressed(Handle, ActorInfo, ActivationInfo);
    //通过句柄获取技能实例
    FGameplayAbilitySpec* AbilitySpec = ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle);
    //技能实例在激活状态，触发输入事件
    if (AbilitySpec->IsActive())
    {
        //将按下事件复制到服务器和所有相关的客户端
        ActorInfo->AbilitySystemComponent->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle,
                                                                 ActivationInfo.GetActivationPredictionKey());
    }
}

// 调用父类并转发带预测键的松开事件。
void UMyGameplayAbility::InputReleased(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo,
                                       const FGameplayAbilityActivationInfo ActivationInfo)
{
    Super::InputReleased(Handle, ActorInfo, ActivationInfo);

    //将抬起事件复制到服务器和所有相关的客户端
    ActorInfo->AbilitySystemComponent->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle,
                                                             ActivationInfo.GetActivationPredictionKey());
}
