// 调用链：Beam Notify/任意 Schema2 图 -> Channel.Electrocute -> 周期费用/伤害、死亡回调、最终命中与资源清理。
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "GameplayEffectTypes.h"
#include "TimerManager.h"
#include "AbilityElectrocuteNodeExecutions.generated.h"

class FAbilityNodeRegistry;
class UAbilityElectrocuteNodeExecution;
class USceneComponent;

/** GA 仅提供费用接口和松开信号；计时、目标集合、伤害及清理状态全部归执行策略。 */
UCLASS(Transient)
class TEST_25D_API UAbilityElectrocuteContext : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<AActor> AimActor;
    TFunction<bool()> CommitPeriodCost;
    TFunction<void()> CommitEndCooldown;
    TWeakObjectPtr<UAbilityElectrocuteNodeExecution> Execution;
    /** 输入松开/最短施法时间届满只请求一次正常收束，不直接执行伤害。 */
    void RequestCompletion();
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityElectrocuteNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DamageInterval = .1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SecondaryRadius = 850.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxAdditionalTargets = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag LoopCue;
    /** 原资产以武器 TipSocket 的半径10扫掠修正主目标；独立图可明确保留传入目标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTraceFromWeapon = true;
    virtual bool Validate(FString& Error) const override;
};

USTRUCT()
struct FAbilityElectrocuteTarget
{
    GENERATED_BODY()
    FAbilityTargetToken Life;
    UPROPERTY()
    FGameplayCueParameters Cue;
    bool bReleased = false;
    bool bCueAdded = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityElectrocuteNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 松开、费用不足与主目标死亡共用正常收束；重复调用不追加最终命中或冷却。 */
    void CompleteChannel();

private:
    void Period();
    void ApplyPeriodDamage(AActor* Actor, const FAbilityTargetToken& Life, FVector Location);
    bool AddTarget(AActor* Actor, USceneComponent* Attach, FVector Location);
    void ReleaseTarget(FAbilityElectrocuteTarget& Target, bool bFinalDamage);
    void Cleanup(bool bFinalDamage);
    bool SourceStillCurrent() const;
    UFUNCTION()
    void TargetDied(AActor* Actor);
    UPROPERTY()
    TObjectPtr<UAbilityElectrocuteNodeConfig> Settings;
    UPROPERTY()
    TObjectPtr<UAbilityElectrocuteContext> Binding;
    UPROPERTY()
    TArray<FAbilityElectrocuteTarget> Targets;
    UPROPERTY()
    FGameplayCueParameters EmptyTargetCue;
    TWeakObjectPtr<AActor> PrimaryActor;
    TWeakObjectPtr<UWorld> TimerWorld;
    FTimerHandle PeriodTimer;
    uint64 PeriodSequence = 0;
    bool bEmptyTargetCue = false;
    bool bCleaned = false;
    bool bCompleting = false;
    bool bSourcePresentationStarted = false;
};

namespace AbilityElectrocuteNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
