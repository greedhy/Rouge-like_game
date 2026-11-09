// 调用链：Grux/Shroom原生动画前端或注册图 -> 独立区域动作 -> 原地面几何、伤害、护甲和预警服务。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilityEnemySpecialNodeExecutions.generated.h"
class ATelegraphedEnemyCharacter;
class FAbilityNodeRegistry;
class UAbilityMovementOwnershipComponent;

/** 原孢子槽位协议：同一时刻至多一次，卡顿跳过错过槽位，结束边界不结算。 */
struct TEST_25D_API FShroomPulseClock
{
    double Started = 0, Ends = 0;
    float Period = 1;
    int32 NextSlot = 0;
    void Reset(double Now, float InPeriod, float Duration);
    bool Consume(double Now);
};

UENUM(BlueprintType)
enum class EAbilityEnemySpecialKind : uint8
{
    Charge,
    Spore,
    Roar
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityEnemySpecialNodeConfig : public UAbilityTelegraphedContactNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityEnemySpecialKind Kind = EAbilityEnemySpecialKind::Charge;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Period = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Armor = 25.f;
    /** 独立图节点先使用原敌人预警宿主；已匹配动画的GA直接使用动作，保留原预警时序。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WarningSeconds = .35f;
    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
};

enum class EAbilityEnemyActionState : uint8
{
    Running,
    Completed,
    Failed
};

/** 真正的每次调用状态；没有GA引用。调用方提供阶段校验和伤害观察者，内部持有位移/脉冲/去重状态。 */
class TEST_25D_API FAbilityEnemyAreaAction
{
public:
    bool Start(const FAbilityNodeInput& InInput, const UAbilityEnemySpecialNodeConfig& Config,
               FAbilityCharacterActionCallbacks InCallbacks, double Now, FString& Error);
    EAbilityEnemyActionState Advance(double Now);
    void Stop();
    bool IsStopped() const
    {
        return bStopped;
    }

private:
    bool Current(AActor*& Avatar) const;
    void Pulse(const TOptional<TPair<FVector, FVector>>& Segment = {});
    EAbilityEnemyActionState AdvanceCharge(double Now);
    FAbilityNodeInput Input;
    FAbilityCharacterActionCallbacks Callbacks;
    EAbilityEnemySpecialKind Kind = EAbilityEnemySpecialKind::Charge;
    FStoneGolemAttackTuning Shape;
    TArray<FVector> Surface;
    TArray<int32> Triangles;
    FShroomPulseClock Clock;
    FVector StartPosition = FVector::ZeroVector;
    FVector PreviousPosition = FVector::ZeroVector;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float Duration = .5f;
    float Yaw = 0.f;
    double Started = 0;
    int32 ChargeRoom = INDEX_NONE;
    bool bStopped = false;
};

namespace AbilityEnemySpecialActions
{
    /** 与原冲锋预警相同：静态碰撞、地面及房间逐段裁剪，写回实际走廊长度。 */
    TEST_25D_API bool PlanCharge(const FAbilitySourceToken& Source, const FVector& Center, float Yaw,
                                 FStoneGolemAttackTuning& Shape);
    /** 复用Grux自己的唯一护甲GE；验证失败或同步生命变更会撤回本次句柄。 */
    TEST_25D_API FActiveGameplayEffectHandle ApplyRoar(const FAbilitySourceToken& Source, float Armor, float Duration);
    TEST_25D_API void RemoveRoar(const FAbilitySourceToken& Source, FActiveGameplayEffectHandle Handle);
} // namespace AbilityEnemySpecialActions

UCLASS(Transient)
class TEST_25D_API UAbilityEnemySpecialNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    bool CurrentPresentation() const;
    void BeginAction();
    UPROPERTY()
    TObjectPtr<UAbilityEnemySpecialNodeConfig> Settings;
    UPROPERTY()
    TObjectPtr<UAbilityMovementOwnershipComponent> MovementOwnership;
    FGuid MovementLease;
    TWeakObjectPtr<ATelegraphedEnemyCharacter> Presenter;
    TSharedPtr<FAbilityEnemyAreaAction> Action;
    FActiveGameplayEffectHandle ArmorHandle;
    int32 Serial = 0, Lease = 0;
    double Elapsed = 0;
    double ActionStartedAt = 0;
    bool bActionStarted = false;
};

namespace AbilityEnemySpecialNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
