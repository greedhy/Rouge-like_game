// 调用链：第四页试装的地面输入 → 共享胶囊位移 → 实际停点 → 独立敌对地表租约。
#pragma once

#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "WorkshopEmberRushAbility.generated.h"

/** 朝准星冲刺，在实际完成水平位移后的停止位置留下临时熔岩。 */
UCLASS()
class TEST_25D_API UWorkshopEmberRush : public UWorkshopUtilityAbility
{
    GENERATED_BODY()

public:
    /** 配置一次费用、地面位移和可独立改装的落点地块。 */
    UWorkshopEmberRush();

protected:
    /** GAS 在激活和提交前复核移动可用性，避免被其他动作禁用时扣除费用。 */
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                           FGameplayTagContainer* Relevant) const override;
};
