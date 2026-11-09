// 调用链：查询节点/行为策略 -> Gather/Sweep -> 原混合查询 -> 捕获服务器生命令牌 -> 后继节点。
// 仅提供查询，不提交费用、不施加伤害；返回的身份必须在后续异步应用时重新验证。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilityTargetQueryService.generated.h"

UENUM(BlueprintType)
enum class EAbilityQueryGeometry : uint8
{
    Sphere,
    Cone,
    Ring,
    Line,
    WaveFront
};

/** 几何与可见性是显式配置；旧行为不得依靠默认值改变自己的 LOS 规则。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityQueryRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityQueryGeometry Geometry = EAbilityQueryGeometry::Sphere;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Origin = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Direction = FVector::ForwardVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InnerRadius = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Range = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Width = 140.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HalfHeight = 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HalfAngleDegrees = 45.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PreviousRadius = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTargets = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRequireVisibility = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeActors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIncludeMass = true;

    /** 严格检查有限参数，防止无界查询和不受支持的几何；不修正设计师配置。 */
    bool Validate(FString& Error) const;
};

struct TEST_25D_API FAbilitySweepRequest
{
    FVector Start = FVector::ZeroVector;
    FVector End = FVector::ZeroVector;
    float Radius = 12.f;
    TSet<FPureMassCombatTarget> Ignored;
    TWeakObjectPtr<AActor> IgnoredEffect;
};

struct TEST_25D_API FAbilityQueryHit
{
    TOptional<FAbilityTargetToken> Target;
    FHitResult Wall;
    float Time = 1.f;
    FVector ImpactPoint = FVector::ZeroVector;
};

namespace AbilityTargetQueryService
{
    /** 成功可返回空列表；源失效/配置错误返回 false。逐结果捕获池租约/Mass 真实句柄。 */
    TEST_25D_API bool Gather(const FAbilitySourceToken& Source, const FAbilityQueryRequest& Request,
                             TArray<FAbilityTargetToken>& Out, FString& Error);
    /** 共用现有混合扫掠的墙优先规则；命中点是位置快照，不是后续伤害权限。 */
    TEST_25D_API bool Sweep(const FAbilitySourceToken& Source, const FAbilitySweepRequest& Request,
                            FAbilityQueryHit& Out, FString& Error);
} // namespace AbilityTargetQueryService
