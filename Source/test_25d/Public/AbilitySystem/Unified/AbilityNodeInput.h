// 调用链：服务器入口捕获令牌 -> 每次调用输入 -> Publish 快照 -> 后继节点重新验证活目标。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilitySystem/Unified/AbilityTargetSet.h"
#include "AbilitySystem/Unified/AbilityContactDamageProfile.h"
#include "MyAbilityTypes.h"

class UAbilityExecutionParameters;

/** 确认命中的历史事实；ConfirmedTarget 不能代替 LiveTargetToken 授权状态应用。 */
struct TEST_25D_API FAbilityImpactSnapshot
{
    FVector Location = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    FAbilityTargetToken ConfirmedTarget;
    uint64 Sequence = 0;
    EAbilityContactDamageProfile ContactDamageProfile = EAbilityContactDamageProfile::Unified;
};

/** 只在权威进程内传递；所有对象引用由调用执行对象另行保活。 */
struct TEST_25D_API FAbilityNodeInput
{
    FAbilitySourceToken SourceToken;
    TOptional<FAbilityTargetToken> LiveTargetToken;
    /** 空间锚点可仅供后续选敌排除同一单位；此身份不授权旧目标状态和伤害。 */
    TOptional<FPureMassCombatTarget> ExcludedAnchorTarget;
    /** 批查询与批效果复用同一组原生命；接收方仍须逐个 Resolve，不能把集合当作永久权限。 */
    TSharedPtr<const FAbilityTargetSetSnapshot> TargetSet;
    FVector Origin = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    TOptional<FAbilityImpactSnapshot> Impact;
    FVector EventCenter = FVector::ZeroVector;
    /** 准备节点冻结的实际球形半径；表现与后继效果消费同一值，不回读共享DA。 */
    TOptional<float> ShapeRadius;
    bool bConfirmedHit = false;
    FDamageEffectParams DamageParams;
    /** 组合传播的累计倍率，原参数保持原值；内置或外部 payload 仅消费一次。 */
    float DamageScale = 1.f;
    float DebuffDamageScale = 1.f;
    float Level = 1.f;
    TWeakObjectPtr<UObject> Context;
    /** 本逻辑片段的只读参数，与费用/输入Context分离；节点和队列负责实际GC保活。 */
    TWeakObjectPtr<UAbilityExecutionParameters> ParameterFrame;
    bool bHasDamageResult = false;
    bool bDamageAttempted = false;
    bool bKilled = false;
    TOptional<bool> DamageApplied;
    TOptional<float> ActualDamage;
};
