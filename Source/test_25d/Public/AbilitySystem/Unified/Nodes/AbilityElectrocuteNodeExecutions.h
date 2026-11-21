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
struct FAbilityDamageResult;

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
    /** 每个真实宿主登记自身；弱集合不延长图的寿命，清理只能移除所属执行。 */
    void AttachExecution(UAbilityElectrocuteNodeExecution* Host);
    void DetachExecution(UAbilityElectrocuteNodeExecution* Host);
    bool HasOtherSourcePresentation(const UAbilityElectrocuteNodeExecution* Host) const;
    bool HasOtherTargetPresentation(const UAbilityElectrocuteNodeExecution* Host,
                                    const FAbilityTargetToken& Target) const;
    /** 输入松开/最短施法时间届满只请求一次正常收束，不直接执行伤害。 */
    void RequestCompletion();

private:
    TArray<TWeakObjectPtr<UAbilityElectrocuteNodeExecution>> ActiveExecutions;
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
    /** 原生雷链提供身份，旧图默认空。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag EquipmentAbilityTag;
    /** 原资产以武器 TipSocket 的半径10扫掠修正主目标；独立图可明确保留传入目标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTraceFromWeapon = true;
    virtual bool Validate(FString& Error) const override;
    /** 持续图显式准入：该宿主仅拥有一个定时器和有上限的目标/表现集合。 */
    virtual bool SupportsSustainedRoot(FString& Error) const override;
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

protected:
    friend class UAbilityElectrocuteContext;
    /** 原Timer/费用流程只启动一次批效果；细宿主交给同图continuation，旧宏继续同步执行。 */
    virtual void BeginPeriodEffects();
    virtual bool IsPeriodPending() const;
    /** 共用目标生命/主目标失效/空地自命中迭代；实际GE可同步结束宿主，下一目标前必须复核。 */
    void WalkPeriodTargets(TFunctionRef<void(AActor*, const FAbilityTargetToken&, FVector)> Action);
    void EndPeriodEffects();
    /** 原清理拥有表现/委托释放；旧宏立即Final，细宿主只冻结待Final的原生命。 */
    virtual void EmitFinalDamage(const FAbilityTargetToken& Life, FVector Location);
    /** 原宏在同步Final之后提交；细宿主在同一执行器的Final continuation之后调用该默认动作。 */
    virtual void FinishCompletion();
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
    FDamageEffectParams ResolvedDamage;
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
    /** 原周期：目标ASC创建上下文/Thunder Spec，独立于完整Final伤害参数。 */
    TEST_25D_API FAbilityDamageResult ApplyPeriod(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                                  FVector Location, const FDamageEffectParams& Params, float Level,
                                                  bool bSourceSelf = false);
    /** 原Final：来源ASC应用完整参数，force使用来源到目标方向，不掷额外击退随机数。 */
    TEST_25D_API FAbilityDamageResult ApplyFinal(const FAbilitySourceToken& Source, const FAbilityTargetToken& Target,
                                                 const FDamageEffectParams& Params);
} // namespace AbilityElectrocuteNodes
