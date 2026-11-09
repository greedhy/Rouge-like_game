// 调用链：哥布林/萨满动画前端或统一图 -> 共享动作 -> 原弹体、角色召唤、导航与导演生命周期。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilityEnemyActionNodeExecutions.generated.h"
class USlingAttackConfig;
class UShamanSummonConfig;
class AEnemySlingProjectile;
class FAbilityNodeRegistry;

namespace AbilityEnemyActions
{
    /** 预先检查发射口静态遮挡；原弹体在FinishSpawning前接收来源、伤害及速度。 */
    TEST_25D_API AEnemySlingProjectile* FireSling(const FAbilitySourceToken& Source, const USlingAttackConfig& Config,
                                                  const FVector& Aim, const FDamageEffectParams& Damage,
                                                  const TFunction<void(AEnemySlingProjectile*)>& OnPrepared = {});
    /** 调用现有角色级召唤动作；不重新创建另一套导航、遭遇计数或随从所有权。 */
    TEST_25D_API int32 SummonShamanMinions(const FAbilitySourceToken& Source, const UShamanSummonConfig& Config);
} // namespace AbilityEnemyActions

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityEnemySlingNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<USlingAttackConfig> Settings;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityShamanSummonNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UShamanSummonConfig> Settings;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityEnemySlingNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UFUNCTION()
    void ProjectileDestroyed(AActor* Actor);
    TWeakObjectPtr<AEnemySlingProjectile> Projectile;
};
UCLASS(Transient)
class TEST_25D_API UAbilityShamanSummonNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
};
namespace AbilityEnemyActionNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
