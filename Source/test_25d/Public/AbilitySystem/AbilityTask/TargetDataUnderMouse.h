/**
 * 传统 Actor 鼠标任务：GA 创建并绑定 ValidData -> ReadyForActivation/Activate -> 本地 SendMouseCursorData；远端 ASC TargetData 委托 -> OnTargetDataReplicatedCallback -> GA 目标校验。请求由 SpecHandle/PredictionKey 关联。该旧任务没有自定义 OnDestroy 或主动 EndTask，生命周期由 AbilityTask/GAS 管理；本文件不实现额外句柄清理。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "TargetDataUnderMouse.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMouseTargetDataSignature, const FGameplayAbilityTargetDataHandle&,
                                            DataHandle);

/**
 * 
 */
UCLASS()
class TEST_25D_API UTargetDataUnderMouse : public UAbilityTask
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
              meta = (DisplayName = "TargetDataUnderMouse", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility",
                      BlueprintInternalUseOnly = true))
    /** 通过 NewAbilityTask 创建属于 OwningAbility 的任务，调用者绑定 ValidData 后激活。 */
    static UTargetDataUnderMouse* CreateTargetDataUnderMouse(UGameplayAbility* OwningAbility);

    UPROPERTY(BlueprintAssignable)
    FMouseTargetDataSignature ValidData;

private:
    /** 本地控制角色立即采集鼠标；远端绑定目标复制委托并消费已到达回调，否则标记等待远端数据。 */
    virtual void Activate() override;
    /** 预测窗口内按 ECC_Target 查询鼠标，构造 GAS 单命中数据并发送服务器；可广播时记录攻击目标并通知本地 GA。 */
    void SendMouseCursorData();

    void OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle,
                                        FGameplayTag ApplicationTag);
};
