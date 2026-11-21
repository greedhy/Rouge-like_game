// 调用链：旧行为兼容适配或注册图策略 -> 候选/几何/节拍纯函数 -> 调用者执行投递或效果。
// 本模块不持有 World、Actor、ASC、计时器或伤害状态；候选生命与可见性由调用者在进入前解析。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"

/** 同距候选保留传入顺序；旧驱动使用 <= 选最后一个，新稳定查询默认选第一个。 */
enum class EAbilityCandidateTieBreak : uint8
{
    First,
    Last
};

/** 目标已由调用者捕获原生命；位置也是本次查询的局部快照，不授权稍后直接作用于目标。 */
struct TEST_25D_API FAbilityTargetCandidate
{
    FAbilityTargetToken Target;
    FVector Location = FVector::ZeroVector;
};

enum class EAbilityBehaviorShape : uint8
{
    Circle,
    Cone,
    Ring,
    Line
};

/** 原驱动多以距离平方比较；原 Query.Shape 的平面几何先转换成 float 距离，保留该边界。 */
enum class EAbilityShapeDistanceMode : uint8
{
    Squared,
    FloatMagnitude
};

/** 明确几何而不隐含阵营、LOS 或伤害；旧前端分别填写自己的距离/高度/角度容差合同。 */
struct TEST_25D_API FAbilityShapeFilter
{
    EAbilityBehaviorShape Kind = EAbilityBehaviorShape::Circle;
    float Radius = 250.f;
    float InnerRadius = 100.f;
    float Width = 140.f;
    float Range = 1200.f;
    float HalfHeight = 120.f;
    float SpreadDegrees = 45.f;
    float AngleTolerance = 0.f;
    bool bUse3DDistance = true;
    bool bRejectOutsideHeight = false;
    EAbilityShapeDistanceMode DistanceMode = EAbilityShapeDistanceMode::Squared;
    bool bConeOriginUsesPlanarDistance = false;
    bool bUseFloatLateralDistance = false;
    bool bFallbackForward = true;
};

/** 周期预算用尽后的积压处理；一次命中后的传播采用 DiscardAfterPulse 重新等待完整间隔。 */
enum class EAbilityCadenceRemainder : uint8
{
    Preserve,
    ClampToPeriod,
    DiscardAfterPulse
};

/** 原 Actor/第六批波前用 float 距离，普通 Mixed 波用平方扩张体积。 */
enum class EAbilityWaveCoverage : uint8
{
    SweptShell3D,
    GrowingSphere3D
};

namespace AbilityBehaviorPrimitives
{
    /** 比较原生命身份而非只比较 Actor 地址或 Mass 数字 ID；不解析/捕获新生命。 */
    TEST_25D_API bool SameTargetLife(const FAbilityTargetToken& A, const FAbilityTargetToken& B);

    /**
     * 从已过滤且已捕获的候选选择最近未访问生命；非法几何返回空，不重新查询世界。
     * 输入顺序在同距时通过 TieBreak 明确保留；半径边界由 bIncludeBoundary 指定。
     */
    TEST_25D_API TOptional<FAbilityTargetCandidate> SelectNearest(
        const TArray<FAbilityTargetCandidate>& Candidates, FVector Origin, float MaxDistance,
        const TArray<FAbilityTargetToken>& Visited,
        EAbilityCandidateTieBreak TieBreak = EAbilityCandidateTieBreak::First, bool bIncludeBoundary = true);

    /** 判断点是否符合本次形状；非法数值失败关闭，不进行世界查询、LOS 或目标授权。 */
    TEST_25D_API bool ContainsPoint(const FAbilityShapeFilter& Filter, FVector Origin, FVector Forward, FVector Point);

    /** 只判断本帧空间覆盖；访问历史、原生命、LOS 与最终帧策略仍属于调用者。 */
    TEST_25D_API bool ContainsWavePoint(FVector Origin, FVector Point, float PreviousRadius, float CurrentRadius,
                                        EAbilityWaveCoverage Coverage);

    /**
     * 累计时间并逐次消费周期；每次消费后才回调，回调返回 false 立即停止，不预消费未来周期。
     * Clock 属于调用者；函数不保存它或 Pulse，不能在回调后跨越调用者自身的生命周期。
     * 容差和减法归零分别保留原 Actor/Mixed/Extension 边界，非法输入保持 Clock 不变。
     */
    TEST_25D_API int32 AdvanceCadence(float& Clock, float DeltaSeconds, float Period, int32 MaxPulses,
                                      EAbilityCadenceRemainder Remainder, TFunctionRef<bool()> Pulse,
                                      float BoundaryTolerance = 0.f, bool bClampClockAtZero = false);
} // namespace AbilityBehaviorPrimitives
