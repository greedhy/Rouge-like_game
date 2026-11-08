// 调用链：动画通知 -> Send -> ASC::HandleGameplayEvent -> 时间轴任务校验来源/去重。
#include "AbilitySystem/Timeline/AnimNotify_AbilityTimelineEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

// 普通通知由动画系统批量分发，使用其原始实例上下文，不能从当前活跃蒙太奇猜来源。
void UAnimNotify_AbilityTimelineEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                              const FAnimNotifyEventReference& EventReference)
{
    const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
    // 首版只支持直接放在Montage上的通知，避免Sequence局部时间与Montage时间混用。
    if (Context && Cast<UAnimMontage>(Animation) && EventReference.GetNotify())
    {
        Send(MeshComp, CastChecked<UAnimMontage>(Animation), Context->MontageInstanceID,
             EventReference.GetNotify()->GetTriggerTime());
    }
}

// 分支点由引擎提供精确播放实例和Montage时间，仍走相同的事件入口。
void UAnimNotify_AbilityTimelineEvent::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
    if (Cast<UAnimMontage>(Payload.SequenceAsset) && Payload.NotifyEvent)
    {
        Send(Payload.SkelMeshComponent, CastChecked<UAnimMontage>(Payload.SequenceAsset), Payload.MontageInstanceID,
             Payload.NotifyEvent->GetTriggerTime());
    }
}

// 上下文被同步GameplayEvent持有；不缓存到Notify资产，不跨网络传送。
void UAnimNotify_AbilityTimelineEvent::Send(USkeletalMeshComponent* MeshComp, UAnimMontage* SourceMontage,
                                            int32 InstanceId, float SourcePosition)
{
    AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
    auto* Anim = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
    // 最后一帧Queued Notify派发前，源实例可能已移除。来源必须取自通知队列携带的
    // 动画资产和实例ID，而不是查询当前活跃实例；后者会丢尾帧通知或误归属下一次播放。
    if (!Owner || !Owner->HasAuthority() || !Owner->GetWorld()->IsGameWorld() || !Anim || !SourceMontage ||
        InstanceId == INDEX_NONE || !EventTag.IsValid())
    {
        return;
    }
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
    if (!ASC)
    {
        return;
    }
    auto* Context = NewObject<UAbilityTimelineNotifyContext>();
    Context->Montage = SourceMontage;
    Context->AnimInstance = Anim;
    Context->MontageInstanceId = InstanceId;
    Context->SourcePosition = SourcePosition;
    FGameplayEventData Event;
    Event.EventTag = EventTag;
    Event.Instigator = Owner;
    Event.Target = Owner;
    Event.OptionalObject = Context;
    ASC->HandleGameplayEvent(EventTag, &Event);
}
