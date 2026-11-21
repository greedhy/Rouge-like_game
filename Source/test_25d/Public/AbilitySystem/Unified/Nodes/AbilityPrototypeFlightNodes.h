// 调用链：Scope.Prototype 同次帧 -> 原型运动/碰撞 -> Contact TargetSet -> 任意 payload；Period/Expired 接独立子片段。
// 不生成原生投射物 GA/AMyProjectile，不结算伤害；只拥有本次运动、碰撞准入与纯表现宿主。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeFlightNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;
struct FAbilityPrototypeFlightQueryHit;

/** 默认只读本片段帧；运行时配方可以显式覆盖运动轴，不能改Kind/单弹身份/伤害或寿命。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeFlightNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 只覆盖显式选中的行为轴；Kind/单弹身份/旧profile和未选择的数值仍来自原冻结帧。 */
    UPROPERTY(EditAnywhere)
    bool bOverrideMotion = false;
    UPROPERTY(EditAnywhere)
    EAbilityProjectileMotion Motion = EAbilityProjectileMotion::Linear;
    UPROPERTY(EditAnywhere)
    bool bOverrideImpactPolicy = false;
    UPROPERTY(EditAnywhere)
    EAbilityProjectileImpactPolicy ImpactPolicy = EAbilityProjectileImpactPolicy::StopFirst;
    UPROPERTY(EditAnywhere)
    bool bOverrideImpactLimit = false;
    UPROPERTY(EditAnywhere)
    int32 ImpactLimit = 1;
    UPROPERTY(EditAnywhere)
    bool bOverrideDamageFalloff = false;
    UPROPERTY(EditAnywhere)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere)
    bool bOverrideSpeed = false;
    UPROPERTY(EditAnywhere)
    float Speed = 1000.f;
    UPROPERTY(EditAnywhere)
    bool bOverrideRange = false;
    UPROPERTY(EditAnywhere)
    float Range = 1200.f;
    UPROPERTY(EditAnywhere)
    bool bOverrideCollisionRadius = false;
    UPROPERTY(EditAnywhere)
    float CollisionRadius = 12.f;
    UPROPERTY(EditAnywhere)
    bool bOverrideHomingTurnRate = false;
    UPROPERTY(EditAnywhere)
    float HomingTurnRate = 360.f;

    /** 运行时配方只写明确的覆盖位，不改原型DA或复制第二份施法寿命。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
    /** 只校验实际启用的覆盖值；没有选中的默认字段不改变旧参数的合法范围。 */
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeFlightNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 安装当前帧的运动和纯表现资源；没有即时接触或隐式伤害。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 共享分段与原 profile 碰撞；每次 Contact 的同步 payload 完成后才继续处理下一接触。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 只清理精确持有的表现；取消不派生 OnExpire、命中或额外投递。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 一帧最多跨出发/返航两个阶段；碰撞和效果之间保持实际步骤屏障。 */
    void Move(float DeltaSeconds);
    /** 位置先到达碰撞点，再向同图发布只含原生命的接触；返回false表示已取消/结束。 */
    bool Contact(const FAbilityPrototypeFlightQueryHit& Hit, float Scale);
    /** 同步当前位置到既有复制宿主，不推进任何伤害或额外时钟。 */
    void RefreshPose(bool bRotate);
    void CompleteFlight(bool bCollisionImpact = false);
    /** 只释放该飞行的并发资源；累计/速率额度不会返还，接触和折返不重新预留。 */
    void ReleasePhysicalLease();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    FAbilityProjectileMotionState Motion;
    /** 在Start从不可变帧和明确覆盖解析一次；帧/图配置不被写回，后续配方修改不会影响在途飞行。 */
    FAbilityProjectileMotionConfig EffectiveFlight;
    EAbilityProjectileImpactPolicy EffectiveImpactPolicy = EAbilityProjectileImpactPolicy::StopFirst;
    int32 EffectiveImpactLimit = 1;
    float EffectiveDamageFalloff = 1.f;
    FAbilityPrototypePresentationFrame Visual;
    TOptional<FAbilityTargetToken> InitialTarget;
    TSet<FPureMassCombatTarget> Visited;
    float Age = 0.f;
    float PeriodClock = 0.f;
    uint32 ImpactId = 0;
    bool bCompleting = false;
    FGuid PhysicalLease;
};

namespace AbilityPrototypeFlightNodes
{
    /** Origin -> Contact:TargetSet、Period/Expired:Origin 非终结；Completed:Origin 终结。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeFlightNodes
