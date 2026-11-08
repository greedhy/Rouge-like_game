// 调用链：蒙太奇Notify -> 携带实例ID的GameplayEvent -> ASC -> RunTimeline校验 -> GA业务。
// Notify是共享资产对象，绝不在其中存储当前角色/施法；事件上下文仅供同步派发期间读取。
#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "AnimNotify_AbilityTimelineEvent.generated.h"

class UAnimMontage;
class UAnimInstance;

/** 本地服务器通知来源。不是网络消息；禁止把客户端构造的对象当服务器命中凭证。 */
UCLASS()
class TEST_25D_API UAbilityTimelineNotifyContext : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY()
    TObjectPtr<UAnimMontage> Montage;
    UPROPERTY()
    TObjectPtr<UAnimInstance> AnimInstance;
    int32 MontageInstanceId = INDEX_NONE;
    float SourcePosition = 0;
};

UCLASS(meta = (DisplayName = "Ability Timeline Event"))
class TEST_25D_API UAnimNotify_AbilityTimelineEvent : public UAnimNotify
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
    FGameplayTag EventTag;
    /** 支持普通Queued通知，只接受带Montage实例上下文的播放。 */
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                        const FAnimNotifyEventReference& EventReference) override;
    /** 支持Montage中的BranchingPoint，不把实例ID丢失到普通事件里。 */
    virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;

private:
    /** 在权威端向角色ASC派发，不在动画编辑器预览或客户端施加游戏逻辑。 */
    void Send(USkeletalMeshComponent* MeshComp, UAnimMontage* SourceMontage, int32 InstanceId, float SourcePosition);
};
