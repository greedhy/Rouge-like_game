// 调用链：蓝图目标/蒙太奇前端 -> SpawnProjectile -> 共享Actor投射物动作；不重复Commit或伤害。

#pragma once

#include "CoreMinimal.h"
#include "MyDamageGameplayAbility.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "Actor/MyProjectile.h"
#include "MyProjectileSpell.generated.h"

/**
 * 
 */
class GameplayEffect;
UCLASS()
class TEST_25D_API UMyProjectileSpell : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
protected:
    /** 必须仍属于本次激活的来源生命，不能在Avatar重绑定后重新授权旧动画通知。 */
    bool CaptureProjectileSource(FAbilitySourceToken& Source) const;
    // Shared deferred-spawn path for active FireBolt and persistent orbiting fireballs.
    AMyProjectile* CreateProjectileDeferred(TSubclassOf<AMyProjectile> Class, const FTransform& Transform);
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    UFUNCTION(BlueprintCallable, Category = "Projectile")
    void SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, const FName SocketName,
                         const bool bOverridePitch = false, const float PitchOverride = 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AMyProjectile> ProjectileClass;

    UPROPERTY(EditDefaultsOnly)
    int32 NumProjectiles = 5;
};
