// 调用链：图投递/传播策略 -> Begin -> 原移动组件真实飞行/共享分段 -> 原接触裁决 -> 命中流/唯一终态。
// 每个实例只属于一次投递；图或传播节点保活，来源/目标为弱生命令牌，结束先关闭回调再释放弹体。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "Components/SceneComponent.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilityDeliveryService.generated.h"

class AMyProjectile;
class USceneComponent;

/** 投递终态描述真实接触或失败原因，不把生成完成当成命中。 */
enum class EAbilityDeliveryOutcome : uint8
{
    Hit,
    Blocked,
    Expired,
    TargetLost,
    Cancelled,
    Returned
};

/** Explicit用于新组合；原FireBolt配置保留真实ProjectileClass的飞行参数。 */
UENUM(BlueprintType)
enum class EAbilityDeliveryFlightProfile : uint8
{
    Explicit,
    NativeFireBoltClassDefaults
};

/** 原Actor接触与Mass桥的力/RNG/碰撞合同不同，不能把Unified冒充零改动兼容。 */
UENUM(BlueprintType)
enum class EAbilityDeliveryContactProfile : uint8
{
    Unified,
    NativeFireBoltActorAndMass
};

UENUM(BlueprintType)
enum class EAbilityDeliveryTargetLossPolicy : uint8
{
    Stop,
    KeepLastAimPoint
};

/** 可复用飞行参数；只包含可冻结的配置，不保存施法者、目标或本次时钟。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityDeliverySettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AMyProjectile> ProjectileClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityDeliveryFlightProfile FlightProfile = EAbilityDeliveryFlightProfile::Explicit;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityDeliveryContactProfile ContactProfile = EAbilityDeliveryContactProfile::Unified;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityDeliveryTargetLossPolicy TargetLossPolicy = EAbilityDeliveryTargetLossPolicy::Stop;
    /** Native profile保留“配置追踪”和“启用追踪”独立；禁追踪仍需原每弹随机采样。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool NativeConfigureHoming = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Speed = 900.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxDistance = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimeoutSeconds = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CollisionRadius = 12.f;
    /** 仅原生class profile可选：实际构造后按世界cm覆盖半径，其余类参数保持原值。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverrideNativeCollisionRadius = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHoming = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityProjectileMotion Motion = EAbilityProjectileMotion::Linear;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityProjectileImpactPolicy ImpactPolicy = EAbilityProjectileImpactPolicy::StopFirst;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ImpactLimit = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageFalloff = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HomingTurnRate = 360.f;
    /** 原FireBolt的原生追踪加速度区间；每个实际弹体独立采样，固定值可令Min等于Max。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HomingAccelerationMin = 1600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HomingAccelerationMax = 3200.f;
    /** false 仅交付命中快照，供图上独立效果节点消费；true 在服务中唯一结算伤害。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bApplyDamage = true;

    /** 拒绝空类、非有限或非正飞行参数；失败不产生 Actor。 */
    bool Validate(FString& Error) const;
};

struct TEST_25D_API FAbilityDeliveryRequest
{
    FAbilitySourceToken Source;
    TOptional<FAbilityTargetToken> Target;
    TOptional<FVector> AimPoint;
    FVector Origin = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    FDamageEffectParams DamageParams;
    float DamageScale = 1.f;
    float DebuffDamageScale = 1.f;
    FAbilityDeliverySettings Settings;
    FGuid RootCastId;
    FGuid StepId;
    /** 只排除该传播已访问的原生命；不会把池中下一生命永久加入忽略集合。 */
    TArray<FAbilityTargetToken> IgnoredTargets;
};

struct TEST_25D_API FAbilityDeliveryResult
{
    FGuid DeliveryId;
    FGuid RootCastId;
    FGuid StepId;
    EAbilityDeliveryOutcome Outcome = EAbilityDeliveryOutcome::Cancelled;
    TOptional<FAbilityImpactSnapshot> Impact;
    FAbilityDamageResult Damage;
    float DamageScale = 1.f;
    float DebuffDamageScale = 1.f;
    FVector Location = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FAbilityDeliveryFinished, const FAbilityDeliveryResult&);

