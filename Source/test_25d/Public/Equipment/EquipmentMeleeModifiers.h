// 调用链：共享角色接触动作 → 解析当前装备规则 → 原伤害与多播命中特效；快照不修改技能配置。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class UNiagaraSystem;

/** 单次接触动作的结果；目标循环中保持相同数值，装备变化只影响下一次动作。 */
struct FEquipmentMeleeSnapshot
{
    float Damage = 0.f;
    FGameplayTag DamageType;
    UNiagaraSystem* HitEffect = nullptr;
    UNiagaraSystem* BodyEffect = nullptr;
    float BodyEffectDuration = 0.f;
};

namespace EquipmentMeleeModifiers
{
    // 只读取本ASC的生效暗金GE；精确匹配技能，非法/互相冲突的转换使本次动作失败关闭。
    TEST_25D_API bool Resolve(UAbilitySystemComponent* ASC, FGameplayTag AbilityTag,
                              FEquipmentMeleeSnapshot& InOutSnapshot);
} // namespace EquipmentMeleeModifiers
