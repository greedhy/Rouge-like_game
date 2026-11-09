// 调用链：共享投射物动作延迟构造并捕获来源生命 -> 原碰撞伤害 -> 原命中特效/复制销毁。

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
    virtual bool IsValidOverlap(AActor* OtherActor);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<USphereComponent> Sphere;

    UPROPERTY()
    TObjectPtr<USceneComponent> HomingTargetSceneComponent;

    virtual void Destroyed() override;

protected:
    virtual void BeginPlay() override;
    virtual void OnHit();
    virtual bool ShouldPlayImpactOnDestroy() const
    {
        return true;
    }

private:
    TOptional<FAbilitySourceToken> ExecutionSource;
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
