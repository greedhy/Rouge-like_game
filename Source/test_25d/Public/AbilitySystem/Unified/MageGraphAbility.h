// 调用链：技能目录授予 -> 通用 GA 图入口；被动槽停用 -> EndAbility -> 图撤销自己持有的 GE。
#pragma once

#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "MageGraphAbility.generated.h"

/** 法师技能的资产化菜单文本，执行仍由统一图处理。 */
UCLASS()
class TEST_25D_API UMageGraphAbility : public UUnifiedGraphAbility
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Spell Menu")
    FText SpellName;
    UPROPERTY(EditDefaultsOnly, Category = "Spell Menu")
    FText SpellSummary;

    virtual FString GetDescription(int32 Level) override;
    virtual FString GetNextLevelDescription(int32 Level) override;
};

/** 装备即运行的自身图，移出被动槽后沿用 GAS 任务取消清理。 */
UCLASS()
class TEST_25D_API UMagePassiveGraphAbility : public UMageGraphAbility
{
    GENERATED_BODY()
public:
    UMagePassiveGraphAbility();
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;

private:
    void PassiveDeactivated(const FGameplayTag& AbilityTag);
};
