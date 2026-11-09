// 调用链：原蓝图前端 -> SpawnProjectiles收集等级/插槽/追踪参数 -> 共享动作；描述保持原规则。

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyProjectileSpell.h"
#include "MyFireBolt.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyFireBolt : public UMyProjectileSpell
{
    GENERATED_BODY()
public:
    virtual FString GetDescription(int32 Level) override;
    virtual FString GetNextLevelDescription(int32 Level) override;

    UFUNCTION(BlueprintCallable)
    void SpawnProjectiles(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, bool bOverridePitch,
                          float PitchOverride, AActor* HomingTarget);

protected:
    UPROPERTY(EditDefaultsOnly, Category = "FireBolt")
    float ProjectileSpread = 90.f;

    UPROPERTY(EditDefaultsOnly, Category = "FireBolt")
    int32 MaxNumProjectiles = 5;

    UPROPERTY(EditDefaultsOnly, Category = "FireBolt")
    float HomingAccelerationMin = 1600.f;

    UPROPERTY(EditDefaultsOnly, Category = "FireBolt")
    float HomingAccelerationMax = 3200.f;

    UPROPERTY(EditDefaultsOnly, Category = "FireBolt")
    bool bLaunchHomingProjectiles = true;
};
