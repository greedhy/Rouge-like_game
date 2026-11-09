// 调用链：Stander动画进度/图时间 -> 共享分段位移 -> 原胶囊扫掠、地面投影和房间限制。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "Components/ActorComponent.h"
#include "AbilityMovementNodeExecutions.generated.h"
class FAbilityNodeRegistry;
class ACharacter;
class UCharacterMovementComponent;

/** 每个角色拥有一份移动占用；弱执行对象+唯一租约阻止并行图和迟到清理互相改写移动模式。 */
UCLASS(Transient)
class TEST_25D_API UAbilityMovementOwnershipComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static UAbilityMovementOwnershipComponent* FindOrCreate(ACharacter* Character);
    /** 只查询仍属于当前来源生命的实际占用；不创建组件、不改变移动模式或回收旧状态。 */
    static bool HasActiveClaim(const ACharacter* Character);
    /** 只捕获所有权与原状态；调用方随后控制移动。已有占用或外部MOVE_None明确拒绝。 */
    FGuid TryAcquire(UObject* Requester, const FAbilitySourceToken& Source, FString& Error);
    /** 只释放匹配执行对象/租约；原来源生命仍有效才恢复状态，旧生命不修改新生命。 */
    bool Release(UObject* Requester, const FGuid& Lease, bool bRestoreAsFalling = false);

private:
    void ReleaseOwnedState(bool bRestoreAsFalling);
    TWeakObjectPtr<UObject> ExecutionOwner;
    TWeakObjectPtr<UCharacterMovementComponent> Movement;
    FAbilitySourceToken CapturedSource;
    FGuid OwnershipLease;
    uint8 SavedMovementMode = 0;
    bool bSavedDesiredRotation = false;
};

struct FAbilityCharacterMoveState
{
    float AppliedDistance = 0.f;
    float JumpOffset = 0.f;
    bool bBlocked = false;
};
namespace AbilityMovementActions
{
    /** DesiredDistance由原DashFraction或图进度计算；AppliedDistance避免重复应用已走过的距离。 */
    TEST_25D_API void GroundDash(const FAbilitySourceToken& Source, const FVector& Forward, float DesiredDistance,
                                 int32 OriginRoom, FAbilityCharacterMoveState& State,
                                 const TFunction<bool()>& IsCurrent = {});
    /** 正负位移及抛物线跳跃沿用原武器事件；受阻后停止水平推进，但仍允许落回地面。 */
    TEST_25D_API void WeaponArc(const FAbilitySourceToken& Source, const FVector& Forward, float Distance, float Height,
                                float BeforeFraction, float AfterFraction, int32 OriginRoom,
                                FAbilityCharacterMoveState& State, const TFunction<bool()>& IsCurrent = {});
} // namespace AbilityMovementActions

UENUM(BlueprintType)
enum class EAbilityCharacterMoveProfile : uint8
{
    GroundDash,
    WeaponArc
};
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityCharacterMoveNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAbilityCharacterMoveProfile Profile = EAbilityCharacterMoveProfile::GroundDash;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Distance = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Height = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = .3f;
    virtual bool Validate(FString& Error) const override;
};
UCLASS(Transient)
class TEST_25D_API UAbilityCharacterMoveNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float Delta) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityCharacterMoveNodeConfig> Settings;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityMovementOwnershipComponent> MovementOwnership;
    FGuid MovementLease;
    FAbilityCharacterMoveState State;
    float Elapsed = 0.f;
    int32 OriginRoom = INDEX_NONE;
    bool bBlockedPublished = false;
};
namespace AbilityMovementNodes
{
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
}