/** 一个投递的非终结命中流；命中序号独立于唯一终态回执。 */
struct TEST_25D_API FAbilityDeliveryImpact
{
    FGuid DeliveryId;
    FGuid RootCastId;
    FGuid StepId;
    uint64 ImpactSequence = 0;
    EAbilityProjectilePhase Phase = EAbilityProjectilePhase::Outbound;
    FAbilityImpactSnapshot Impact;
    FAbilityDamageResult Damage;
    float DamageScale = 1.f;
    float DebuffDamageScale = 1.f;
    bool bRequireImmediatePayload = false;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FAbilityDeliveryImpactReceived, const FAbilityDeliveryImpact&);

/** 实际弹体由世界拥有；本对象保活配置/伤害引用并管理一次回执，不是另一个技能调度器。 */
UCLASS(Transient)
class TEST_25D_API UAbilityDeliveryInstance : public UObject
{
    GENERATED_BODY()
public:
    /** 必须在服务器、有效原来源下启动；重复 Begin 或非法配置不产生投递。 */
    bool Begin(const FAbilityDeliveryRequest& Request, FString& Error);
    /** 持有者推进寿命/生命校验；折返/恒转速模型由共享分段驱动原移动组件，原生追踪仍交给UE。 */
    void Advance(float DeltaSeconds);
    /** 幂等关闭本次入口并销毁仍属于自己的弹体；不发布命中或正常结束效果。 */
    void Cancel();
    bool IsFinished() const
    {
        return bFinished;
    }
    AMyProjectile* GetProjectile() const;
    const FAbilityDeliveryResult& GetResult() const
    {
        return Result;
    }
    FAbilityDeliveryFinished OnFinished;
    FAbilityDeliveryImpactReceived OnImpact;

private:
    /** 实际组件移动段同时查询墙、Actor 和 Mass，墙体与 Actor 遮挡保持原查询规则。 */
    void Moved(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);
    /** 捕获命中事实先于伤害，致死后仍保留原位置和生命身份供传播。 */
    EAbilityProjectileContactDisposition Contact(const FAbilityTargetToken& Target, FVector Location,
                                                 FVector Direction);
    /** 原弹体销毁或反射：反射只切断旧投递，不能销毁已转移给防御者的返程弹。 */
    void Retired(bool bReflected);
    /** 幂等认领终态，先解绑接触再释放弹体；回调取消不能产生第二个结果。 */
    void Finish(EAbilityDeliveryOutcome Outcome, bool bDestroyProjectile);
    /** 兼容旧bHoming配置，同时保留原生加速度和原型恒转速两种独立模型。 */
    EAbilityProjectileMotion EffectiveMotion() const;
    /** 每阶段只忽略已访问的原生命；折返点清空阶段历史后同步恢复Actor扫掠资格。 */
    void RefreshIgnoredTargets();
    /** 原Actor实际Sphere墙接触终结；Unified仍由其原混合扫掠裁决。 */
    void NativeBlocked(FVector Location);
    /** Native Actor目标位移时只跟随捕获的原生命；池唤醒不能重绑定新租约。 */
    void HomingTargetMoved(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);
    /** 关闭原目标订阅并按显式策略保留最后位置或结束，绝不重捕获。 */
    bool RetireHomingTarget();

    UPROPERTY(Transient)
    TWeakObjectPtr<AMyProjectile> Projectile;
    UPROPERTY(Transient)
    FAbilityDeliverySettings RetainedSettings;
    UPROPERTY(Transient)
    FDamageEffectParams RetainedDamage;
    FAbilityDeliveryRequest ActiveRequest;
    FAbilityDeliveryResult Result;
    FVector PreviousLocation = FVector::ZeroVector;
    float Elapsed = 0.f;
    float Travelled = 0.f;
    bool bStarted = false;
    bool bFinished = false;
    bool bContactClaimed = false;
    bool bProcessingMovement = false;
    FAbilityProjectileMotionState MotionState;
    TArray<FAbilityTargetToken> PhaseImpacts;
    uint64 ImpactSequence = 0;
    FVector LastAimPoint = FVector::ZeroVector;
    bool bHomingTargetRetired = false;
    TWeakObjectPtr<USceneComponent> ObservedHomingRoot;
};
