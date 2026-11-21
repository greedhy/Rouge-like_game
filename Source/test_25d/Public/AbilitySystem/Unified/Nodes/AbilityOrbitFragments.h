// 调用链：原环绕前端 -> 独立库存/攻击订阅 -> 原生轨道投递 -> 可替换接触效果；仍使用同一图根。
#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Unified/AbilityExecutionParameters.h"
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilityOrbitFragments.generated.h"
class UOrbitingFireBoltConfig;
class UAbilityGraphDefinition;
class FAbilityNodeRegistry;
enum class EAbilityProjectileContactDisposition : uint8;

/** 单独的实例化参数避免运行时配方修改共享库存DA；原前端配置仍由其自己的资产管理。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNativeOrbitConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Instanced)
    TObjectPtr<UOrbitingFireBoltConfig> Parameters;
    virtual bool Validate(FString& Error) const override;
    virtual bool SupportsSustainedRoot(FString& Error) const override;
};

/** 冻结库存配置和当前根身份，不在只读帧中放Actor、定时器或可变库存状态。 */
UCLASS(Transient)
class TEST_25D_API UAbilityNativeOrbitFrame : public UAbilityExecutionParameters
{
    GENERATED_BODY()
public:
    /** 为准确库存建立不可变身份和值快照；失败不把Actor/句柄塞进配置。 */
    static UAbilityNativeOrbitFrame* Create(UObject* Outer, FGuid Root, const FAbilitySourceToken& Source,
                                            const UOrbitingFireBoltConfig& Settings);

private:
    UPROPERTY(Transient)
    TObjectPtr<UOrbitingFireBoltConfig> FrozenSettings;
};

UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityNativeOrbitDeliveryConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()
};

/** 复用原库存、费用、目标与退回逻辑，只把真实发射与命中效果交给图。 */
UCLASS(Transient)
class TEST_25D_API UAbilityNativeOrbitHostExecution : public UAbilityOrbitNodeExecution
{
    GENERATED_BODY()
public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
    virtual void Stop(EAbilityGraphEndReason Reason) override;
    /** 只接受本宿主持有的准确帧/弹体和当前原生命，重复发射不取得新库存。 */
    bool LaunchPrepared(const UAbilityNativeOrbitFrame& Frame, const FAbilityTargetToken& Target);

protected:
    virtual bool PrepareDelivery(AMyOrbitingFireBolt* Bolt) override;
    virtual void DispatchLaunch(AMyOrbitingFireBolt* Bolt, const FAbilityTargetToken& Target) override;

private:
    /** 实际碰撞是唯一入口；先播放原终结表现，再在同一接触边界结算外部payload。 */
    EAbilityProjectileContactDisposition Contact(AMyOrbitingFireBolt* Bolt, const FAbilityTargetToken& Target,
                                                 FVector Position);
    UFUNCTION()
    void InventoryDestroyed(AActor* Actor);
    UPROPERTY(Transient)
    TMap<TObjectPtr<AMyOrbitingFireBolt>, TObjectPtr<UAbilityNativeOrbitFrame>> Frames;
    TMap<TWeakObjectPtr<AActor>, FGuid> PhysicalLeases;
    uint64 ContactSequence = 0;
};

UCLASS(Transient)
class TEST_25D_API UAbilityNativeOrbitDeliveryExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()
public:
    /** 消费本根已准备库存；不再次生成Actor、不再次扣费，运动仍由原生轨道Actor维护。 */
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityOrbitFragments
{
    /** 由真实配置复原库存、维持费用、逐个发射与接触效果；不在构造定义时生成Actor。 */
    TEST_25D_API UAbilityGraphDefinition* Build(UObject* Outer, const UOrbitingFireBoltConfig& Settings);
    /** 注册实际库存/投递策略，共用原Actor与GAS通路。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityOrbitFragments
