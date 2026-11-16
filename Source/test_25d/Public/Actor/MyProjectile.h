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
