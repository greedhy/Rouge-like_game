// 调用链：统一图 -> 护盾节点 -> 当前玩家 Avatar 组件 -> MyAttributeSet 吸收伤害。
// 服务器管理数值和到期，组件只复制剩余值，由各客户端创建和清理护盾表现。
#pragma once

#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "AbilityBarrier.generated.h"

class FAbilityNodeRegistry;
class UNiagaraComponent;

/** 短时普通护盾跟随承受伤害的 Avatar；结束施法后仍有效，离开该 Avatar 后回收。 */
UCLASS()
class TEST_25D_API UAbilityBarrierComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAbilityBarrierComponent();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

    /** 服务器合并同类护盾，取较高的剩余值和较迟的到期时刻。 */
    static void Grant(AActor& Avatar, float Capacity, float Duration);
    /** 服务器消耗当前护盾并返回由生命承担的剩余伤害。 */
    float ConsumeDamage(float Damage);

private:
    /** 到期和耗尽走同一条清理路径。 */
    void Clear();
    /** 外部死亡流程绕过伤害结算时立即丢弃剩余护盾。 */
    UFUNCTION()
    void OnOwnerDied(AActor* DeadActor);
    /** 复制到达时更新客户端表现，服务器本地玩家也通过这条路径显示。 */
    UFUNCTION()
    void OnRep_Remaining();
    void RemoveVisual();

    UPROPERTY(ReplicatedUsing = OnRep_Remaining)
    float Remaining = 0.f;
    double ExpiresAt = 0.0;
    FTimerHandle ExpiryTimer;
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> Visual;
};

/** 自施放消费 Origin；命中版只消费有结算结果的直接伤害。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class TEST_25D_API UAbilityBarrierNodeConfig : public UAbilityNodeConfig
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Barrier")
    float Capacity = 40.f;
    UPROPERTY(EditAnywhere, Category = "Barrier")
    float Duration = 4.f;
    UPROPERTY(EditAnywhere, Category = "Barrier")
    bool bOnDamage = false;

    virtual bool Validate(FString& Error) const override;
    virtual bool MatchesType(FName TypeId) const override;
    virtual bool TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error) override;
};

/** 完成护盾授予后立即结束节点；长效状态交给 Avatar 负责。 */
UCLASS(Transient)
class TEST_25D_API UAbilityBarrierNodeExecution : public UAbilityNodeExecution
{
    GENERATED_BODY()

public:
    virtual bool Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Configuration,
                       const FAbilityNodeInput& InInput, FString& Error) override;
};

namespace AbilityBarrierNodes
{
    /** 注册自施放与伤后反馈各自的有限图输入协议。 */
    TEST_25D_API void Register(FAbilityNodeRegistry& Registry);
} // namespace AbilityBarrierNodes
