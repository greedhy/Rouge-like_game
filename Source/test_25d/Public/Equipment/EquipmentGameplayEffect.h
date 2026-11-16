// 调用链：装备事务 → 共享安全校验 → 固定或随机 GE Spec；暗金子类复用同一生命周期约束。
#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EquipmentGameplayEffect.generated.h"

/** Reversible equipment modifiers. Arbitrary GE components/executions are deliberately disallowed. */
UCLASS(Blueprintable)
class TEST_25D_API UEquipmentGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UEquipmentGameplayEffect();
    // 校验可回收的装备效果；普通/随机类型只允许 Channel1 加法。
    virtual bool IsSafeEquipmentEffect(float Level, bool bAllowRolled = false) const;

protected:
    // 为专用暗金开放受限末级乘法，其余持续策略、组件与属性白名单由所有装备共享。
    bool ValidateEquipmentEffect(float Level, bool bAllowRolled, bool bAllowCompoundMultipliers) const;
};

/** Fixed whitelist of attributes; per-instance values are injected into the Spec, never the CDO. */
UCLASS(Blueprintable)
class TEST_25D_API URandomEquipmentGameplayEffect : public UEquipmentGameplayEffect
{
    GENERATED_BODY()
public:
    URandomEquipmentGameplayEffect();
};
