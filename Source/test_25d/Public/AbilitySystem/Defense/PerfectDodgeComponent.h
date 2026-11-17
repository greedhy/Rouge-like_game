// 调用链：翻滚GA/Defense.PerfectDodge -> 角色防御组件 -> GroundDash/攻击前拦截/独立世界时间租约。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "PerfectDodgeComponent.generated.h"
class UNiagaraSystem;
class UNiagaraComponent;

USTRUCT(BlueprintType)
struct TEST_25D_API FPerfectDodgeSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Distance = 420.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MoveDuration = .30f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PerfectWindow = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Invulnerability = .24f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Recovery = .42f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RewardLock = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityWorldSlowSettings Reward;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> DodgeEffect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UNiagaraSystem> PerfectEffect;
    bool Validate(FString& Error) const;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDodgeFinished, FGuid, bool);

/** 每个Pawn一份；正常GA结束不撤销已赢得奖励，死亡/撤销Spec/换Avatar则立即关闭本生命。 */
UCLASS(Transient)
class TEST_25D_API UPerfectDodgeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPerfectDodgeComponent();
    static UPerfectDodgeComponent* FindOrCreate(ACharacter* Character);
    static bool CanStart(ACharacter* Character, bool bPreflightGroundHandoff = false);
    /** Commit前预约位移；预约本身不开窗口，不移动。失败必须不消耗冷却。 */
    FGuid Reserve(const FAbilitySourceToken& Source, UObject* Requester, const FPerfectDodgeSettings& Settings,
                  const FVector& Direction, FString& Error);
    /** Commit成功后固定见证体和动作起点；旧预约或失效来源不能打开窗口。 */
    bool Begin(FGuid Execution);
    void Finish(FGuid Execution, bool bCancelled);
    /** Spec撤销/死亡/换生命共用清理入口，幂等释放本组件持有的资源。 */
    void InvalidateLife();
    bool IsRunning(FGuid Execution) const;
    bool WasPerfect(FGuid Execution) const;
    bool WasCancelled(FGuid Execution) const;
    FGuid GetRewardLease() const
    {
        return RewardLease;
    }
    FDodgeFinished OnFinished;

    /** 仅在真实攻击生效时调用；Contains复用攻击几何，不从预警或距离推断攻击发生。 */
    static void ObserveActorAttack(const FAbilitySourceToken& AttackSource, FGuid AttackId, const FVector& Origin,
                                   const TFunction<bool(const FVector&, float, float)>& Contains,
                                   AActor* OnlyTarget = nullptr, double EarliestSampleTime = -1.);
    /** 当前身体已确认命中时调用；返回true必须跳过整次GE及命中Debuff/击退。 */
    static bool TryDodgeActorHit(AActor* Target, const FAbilitySourceToken& AttackSource, FGuid AttackId,
                                 bool bAllowPerfect = true);
    /** Mass既有AttackPlayer入口调用；以真实EntityHandle校验来源，保留原IncomingDamage GE。 */
    static bool TryDodgeMassHit(AActor* Target, int32 EntityId, FMassEntityHandle Entity, const FVector& Origin,
                                FGuid AttackId);
    /** 供Mass实际接触时读取短时起点，范围判定本身仍由原Mass攻击代码完成。 */
    bool GetWitness(FVector& Center, float& Radius, float& HalfHeight) const;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION()
    void OnOwnerDeath(AActor* Actor);
    bool Current() const;
    double Now() const;
    bool WindowOpen(float Duration) const;
    bool VisibleFrom(const FVector& Origin, const FVector& Center, AActor* Ignore) const;
    void RewardAttack(FGuid AttackId);
    void ReleaseMovement();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityMovementOwnershipComponent> MovementOwner;
    TWeakObjectPtr<UObject> ExecutionOwner;
    TWeakObjectPtr<UAbilitySystemComponent> BlockedASC;
    FAbilitySourceToken Source;
    UPROPERTY(Transient)
    FPerfectDodgeSettings Settings;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> DodgeVisual;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> PerfectVisual;
    FGuid ExecutionId;
    FGuid LastPerfectId;
    FGuid LastFinishedId;
    uint32 LifeGeneration = 1;
    uint32 ExecutionGeneration = 0;
    bool bLastCancelled = true;
    bool bInvalidatingLife = false;
    FGuid MovementLease;
    FGuid RewardLease;
    FVector Direction = FVector::ForwardVector;
    FVector WitnessCenter = FVector::ZeroVector;
    float WitnessRadius = 0.f;
    float WitnessHalfHeight = 0.f;
    double StartTime = 0.;
    double NextRewardTime = 0.;
    int32 OriginRoom = INDEX_NONE;
    bool bBegun = false;
    bool bWindowsClosed = false;
    FAbilityCharacterMoveState MoveState;
    TArray<FGuid> RewardedAttacks;
};
