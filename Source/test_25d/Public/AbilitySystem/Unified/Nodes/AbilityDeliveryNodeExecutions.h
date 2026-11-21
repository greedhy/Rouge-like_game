// 调用链：Schema2 Origin -> 可等待的投递 -> Hit/DamageResolved -> 后继图；Stop取消所属投递。
#pragma once
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "AbilityDeliveryNodeExecutions.generated.h"

class FAbilityNodeRegistry;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityDeliveryNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    /** 显式消费原生准备帧；不回读装备或重做等级限制。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bUseNativeLaunchParameters = false;
    /** 只覆盖完成实际BP构造后的世界碰撞半径；未选中时保持类自身参数。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOverridePreparedCollisionRadius = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAbilityDeliverySettings Delivery;
    virtual bool Validate(FString& Error) const override;
    /** 仅接受已有实际消费/测试的CollisionRadius；未实现运动/寿命轴明确拒绝。 */
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

UCLASS(Transient)
class TEST_25D_API UAbilityDeliveryNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& Input, FString& Error) override;
    virtual void Advance(float DeltaSeconds) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;

private:
    /** 每次真实接触独立发布历史快照；同一个投递可以发布多个Hit。 */
    void ImpactReceived(const FAbilityDeliveryImpact& Impact);
    /** 仅发布投递终态；不会重复结算或重发最后一次命中。 */
    void Delivered(const FAbilityDeliveryResult& Result);
    UPROPERTY(Transient)
    TObjectPtr<UAbilityDeliveryInstance> Delivery;
};

namespace AbilityDeliveryNodes
{
    /** Await与逐跳共用同根准备帧解析；只复制冻结值，不重新读取装备或重复解析等级。 */
    TEST_25D_API bool ResolveDeliverySettings(const UAbilityDeliveryNodeConfig& Config, const FAbilityNodeInput& Input,
                                              FGuid Root, FAbilityDeliverySettings& OutSettings, FString& Error);
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityDeliveryNodes
