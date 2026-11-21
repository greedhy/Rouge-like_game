// 调用链：服务器图端口/原前端费用回执 -> 开发观察组件 -> 有界纯值记录 -> 所属玩家开发面板。
// 不传递图、执行对象、Context、ASC 或目标 UObject；记录不能作为施法或伤害授权。
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/Unified/AbilityGraphTypes.h"
#include "AbilityRuntimeRecipeObservations.generated.h"

UENUM(BlueprintType)
enum class EAbilityRuntimeObservationKind : uint8
{
    GraphEvent,
    GraphFinished,
    Cost,
    Delivery
};

/** 由真正执行费用判定的原前端提交；图终结和弹体接触不能推断 GA 是否成功扣费。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeDeveloperCost
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid RootCastId;
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityTag;
    UPROPERTY(BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(BlueprintReadOnly)
    FString FrozenHash;
    UPROPERTY(BlueprintReadOnly)
    FName Stage;
    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly)
    float ResourceBefore = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float ResourceAfter = 0.f;
};

/** 最多保留 128 条；明确区分未知伤害与数值 0，未知 Applied 与 false。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityRuntimeDeveloperObservation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int64 Sequence = 0;
    UPROPERTY(BlueprintReadOnly)
    EAbilityRuntimeObservationKind Kind = EAbilityRuntimeObservationKind::GraphEvent;
    UPROPERTY(BlueprintReadOnly)
    FGuid RootCastId;
    UPROPERTY(BlueprintReadOnly)
    FGuid ExecutionId;
    UPROPERTY(BlueprintReadOnly)
    FGuid InvocationId;
    UPROPERTY(BlueprintReadOnly)
    int64 EventSequence = 0;
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag AbilityTag;
    UPROPERTY(BlueprintReadOnly)
    int32 Revision = 0;
    UPROPERTY(BlueprintReadOnly)
    FString FrozenHash;
    UPROPERTY(BlueprintReadOnly)
    FName NodeType;
    UPROPERTY(BlueprintReadOnly)
    FName Stage;
    UPROPERTY(BlueprintReadOnly)
    float ServerSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly)
    FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FVector Direction = FVector::ForwardVector;
    UPROPERTY(BlueprintReadOnly)
    FName TargetKind;
    UPROPERTY(BlueprintReadOnly)
    FString TargetLabel;
    UPROPERTY(BlueprintReadOnly)
    int32 MassEntityId = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly)
    int32 TargetLease = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly)
    bool bConfirmedHit = false;
    UPROPERTY(BlueprintReadOnly)
    bool bDamageAttempted = false;
    UPROPERTY(BlueprintReadOnly)
    bool bActualDamageKnown = false;
    UPROPERTY(BlueprintReadOnly)
    float ActualDamage = 0.f;
    UPROPERTY(BlueprintReadOnly)
    bool bAppliedKnown = false;
    UPROPERTY(BlueprintReadOnly)
    bool bApplied = false;
    UPROPERTY(BlueprintReadOnly)
    bool bKilled = false;
    UPROPERTY(BlueprintReadOnly)
    EAbilityGraphEndReason EndReason = EAbilityGraphEndReason::Completed;
    UPROPERTY(BlueprintReadOnly)
    bool bCostSucceeded = false;
    UPROPERTY(BlueprintReadOnly)
    float ResourceBefore = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float ResourceAfter = 0.f;
};
