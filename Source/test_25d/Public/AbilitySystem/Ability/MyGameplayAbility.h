// 主要调用链：GAS授予与激活 → 派生GA复用描述、消耗/冷却查询和攻击报告接口 → ASC与技能UI。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MyGameplayAbility.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    FGameplayTag StartupInputTag;
    // Toggle abilities use a single press routed to authority, never held activation.
    virtual bool IsInputToggleAbility() const
    {
        return false;
    }
    virtual bool ShouldCancelOnEquipChange() const
    {
        return IsInputToggleAbility();
    }
    virtual bool IsEquipmentGrantedOnly() const
    {
        return false;
    }

    // 调用父类并为活动技能转发带预测键的按下事件。
    virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                              const FGameplayAbilityActivationInfo ActivationInfo) override;
    // 调用父类并转发带预测键的松开事件。
    virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo) override;

    // 生成当前等级的默认富文本描述，供派生技能覆盖。
    virtual FString GetDescription(int32 Level);
    // 生成下一等级的默认描述，供派生技能覆盖。
    virtual FString GetNextLevelDescription(int32 Level);
    // 生成包含解锁等级的提示文本。
    static FString GetLockedDescription(int32 Level);

protected:
    // 从费用 GE 的法力修饰器尝试求指定等级静态消耗。
    float GetManaCost(float InLevel = 1.f) const;
    // 从冷却 GE 尝试求指定等级静态持续时间。
    float GetCooldown(float InLevel = 1.f) const;
};
