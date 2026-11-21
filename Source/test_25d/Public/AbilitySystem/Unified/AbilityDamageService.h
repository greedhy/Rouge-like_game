// 调用链：伤害节点/兼容策略 -> Apply -> Actor GAS GE / Mass 子系统；不复制第二套伤害公式。
#pragma once

#include "CoreMinimal.h"
#include "MyAbilityTypes.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityContactDamageProfile.h"

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
    /** 仅实际伤害所有者调用：原值/累计倍率变成本次局部副本并一次生成接触力；Apply使用Scale=1。 */
    TEST_25D_API bool PrepareContactDamage(
        const FDamageEffectParams& Original, FVector Direction, float DamageScale, float DebuffDamageScale,
        FDamageEffectParams& OutPrepared, EAbilityContactDamageProfile Profile = EAbilityContactDamageProfile::Unified);
    /** 每次伤害前校验来源/目标生命与 ASC 绑定；费用仍由根 GA Commit 单独负责。 */
    TEST_25D_API FAbilityDamageResult Apply(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                            const FDamageEffectParams& Parameters, float Scale = 1.f,
                                            bool bCaptureLocationAfterDamage = false);
} // namespace AbilityDamageService
