/**
 * 混合目标异步任务：GA Create/ReadyForActivation -> Activate；本地鼠标反投影与混合 Sweep -> ServerSetReplicatedTargetData；服务器 ASC 委托 -> Receive -> GA TargetReady。Actor 目标使用 Actor 且 Item=INDEX_NONE，实体目标使用 Item 且 Actor 为空；服务端 GA 仍须复核。任务由 GAS 管理，EndTask/OnDestroy 解绑数据委托。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "PureMass/PureMassTargetData.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Components/PrimitiveComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

/** 通过 NewAbilityTask 创建归属指定 GA 的任务；调用者绑定 Ready 后才能 ReadyForActivation。 */
UPureMassTargetData* UPureMassTargetData::Create(UGameplayAbility* Owner)
{
    return NewAbilityTask<UPureMassTargetData>(Owner);
}

/** 本地控制端在预测窗口采集目标并发送 GAS TargetData；远端绑定按 Spec/PredictionKey 匹配的委托并处理已缓存数据。 */
void UPureMassTargetData::Activate()
{
    if (!Ability || !AbilitySystemComponent.IsValid())
    {
        EndTask();
        return;
    }
    const auto* Info = Ability->GetCurrentActorInfo();
    if (!Info->IsLocallyControlled())
    {
        DataDelegate =
            AbilitySystemComponent->AbilityTargetDataSetDelegate(GetAbilitySpecHandle(), GetActivationPredictionKey())
                .AddUObject(this, &ThisClass::Receive);
        if (!AbilitySystemComponent->CallReplicatedTargetDataDelegatesIfSet(GetAbilitySpecHandle(),
                                                                            GetActivationPredictionKey()))
            SetWaitingOnRemotePlayerData();
        return;
    }
    FScopedPredictionWindow Prediction(AbilitySystemComponent.Get(), true);
    FGameplayAbilityTargetDataHandle Data;
    auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
    Data.Add(Hit);
    FVector Start, Direction;
    auto* PC = Info->PlayerController.Get();
    if (PC && PC->DeprojectMousePositionToWorld(Start, Direction))
    {
        const FVector End = Start + Direction * 100000.f;
        float EntityTime = 1.f;
        FHitResult Wall;
        const auto Target =
            PureMassCombat::Sweep(GetWorld(), Info->AvatarActor.Get(), Start, End, 0.f, EntityTime, Wall);
        FVector Location;
        if (Target.IsSet() && PureMassCombat::Resolve(GetWorld(), Info->AvatarActor.Get(), Target, Location))
        {
            if (AActor* Actor = Target.Actor.Get())
                Hit->HitResult = FHitResult(Actor, Cast<UPrimitiveComponent>(Actor->GetRootComponent()), Location,
                                            FVector::UpVector);
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Item = Target.EntityId;
            Hit->HitResult.ImpactPoint = Location;
            Hit->HitResult.Location = Hit->HitResult.ImpactPoint;
        }
        else if (Wall.bBlockingHit)
        {
            Hit->HitResult.bBlockingHit = true;
            Hit->HitResult.Item = INDEX_NONE;
            Hit->HitResult.ImpactPoint = Wall.ImpactPoint;
            Hit->HitResult.Location = Wall.ImpactPoint;
        }
    }
    if (!Info->IsNetAuthority())
        AbilitySystemComponent->ServerSetReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey(),
                                                              Data, FGameplayTag(),
                                                              AbilitySystemComponent->ScopedPredictionKey);
    if (ShouldBroadcastAbilityTaskDelegates())
        Ready.Broadcast(Data);
    EndTask();
}

/** 服务器目标数据到达回调：先复制共享 Handle 再消费 ASC 缓存，广播 Ready 后 EndTask。 */
void UPureMassTargetData::Receive(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag)
{
    const FGameplayAbilityTargetDataHandle Copy = Data;
    AbilitySystemComponent->ConsumeClientReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey());
    if (ShouldBroadcastAbilityTaskDelegates())
        Ready.Broadcast(Copy);
    EndTask();
}

/** 任务结束或 GA 结束时移除自己保存的 DataDelegate，再调用父类，避免残留监听。 */
void UPureMassTargetData::OnDestroy(bool bAbilityEnded)
{
    if (AbilitySystemComponent.IsValid())
        AbilitySystemComponent->AbilityTargetDataSetDelegate(GetAbilitySpecHandle(), GetActivationPredictionKey())
            .Remove(DataDelegate);
    Super::OnDestroy(bAbilityEnded);
}
