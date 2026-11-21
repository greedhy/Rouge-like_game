// 调用链：自动化 -> ASC 激活该测试前端 -> 真实 UnifiedGraphAbility 等待目标 -> 断言本次配方快照。
#pragma once
#include "AbilityGraphTestAbility.h"
#include "AbilityRuntimeRecipeFrontendTestTypes.generated.h"

/** 仅增加一个已登记的测试技能身份；目标、费用、图运行全部使用生产父类。 */
UCLASS(Transient, NotBlueprintable)
class UAbilityRuntimeRecipeFrontendTestAbility : public UAbilityGraphTestAbility
{
    GENERATED_BODY()
public:
    UAbilityRuntimeRecipeFrontendTestAbility()
    {
        SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Prototype.Burst"))));
    }
};
