// 调用链：服务器入口捕获令牌 -> 每次调用输入 -> Publish 快照 -> 后继节点重新验证活目标。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "MyAbilityTypes.h"

/** 确认命中的历史事实；ConfirmedTarget 不能代替 LiveTargetToken 授权状态应用。 */
struct TEST_25D_API FAbilityImpactSnapshot
{
    FVector Location = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    FAbilityTargetToken ConfirmedTarget;
    uint64 Sequence = 0;
};

/** 只在权威进程内传递；所有对象引用由调用执行对象另行保活。 */
struct TEST_25D_API FAbilityNodeInput
{
    FAbilitySourceToken SourceToken;
    TOptional<FAbilityTargetToken> LiveTargetToken;
    FVector Origin = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    TOptional<FAbilityImpactSnapshot> Impact;
    FVector EventCenter = FVector::ZeroVector;
    bool bConfirmedHit = false;
    FDamageEffectParams DamageParams;
    float Level = 1.f;
    TWeakObjectPtr<UObject> Context;
    bool bHasDamageResult = false;
    bool bDamageAttempted = false;
    bool bKilled = false;
    TOptional<bool> DamageApplied;
    TOptional<float> ActualDamage;
};
