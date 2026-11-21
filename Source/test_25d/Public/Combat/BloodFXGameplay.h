// 调用链：近战捕获视觉快照 → 原 GAS/Mass 扣血 → 确认结果 → Actor/Crowd 复制表现。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

class UBloodFXComponent;
class APureMassCrowd;
class UPureMassMonsterType;
struct FAbilityDamageResult;

/** 同步伤害前快照。位置与物种保留到致死结果，Actor 身份仍按原生命验证。 */
struct FBloodHitSnapshot
{
    TWeakObjectPtr<UBloodFXComponent> Component;
    TWeakObjectPtr<APureMassCrowd> Crowd;
    TWeakObjectPtr<UPureMassMonsterType> MassType;
    int32 Life = 0;
    int32 MassId = INDEX_NONE;
    FVector Location = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
};

namespace BloodFXGameplay
{
    TEST_25D_API FBloodHitSnapshot Capture(const FAbilityTargetToken& Target, FVector Origin, FVector TargetLocation);
    TEST_25D_API void Confirm(const FBloodHitSnapshot& Snapshot, const FAbilityDamageResult& Result, FGuid AttackId);
} // namespace BloodFXGameplay
