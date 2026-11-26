// 调用链：第四页元素法术 -> 统一 Ground 目标与一次 Commit -> 原生图 -> Actor/Mass 伤害或状态。
#pragma once

#include "AbilitySystem/Unified/AbilityElementalExposure.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopElementalAbilities.generated.h"

/** 四个试装入口共用 Ground 目标和默认图的构造，施法时仍用统一前端。 */
UCLASS(Abstract)
class TEST_25D_API UWorkshopElementalAreaAbility : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    UWorkshopElementalAreaAbility();
    /** 仅服务器解析现有伤害 GE，再交给统一前端捕获本次参数。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;

protected:
    /** 构建位置查询、活目标效果和独立表现。仅在默认图构造时调用。 */
    void ConfigureArea(float Radius, float DamageAmount, EAbilityElement Element, const TCHAR* VisualPath);
    /** 在查询前接入现有时序节点。图而非默认 GA 类管理释放等待和取消。 */
    void PrependTiming(FName Type, UAbilityNodeConfig* Config, FName QueryPort);

private:
    UPROPERTY(EditDefaultsOnly, Category = "Workshop")
    TSoftClassPtr<UGameplayEffect> WorkshopDamageEffect;
};

UCLASS()
class TEST_25D_API UWorkshopSoakingRain : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopSoakingRain();
};

UCLASS()
class TEST_25D_API UWorkshopCinderBlast : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopCinderBlast();
};

UCLASS()
class TEST_25D_API UWorkshopThunderImpact : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopThunderImpact();
};

UCLASS()
class TEST_25D_API UWorkshopCrystalBurst : public UWorkshopElementalAreaAbility
{
    GENERATED_BODY()

public:
    UWorkshopCrystalBurst();
};
