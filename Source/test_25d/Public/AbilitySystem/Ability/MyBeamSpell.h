// 调用链：GAS 原生激活 -> 鼠标/动画 Notify -> Channel.Electrocute 图；松开最短时长 -> 策略最终命中和结束。

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"
#include "TimerManager.h"
#include "MyBeamSpell.generated.h"

class UAbilityGraphInstance;
class UAbilityElectrocuteContext;
class UAnimMontage;

/** 原蓝图可调用函数保留兼容；激活入口直接驱动注册策略，不再执行蓝图自定义伤害计时器。 */
UCLASS()
class TEST_25D_API UMyBeamSpell : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UMyBeamSpell();
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled) override;
    UFUNCTION(BlueprintCallable)
    void StoreMouseDataInfo(const FHitResult& HitResult);

    UFUNCTION(BlueprintCallable)
    void StoreOwnerVariables();

    UFUNCTION(BlueprintCallable)
    void TraceFirstTarget(const FVector& BeamTargetLocation);

    UFUNCTION(BlueprintCallable)
    void StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets);

    UFUNCTION(BlueprintImplementableEvent)
    void PrimaryTargetDied(AActor* DeadActor);

    UFUNCTION(BlueprintImplementableEvent)
    void AdditionalTargetDied(AActor* DeadActor);

    /**
 * 技能结束时调用
 */
    UFUNCTION(BlueprintCallable)
    void OnEndAbility(TArray<AActor*> AdditionalTargets);

protected:
    /** 原 GA_Electrocute 的 Montage_Electrocute/DamageDeltaTime/MiniSpellTIme 由资产迁移脚本复制。 */
    UPROPERTY(EditDefaultsOnly, Category = "Beam|Unified")
    TObjectPtr<UAnimMontage> ElectrocuteMontage;
    UPROPERTY(EditDefaultsOnly, Category = "Beam|Unified")
    float ElectrocuteDamageInterval = .1f;
    UPROPERTY(EditDefaultsOnly, Category = "Beam|Unified")
    float MinimumElectrocuteTime = 1.f;
    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    FVector MouseHitLocation;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<AActor> MouseHitActor;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<APlayerController> OwnerPlayerController;

    UPROPERTY(BlueprintReadWrite, Category = "Beam")
    TObjectPtr<ACharacter> OwnerCharacter;

    UPROPERTY(EditDefaultsOnly, Category = "Beam")
    int32 MaxNumShockTargets = 10;

private:
    UFUNCTION()
    void OnElectrocuteTargetData(const FGameplayAbilityTargetDataHandle& Data);
    UFUNCTION()
    void OnElectrocuteNotify(FGameplayEventData Payload);
    UFUNCTION()
    void OnElectrocuteReleased(float TimeHeld);
    UFUNCTION()
    void OnElectrocuteMontageInterrupted();
    UFUNCTION()
    void OnElectrocuteMontageCompleted();
    void FinishElectrocute();
    void CleanupPredictedPresentation();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityGraphInstance> ElectrocuteGraph;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityElectrocuteContext> ElectrocuteContext;
    FAbilityNodeInput ElectrocuteInput;
    FDelegateHandle GraphFinishedHandle;
    FTimerHandle MinimumHoldTimer;
    bool bTargetConfirmed = false;
    bool bChannelStarted = false;
    bool bReleaseReceived = false;
    bool bPredictedMovementLocked = false;
    bool bCooldownCommitted = false;
};
