// 调用链：Scope原参数 -> 独立库存/攻击订阅 -> PreparedInventory发射 -> 真Actor或轻量运动 -> 外部payload。
// 不是完整旧Driver宏；库存不结算伤害，投递不复制第二份技能数值。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "AbilityPrototypeOrbitNodes.generated.h"

class FAbilityNodeRegistry;
class UAbilityPrototypeParameterFrame;
class UAbilityPrototypeOrbitDeliveryNodeExecution;
class AMyOrbitingFireBolt;
class UGameplayAbility;
enum class EAbilityProjectileContactDisposition : uint8;

/** 一次发射的不可变凭据；没有Actor、计时器或可变库存，只强持同一份原型参数及步骤事实。 */
UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeOrbitLaunchFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    static UAbilityPrototypeOrbitLaunchFrame* Create(UObject* Outer, UAbilityGraphInstance& Owner,
                                                     UAbilityPrototypeParameterFrame& InParameters, FGuid InSlot,
                                                     FVector InPosition, float InDelta,
                                                     const FAbilitySourceToken& Source);
    const UAbilityPrototypeParameterFrame* GetParameters() const;
    FGuid GetSlot() const;
    FVector GetStartPosition() const;
    float GetLaunchDelta() const;

private:
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Parameters;
    FGuid Slot;
    FVector StartPosition = FVector::ZeroVector;
    float LaunchDelta = 0.f;
};

/** 每调用库存状态；Actor由World持有，这里只用弱身份和准确物理额度，避免延长World寿命。 */
struct FAbilityPrototypeOrbitSlot
{
    FGuid Id;
    FGuid PhysicalLease;
    TWeakObjectPtr<AMyOrbitingFireBolt> Actor;
    TWeakObjectPtr<UAbilityPrototypeOrbitDeliveryNodeExecution> Delivery;
    FVector Position = FVector::ZeroVector;
    bool bLaunched = false;
    bool bLaunchPending = false;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeOrbitInventoryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityPrototypeOrbitDeliveryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeOrbitInventoryNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 安装每次调用的库存、来源订阅和原profile费用时钟；不直接伤害。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** 只按原profile补充/选择库存；Launch保留准确原生命及本次准备凭据。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 精确解绑本调用的订阅、库存和资源，不取消其它施法库存。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 只读真实库存（包含飞行中资源），供诊断/行为测试；不从表现副本推算。 */
    int32 GetInventoryCount() const;
    /** 只读已发射库存；返航和终结由准确资源所有者更新。 */
    int32 GetLaunchedCount() const;

private:
    friend class UAbilityPrototypeOrbitDeliveryNodeExecution;
    void HandleActorAttack(UGameplayAbility* Ability, AActor* Target);
    void HandleMixedAttack(UGameplayAbility* Ability, FPureMassCombatTarget Target);
    void SetTarget(FPureMassCombatTarget Target);
    bool Replenish();
    bool Launch(FAbilityPrototypeOrbitSlot& Slot, float Delta);
    void ArrangeAndAdvance(float Delta);
    void RefreshPresentation();
    void CompleteInventory();
    FAbilityPrototypeOrbitSlot* FindSlot(FGuid Id);
    void ReturnSlot(FGuid Id);
    void ConsumeSlot(FGuid Id);
    bool IsActorProfile() const;
    UFUNCTION()
    void ResourceDestroyed(AActor* Actor);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeParameterFrame> Frame;
    UPROPERTY(Transient)
    TObjectPtr<AActor> PresentationHost;
    UPROPERTY(Transient)
    TMap<FGuid, TObjectPtr<UAbilityPrototypeOrbitLaunchFrame>> LaunchFrames;
    TArray<FAbilityPrototypeOrbitSlot> Slots;
    TMap<TWeakObjectPtr<AActor>, FGuid> ActorSlots;
    FAbilityPrototypePresentationFrame Visual;
    TOptional<FAbilityTargetToken> Current;
    FDelegateHandle AttackHandle;
    FVector Anchor = FVector::ZeroVector;
    float Age = 0.f;
    float RefillClock = 0.f;
    float LaunchClock = 0.f;
    float ManaClock = 0.f;
    bool bClosing = false;
};

UCLASS(Transient)
class TEST_25D_API UAbilityPrototypeOrbitDeliveryNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 消费本根准确库存凭据，不重新生成第二颗弹体或再次支付补充费用。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    /** Actor沿实际PM；Mixed沿原轻量分段，接触后仅发布原生命到外部效果。 */
    virtual void Advance(float DeltaSeconds) override;
    /** 释放准确本次投递；库存返航与销毁按原profile分别管理。 */
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    friend class UAbilityPrototypeOrbitInventoryNodeExecution;
    /** Inventory只按原逆序排列推进物理资源；图仍唯一管理调用/队列/取消，不手工激活其它图。 */
    void AdvanceLightResource(float Delta);
    EAbilityProjectileContactDisposition ActorContact(const FAbilityTargetToken& Target, FVector Position,
                                                      FVector Direction);
    bool Contact(const FAbilityTargetToken& Target, FVector Position, FVector Direction);
    void Complete(bool bConsume);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityPrototypeOrbitLaunchFrame> Ticket;
    TWeakObjectPtr<UAbilityPrototypeOrbitInventoryNodeExecution> Inventory;
    TWeakObjectPtr<AMyOrbitingFireBolt> Actor;
    FAbilityTargetToken Target;
    FVector Position = FVector::ZeroVector;
    float FlightAge = 0.f;
    bool bCompleting = false;
};

namespace AbilityPrototypeOrbitNodes
{
    /** 库存Launch为PreparedInventory；投递Contact为LiveTarget，允许替换为纯状态效果。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityPrototypeOrbitNodes
