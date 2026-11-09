// 调用链：伤害节点/兼容策略 -> Apply -> Actor GAS GE / Mass 子系统；不复制第二套伤害公式。
#pragma once

#include "CoreMinimal.h"
#include "MyAbilityTypes.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

/** Actor 的现有 GE API 不返回准确扣血量，未观测值保持未设置，不能以 BaseDamage 伪造结果。 */
struct TEST_25D_API FAbilityDamageResult
{
    bool bAttempted = false;
    bool bKilled = false;
    TOptional<bool> Applied;
    TOptional<float> ActualDamage;
    FVector ImpactLocation = FVector::ZeroVector;
};

namespace AbilityDamageService
{
    /** 每次伤害前校验来源/目标生命与 ASC 绑定；费用仍由根 GA Commit 单独负责。 */
    TEST_25D_API FAbilityDamageResult Apply(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                            const FDamageEffectParams& Parameters, float Scale = 1.f,
                                            bool bCaptureLocationAfterDamage = false);
} // namespace AbilityDamageService
