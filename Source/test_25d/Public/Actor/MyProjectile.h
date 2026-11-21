// 调用链：共享投射物动作捕获来源生命 -> 接触弹反转移或原伤害 -> 原命中特效/复制销毁。

#pragma once

#include "CoreMinimal.h"
#include "MyAbilityTypes.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "MyProjectile.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectileDamageApplied, AActor*);
enum class EAbilityProjectileContactDisposition : uint8;

UCLASS()
class TEST_25D_API AMyProjectile : public AActor
{
    GENERATED_BODY()

public:
    AMyProjectile();
    // Authority notification after the existing damage pipeline, for composable abilities.
    FProjectileDamageApplied DamageApplied;
    UFUNCTION()
    void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                         int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
    UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = true))
    FDamageEffectParams DamageEffectParams;
    /** 仅权威端保存原施法生命；正常GA结束不影响飞行，死亡/换Avatar/池复用使命中失效。 */
    void SetExecutionSourceToken(const FAbilitySourceToken& Source);
    /** 图投递显式接管伤害；默认未设置时仍走原内部伤害。回调只属于当前来源，反射会先切断旧回调。 */
    void SetGraphDeliveryHandlers(
        TFunction<EAbilityProjectileContactDisposition(const FAbilityTargetToken&, FVector, FVector)> Contact,
        TFunction<void(bool)> Retired, const TArray<FAbilityTargetToken>& IgnoredTargets = {});
    /** 所属投递结束先解绑再销毁，不影响未接管的旧投射物。 */
    void ClearGraphDeliveryHandlers();
    /** 权威初始化选择性世界碰撞半径；复制纯数值供代理按实际组件scale重建，0默认不改变原类形状。 */
    bool SetGraphDeliveryCollisionRadius(float WorldRadius);
    /** Native接触profile保留原Sphere的Actor/墙事件；Mass仍由移动段桥接。 */
    void SetGraphDeliveryBlockingContactHandler(TFunction<void(FVector)> Handler);
    /** 原资产的配置寿命；临时测试World尚未启动Timer时也可只读查询。 */
    float GetConfiguredGraphFlightLifeSpan() const
    {
        return LifeSpan;
    }
    bool IsGraphDeliveryManaged() const
    {
        return bGraphDeliveryManaged;
    }
#if WITH_DEV_AUTOMATION_TESTS
    /** 仅观察终结表现门；测试通过真实组件移动产生接触，不直接调用OnHit。 */
    bool HasTerminalImpactForTesting() const
    {
        return bHit;
    }
#endif
    /** 只有已适配外部投递生命周期的原生类可接管；Blueprint继承不授权专用native生命周期。 */
    virtual bool SupportsGraphDelivery() const;
    /** Actor真实接触或Mass实际扫掠共用一次命中门；这里只交付原生命，不再调用内部伤害。 */
    EAbilityProjectileContactDisposition DispatchGraphDeliveryContact(const FAbilityTargetToken& Target,
                                                                      FVector Location, FVector Direction);
    /** 动态投递命中墙体时复用本弹体原命中表现，服务仍负责唯一终结和销毁。 */
    void ShowGraphDeliveryImpact();
    /** 多次命中只播放接触粒子/声音；终结命中才停止飞行并设置原bHit门。 */
    void PlayGraphDeliveryImpactAt(FVector Location, bool bTerminal);
    void SetGraphDeliveryIgnoredTargets(const TArray<FAbilityTargetToken>& Targets);
    /** 弹反只改变本实例归属/速度；冻结伤害参数与剩余寿命，最多转移一次。 */
    bool TryReflectFrom(const FAbilitySourceToken& Defender);
    bool HasBeenParryReflected() const
    {
        return bParryReflected;
    }
    /** 自行维护飞行目标/施法上下文的子类必须拒绝，直到实现自己的转移合同。 */
    virtual bool SupportsParryReflection() const
    {
        return true;
    }
    virtual bool IsValidOverlap(AActor* OtherActor);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USphereComponent> Sphere;

    UPROPERTY()
    TObjectPtr<USceneComponent> HomingTargetSceneComponent;

    virtual void Destroyed() override;
    virtual void Tick(float DeltaSeconds) override;
    /** 仅复制稳定的托管模式；临时图、回调与生命令牌始终留在服务器。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** 转移已验证但尚未提交新令牌；子类解除旧施法生命委托，不施加伤害或播放终结表现。 */
    virtual void OnParrySourceTransferred(AActor* PreviousSource);
    /** 供专属弹体Tick复核当前归属，死亡/换Avatar/池复用后不能继续攻击。 */
    bool ResolveExecutionSource(AActor*& Source) const;
    /** Tick或真实终止碰撞提交已走过的末段；使用实际位置，不预测穿过阻挡后的路径。 */
    void ObserveTraveledDodgeSegment();
    virtual void BeginPlay() override;
    virtual void OnHit();
    virtual bool ShouldPlayImpactOnDestroy() const
    {
        return true;
    }

private:
    /** 代理收到托管状态后关闭本地预测移动，使用服务器ReplicatedMovement；解除后恢复原弹体模式。 */
    UFUNCTION()
    void OnRep_GraphDeliveryManaged();
    /** 同一纯值在BeginPlay/复制通知重放；不进行接触判定、伤害或终态通知。 */
    bool ApplyGraphDeliveryCollisionRadius();
    /** 多次接触只有稳定位置和类资产参与复制，不复制临时图或命中对象。 */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastGraphDeliveryImpact(FVector Location);
    /** 记录实际已扫过的飞行段；只查询见证体，不制造碰撞或消耗投射物。 */
    void ObserveDodgeSweep(const FVector& Start, const FVector& End);
    UFUNCTION()
    void DodgeSourceDied(AActor* Actor);
    FVector PreviousDodgeLocation = FVector::ZeroVector;
    double PreviousDodgeSampleTime = 0.;
    FGuid DodgeAttackId;
    TOptional<FAbilitySourceToken> ExecutionSource;
    TOptional<FAbilityTargetToken> ReflectedOriginalTarget;
    TOptional<TPair<FVector, FVector>> InvalidatedParrySweep;
    bool bParryReflected = false;
    bool bHit = false;
    UPROPERTY(ReplicatedUsing = OnRep_GraphDeliveryManaged)
    bool bGraphDeliveryManaged = false;
    /** 本实例明确选中的世界cm；反射只转移归属，已选物理形状仍随同一弹体保留。 */
    UPROPERTY(ReplicatedUsing = OnRep_GraphDeliveryManaged)
    float GraphDeliveryCollisionRadius = 0.f;
    bool bGraphDeliveryContactClaimed = false;
    TFunction<EAbilityProjectileContactDisposition(const FAbilityTargetToken&, FVector, FVector)> GraphDeliveryContact;
    TFunction<void(bool)> GraphDeliveryRetired;
    TFunction<void(FVector)> GraphDeliveryBlockingContact;
    TArray<FAbilityTargetToken> GraphDeliveryIgnoredTargets;

    UPROPERTY(EditDefaultsOnly)
    float LifeSpan = 15.f;

    UPROPERTY(EditAnywhere)
    TObjectPtr<UNiagaraSystem> ImpactEffect;

    UPROPERTY(EditAnywhere)
    TObjectPtr<USoundBase> ImpactSound;

    UPROPERTY(EditAnywhere)
    TObjectPtr<USoundBase> LoopingSound;

    UPROPERTY()
    TObjectPtr<UAudioComponent> LoopingSoundComponent;
};
