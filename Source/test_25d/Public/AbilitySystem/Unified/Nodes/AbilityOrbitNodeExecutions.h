// 调用链：Orbit GA/Schema2图 -> 注册 Channel.OrbitingFireBolts -> 补充/费用/攻击订阅 -> 原轨道弹Actor。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "AbilityOrbitNodeExecutions.generated.h"

class AMyOrbitingFireBolt;
class UOrbitingFireBoltConfig;
class UPrototypeCastContext;
class UGameplayAbility;
class UAbilityOrbitNodeExecution;
class FAbilityNodeRegistry;

/** 可选GA桥接：用于排除自身攻击信号和保持既有诊断接口，不承载玩法状态。 */
UCLASS(Transient)
class TEST_25D_API UAbilityOrbitContext : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UGameplayAbility> SourceAbility;
    FGameplayTagContainer AbilityTags;
    TWeakObjectPtr<UAbilityOrbitNodeExecution> Execution;
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityOrbitNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UOrbitingFireBoltConfig> OrbitSettings;
    virtual bool Validate(FString& Error) const override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityOrbitNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    int32 GetLiveCount() const;
    int32 GetLaunchedCount() const;
    AActor* GetAttackTarget() const;
    /** 激活费和每秒费用共用现有瞬时 GAS Mana 修改，失败不改属性。 */
    static bool SpendMana(UAbilitySystemComponent* ASC, AActor* Avatar, float Level, float Amount);

private:
    bool ValidateSource();
    bool IsValidEnemy(AActor* Target) const;
    void Replenish();
    void PayUpkeep();
    void LaunchNext();
    void ArrangeOrbit();
    void ClearTarget();
    void HandleAttack(UGameplayAbility* Ability, AActor* Target);
    void HandleDeactivate(const FGameplayTag& Tag);
    void BoltHit(AActor* Target);
    UFUNCTION()
    void OnTargetDied(AActor* Target);
    UFUNCTION()
    void OnOwnerDied(AActor* Avatar);
    UFUNCTION()
    void OnOwnerDestroyed(AActor* Avatar);
    UPROPERTY()
    TObjectPtr<UOrbitingFireBoltConfig> Settings;
    UPROPERTY()
    TObjectPtr<UAbilityOrbitContext> Binding;
    UPROPERTY()
    TObjectPtr<UPrototypeCastContext> LifeContext;
    UPROPERTY()
    TArray<TObjectPtr<AMyOrbitingFireBolt>> Bolts;
    TOptional<FAbilityTargetToken> AttackTargetLife;
    FVector LastTargetLocation = FVector::ZeroVector;
    FTimerHandle SpawnTimer;
    FTimerHandle ManaTimer;
    FTimerHandle LaunchTimer;
    FDelegateHandle AttackHandle;
    FDelegateHandle DeactivateHandle;
    uint64 HitSequence = 0;
};

namespace AbilityOrbitNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
