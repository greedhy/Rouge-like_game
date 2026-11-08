/**
 * 传统 Actor 鼠标任务：GA 创建并绑定 ValidData -> ReadyForActivation/Activate -> 本地 SendMouseCursorData；远端 ASC TargetData 委托 -> OnTargetDataReplicatedCallback -> GA 目标校验。请求由 SpecHandle/PredictionKey 关联。该旧任务没有自定义 OnDestroy 或主动 EndTask，生命周期由 AbilityTask/GAS 管理；本文件不实现额外句柄清理。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "test_25d/test_25d.h"

/** 通过 NewAbilityTask 创建属于 OwningAbility 的任务，调用者绑定 ValidData 后激活。 */
UTargetDataUnderMouse* UTargetDataUnderMouse::CreateTargetDataUnderMouse(UGameplayAbility* OwningAbility)
{
    UTargetDataUnderMouse* MyObj = NewAbilityTask<UTargetDataUnderMouse>(OwningAbility);
    return MyObj;
}

/** 本地控制角色立即采集鼠标；远端绑定目标复制委托并消费已到达回调，否则标记等待远端数据。 */
void UTargetDataUnderMouse::Activate()
{
    const bool bIsLocallyControlled = Ability->GetCurrentActorInfo()->IsLocallyControlled();
    if (bIsLocallyControlled)
    {
        SendMouseCursorData();
    }
    else
    {
        const FGameplayAbilitySpecHandle SpecHandle = GetAbilitySpecHandle();
        const FPredictionKey PredictionKey = GetActivationPredictionKey();

        AbilitySystemComponent.Get()
            ->AbilityTargetDataSetDelegate(SpecHandle, PredictionKey)
            .AddUObject(this, &UTargetDataUnderMouse::OnTargetDataReplicatedCallback);

        const bool bCalledDelegate =
            AbilitySystemComponent.Get()->CallReplicatedTargetDataDelegatesIfSet(SpecHandle, PredictionKey);
        if (!bCalledDelegate)
        {
            SetWaitingOnRemotePlayerData();
        }
    }
}

/** 预测窗口内按 ECC_Target 查询鼠标，构造 GAS 单命中数据并发送服务器；可广播时记录攻击目标并通知本地 GA。 */
void UTargetDataUnderMouse::SendMouseCursorData()
{
    FScopedPredictionWindow ScopedPrediction(AbilitySystemComponent.Get(), true);

    APlayerController* PC = Ability->GetCurrentActorInfo()->PlayerController.Get();

    FHitResult CursorHit;
    PC->GetHitResultUnderCursor(ECC_Target, false, CursorHit);

    FGameplayAbilityTargetDataHandle DataHandle;
    FGameplayAbilityTargetData_SingleTargetHit* Data = new FGameplayAbilityTargetData_SingleTargetHit();
    Data->HitResult = CursorHit;
    DataHandle.Add(Data);
    FGameplayTag ApplicationTag;

    AbilitySystemComponent->ServerSetReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey(),
                                                          DataHandle, ApplicationTag,
                                                          AbilitySystemComponent->ScopedPredictionKey);

    if (ShouldBroadcastAbilityTaskDelegates())
    {
        if (auto* Attack = Cast<UMyDamageGameplayAbility>(Ability))
            Attack->SetOffensiveAttackTarget(CursorHit.GetActor());
        ValidData.Broadcast(DataHandle);
    }
}

/** 保留共享数据副本后消费 ASC 缓存，记录攻击目标并广播 ValidData；业务目标可信性由接收 GA 继续校验。 */
void UTargetDataUnderMouse::OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle,
                                                           FGameplayTag ApplicationTag)
{
    // Keep shared target data alive after consuming the ASC's cached handle.
    const FGameplayAbilityTargetDataHandle LocalData = DataHandle;
    AbilitySystemComponent->ConsumeClientReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey());
    if (ShouldBroadcastAbilityTaskDelegates())
    {
        if (auto* Attack = Cast<UMyDamageGameplayAbility>(Ability))
        {
            const FHitResult* Hit =
                LocalData.Num() > 0 && LocalData.Get(0) ? LocalData.Get(0)->GetHitResult() : nullptr;
            Attack->SetOffensiveAttackTarget(Hit ? Hit->GetActor() : nullptr);
        }
        ValidData.Broadcast(LocalData);
    }
}
