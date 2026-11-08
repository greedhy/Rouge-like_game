/**
 * 混合目标异步任务：GA Create/ReadyForActivation -> Activate；本地鼠标反投影与混合 Sweep -> ServerSetReplicatedTargetData；服务器 ASC 委托 -> Receive -> GA TargetReady。Actor 目标使用 Actor 且 Item=INDEX_NONE，实体目标使用 Item 且 Actor 为空；服务端 GA 仍须复核。任务由 GAS 管理，EndTask/OnDestroy 解绑数据委托。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#pragma once
#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "PureMassTargetData.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPureMassTargetReady, const FGameplayAbilityTargetDataHandle&, Data);

/** GAS 混合命中载荷：实体目标使用 Item 且 Actor 为空；Actor 目标使用 Actor 且 Item=INDEX_NONE。 */
UCLASS()
class TEST_25D_API UPureMassTargetData : public UAbilityTask
{
    GENERATED_BODY()
public:
    /** 通过 NewAbilityTask 创建归属指定 GA 的任务；调用者绑定 Ready 后才能 ReadyForActivation。 */
    static UPureMassTargetData* Create(UGameplayAbility* OwningAbility);
    UPROPERTY(BlueprintAssignable)
    FPureMassTargetReady Ready;
    /** 本地控制端在预测窗口采集目标并发送 GAS TargetData；远端绑定按 Spec/PredictionKey 匹配的委托并处理已缓存数据。 */
    virtual void Activate() override;
    /** 任务结束或 GA 结束时移除自己保存的 DataDelegate，再调用父类，避免残留监听。 */
    virtual void OnDestroy(bool bAbilityEnded) override;

private:
    FDelegateHandle DataDelegate;
    /** 服务器目标数据到达回调：先复制共享 Handle 再消费 ASC 缓存，广播 Ready 后 EndTask。 */
    void Receive(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag);
};
