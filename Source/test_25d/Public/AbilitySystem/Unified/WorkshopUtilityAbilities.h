// 调用链：改装目录 -> ASC 临时 GA -> 统一目标采集和 GAS Commit -> 原生默认图或试装快照。
#pragma once

#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "WorkshopUtilityAbilities.generated.h"

/** 试装实用法术共用固定费用和冷却；实际防御与控制在图节点中执行。 */
UCLASS(Abstract)
class TEST_25D_API UWorkshopUtilityAbility : public UMageGraphAbility
{
    GENERATED_BODY()

public:
    UWorkshopUtilityAbility();
    virtual FString GetDescription(int32 Level) override;
    virtual const FGameplayTagContainer* GetCooldownTags() const override;

protected:
    /** 两端都读取资源做准入检查，实际扣除由服务器 Commit 完成。 */
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayTagContainer* Relevant) const override;
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayAbilityActivationInfo Activation) const override;
    /** 用既有冷却效果复制服务器授予的独立冷却标签。 */
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                               FGameplayAbilityActivationInfo Activation) const override;

    float ManaCost = 0.f;
    float Cooldown = 0.f;
    FGameplayTagContainer CooldownTags;
};

/** 服务器自身施放，根节点直接授予 Avatar 短时护盾。 */
UCLASS()
class TEST_25D_API UWorkshopArcaneBarrier : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopArcaneBarrier();
};

/** 地面选点，服务器执行一次圆形查询并交给既有石化服务。 */
UCLASS()
class TEST_25D_API UWorkshopStasisField : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopStasisField();
};
