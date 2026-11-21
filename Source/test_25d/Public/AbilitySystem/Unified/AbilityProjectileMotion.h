// 调用链：原Actor/Mixed或图投递 -> 纯飞行分段/接触准入 -> 调用者扫掠/效果；无世界状态或第二执行器。
#pragma once
#include "CoreMinimal.h"
#include "AbilityProjectileMotion.generated.h"

UENUM(BlueprintType)
enum class EAbilityProjectileMotion : uint8
{
    Linear,
    LegacyTurnHoming,
    NativeHoming,
    Returning
};

UENUM(BlueprintType)
enum class EAbilityProjectileImpactPolicy : uint8
{
    StopFirst,
    Pierce,
    ReturnPerPhase
};

enum class EAbilityProjectilePhase : uint8
{
    Outbound,
    Returning
};

struct TEST_25D_API FAbilityProjectileMotionConfig
{
    EAbilityProjectileMotion Motion = EAbilityProjectileMotion::Linear;
    float Speed = 900.f;
    float Range = 1200.f;
    float CollisionRadius = 12.f;
    float HomingTurnRate = 360.f;
};

enum class EAbilityProjectileContactDisposition : uint8
{
    Ignore,
    Continue,
    Terminate
};

struct TEST_25D_API FAbilityProjectileMotionState
{
    FVector Position = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    float OutboundDistance = 0.f;
    EAbilityProjectilePhase Phase = EAbilityProjectilePhase::Outbound;
};

struct TEST_25D_API FAbilityFlightSegment
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    float SecondsUsed = 0.f;
    float RemainingSeconds = 0.f;
    bool bReachesPhaseEnd = false;
};

struct TEST_25D_API FAbilityImpactAdmission
{
    bool bAccepted = false;
    bool bTerminateAfterImpact = false;
    float DamageScale = 0.f;
};

namespace AbilityProjectileMotion
{
    /** 返回本阶段真实移动段；折返余量由调用者继续消费，不隐含碰撞或位移。 */
    TEST_25D_API bool ComputeFlightSegment(const FAbilityProjectileMotionConfig& Config,
                                           const FAbilityProjectileMotionState& State, float DeltaSeconds,
                                           FVector SourcePosition, const TOptional<FVector>& TargetPosition,
                                           FAbilityFlightSegment& OutSegment);
    /** 重复接触不消费次数；往返按阶段计算倍率且达到数量不会截断飞行。 */
    TEST_25D_API FAbilityImpactAdmission AdmitImpact(EAbilityProjectileImpactPolicy Policy,
                                                     EAbilityProjectilePhase Phase, int32 UniqueOrdinal,
                                                     bool bAlreadyVisited, int32 Limit, float Falloff);
} // namespace AbilityProjectileMotion
