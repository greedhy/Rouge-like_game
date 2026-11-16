// 调用链：传统GA/统一投射物动作 -> 本次装备规则快照 -> 原投射物生成与伤害参数。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

struct FAbilitySourceToken;
struct FAbilityActorProjectileSettings;
struct FDamageEffectParams;

namespace EquipmentAbilityModifiers
{
    /**
     * 仅在权威来源仍有效时读取其生效暗金GE，修改调用者持有的本次发射副本。
     * Count已经过技能自身等级限制。无匹配规则时保持所有原值；非法组合导致非有限伤害时返回false。
     * 不保存GE句柄，不写CDO，也不改变已经生成的投射物。
     */
    TEST_25D_API bool ApplyProjectileRules(const FAbilitySourceToken& Source, FAbilityActorProjectileSettings& Settings,
                                           FDamageEffectParams& Damage, int32& Count);

    /** 锁定已接入节点的系列身份。空或任意旧技能标签无法命中范围/雷链规则。 */
    TEST_25D_API FGameplayTag GetSpellFamily(FGameplayTag AbilityTag);
    TEST_25D_API FGameplayTag ArcaneAreaFamily();
    TEST_25D_API FGameplayTag LightningChainFamily();

    /** 在节点启动时修正本次伤害、范围和雷链目标预算。失败不写回调用者快照。 */
    TEST_25D_API bool ApplySpellRules(const FAbilitySourceToken& Source, FGameplayTag AbilityTag, float& Radius,
                                     FDamageEffectParams& Damage, int32& ExtraChainTargets);
} // namespace EquipmentAbilityModifiers
