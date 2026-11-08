/**
 * 减速链：APureMassRuntime::Hit -> Apply；Mass 转交 UPureMassSubsystem，Actor 按需创建组件 -> RefreshMovementSpeed/GetMultiplier。Actor 条目记录独立过期时间，取最强减速，不让较弱效果延长旧强效果。组件由 Enemy 持有，Tick/Clear/EndPlay 处理过期、死亡和对象池 Lease 变化；不独立复制 Entries。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffect.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Batch6Slow.generated.h"
UCLASS()
class TEST_25D_API UBatch6SlowEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 构造 Infinite 标记 GE；实际倍率由组件条目和 Enemy 速度刷新逻辑读取。 */
    UBatch6SlowEffect();
};
UCLASS()
class TEST_25D_API UBatch6SlowComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 构造可 Tick 组件，但初始关闭；首次 Apply 后才启用过期检查。 */
    UBatch6SlowComponent();
    /** 服务器验证参数及混合目标；Mass 调用 ApplySlow，Actor 创建/复用组件，合并同倍率过期时间并刷新速度。 */
    static bool Apply(UWorld* World, AActor* Caster, const FPureMassCombatTarget& Target, float Multiplier,
                      float Duration);
    /** 移动速度刷新查询：校验 Enemy 当前 Lease/存活状态，从未过期条目取最小倍率，否则返回 1。 */
    static float GetMultiplier(const AActor* Actor);
    /** 权威端逐帧校验 Lease、睡眠和死亡；删除过期条目，全部清空时 Clear，否则必要时刷新速度。 */
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    /** 引擎销毁入口：Clear 移除本组件标记并还原速度计算，再调用父类。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FSlow
    {
        float Multiplier;
        double Expires;
    };
    TArray<FSlow> Entries;
    int64 Lease = 0;
    FActiveGameplayEffectHandle Marker;
    /** 禁用 Tick、回收标记 GE 句柄、清空条目并刷新 Enemy 速度；用于过期/复用/销毁。 */
    void Clear();
};
