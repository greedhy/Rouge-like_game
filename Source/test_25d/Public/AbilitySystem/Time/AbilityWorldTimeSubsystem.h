// 调用链：GA/注册节点/蓝图请求 -> 世界租约 -> 不膨胀时钟/最慢包络 -> 世界倍率及参与者补偿 -> 释放恢复。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "AbilityWorldTimeSubsystem.generated.h"

/** 只保存配置；动作秒不受世界慢速影响，游戏暂停时冻结。 */
USTRUCT(BlueprintType)
struct TEST_25D_API FAbilityWorldSlowSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Scale = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BlendIn = .05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Hold = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BlendOut = .15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bCompensateSource = true;
    bool Validate(FString& Error) const;
    double Duration() const;
    float Evaluate(double Elapsed) const;
};

struct FAbilityWorldTimeLease
{
    FGuid Id;
    FAbilitySourceToken Source;
    FAbilityWorldSlowSettings Settings;
    TWeakObjectPtr<UObject> Owner;
    double Started = 0.;
};

struct FAbilityTimeParticipant
{
    TWeakObjectPtr<AActor> Actor;
    TSet<TWeakObjectPtr<AActor>> Beneficiaries;
    float Baseline = 1.f;
    float LastWritten = 1.f;
};

/** 独立时间机制，不激活其他GA；每个调用者只拥有自己的租约。首版仅Standalone。 */
UCLASS()
class TEST_25D_API UAbilityWorldTimeSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 提交费用前只读检查来源、容量及外部时间所有权；Acquire仍会复核。 */
    bool CanAcquire(const FAbilitySourceToken& Source, const FAbilityWorldSlowSettings& Settings, const UObject* Owner,
                    FString& Error) const;
    FGuid Acquire(const FAbilitySourceToken& Source, const FAbilityWorldSlowSettings& Settings, UObject* Owner,
                  FString& Error);
    /** Blueprint调用入口现场捕获权威来源；客户端、自报无效来源和不支持网络模式均失败关闭。 */
    UFUNCTION(BlueprintCallable, Category = "Ability|World Time")
    FGuid RequestWorldSlow(AActor* Source, UObject* LifetimeOwner, const FAbilityWorldSlowSettings& Settings);
    UFUNCTION(BlueprintCallable, Category = "Ability|World Time")
    void Release(FGuid Handle);
    /** 同Pawn倒地/复活等不广播普通死亡的路径显式撤销旧生命所有时间请求。 */
    UFUNCTION(BlueprintCallable, Category = "Ability|World Time")
    void CancelSource(AActor* Source);
    UFUNCTION(BlueprintPure, Category = "Ability|World Time")
    bool IsActive(FGuid Handle) const;
    UFUNCTION(BlueprintPure, Category = "Ability|World Time")
    bool HasActiveRequests() const;
    UFUNCTION(BlueprintPure, Category = "Ability|World Time")
    double GetGameplayRealTime() const;
    /** 节点区分自然到期与外部取消；只保留最近256个自然完成标识。 */
    bool HasExpired(FGuid Handle) const;
    /** 显式表现参与入口；例如独立特效Actor。不会改变GE/TimerManager的世界时钟。 */
    UFUNCTION(BlueprintCallable, Category = "Ability|World Time")
    void RegisterCompensatedActor(AActor* Actor, AActor* Beneficiary);
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickableWhenPaused() const override
    {
        return true;
    }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual TStatId GetStatId() const override;
#if WITH_DEV_AUTOMATION_TESTS
    void SampleClockForTesting(double AbsoluteRealSeconds, bool bPaused);
#endif
private:
    bool IsRequestLive(const FAbilityWorldTimeLease& Request) const;
    void PruneRequests();
    void SampleClock(double AbsoluteRealSeconds, bool bPaused);
    void Recompute();
    void RestoreParticipants();
    void RemoveUnusedDeathBindings();
    void OnActorSpawned(AActor* Actor);
    void DiscoverProjectile(AActor* Actor);
    UFUNCTION()
    void SourceDied(AActor* Actor);

    TArray<FAbilityWorldTimeLease> Requests;
    TArray<FAbilityTimeParticipant> Participants;
    TArray<TWeakObjectPtr<AActor>> SpawnCandidates;
    TSet<TWeakObjectPtr<AActor>> DeathBindings;
    TArray<FGuid> Expired;
    FDelegateHandle SpawnHandle;
    double LastRealTime = 0.;
    double GameplayRealTime = 0.;
    float WorldBaseline = 1.f;
    float LastWorldWritten = 1.f;
    bool bClockInitialized = false;
    bool bOwnsWorld = false;
    bool bClosing = false;
};
