// 测试调用链：自动化夹具授予本GA -> RunTimeline -> 记录Notify/结束；游戏角色不授予此测试类型。
#pragma once
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Timeline/AbilityTask_RunTimeline.h"
#include "AbilityTimelineTestAbility.generated.h"

/** 真实ASC/蒙太奇任务的测试接收器，计数仅存在于当前测试对象，不复制。 */
UCLASS()
class UAbilityTimelineTestAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 服务器实例化，避免污染CDO。 */
    UAbilityTimelineTestAbility();
    /** 测试在激活前设置独立的瞬态蒙太奇和配置。 */
    UPROPERTY()
    TObjectPtr<UAnimMontage> TestMontage;
    UPROPERTY()
    TObjectPtr<UAbilityTimelineDefinition> TestDefinition;
    UPROPERTY()
    TObjectPtr<UAbilityTask_RunTimeline> Task;
    int32 Hits = 0;
    int32 TailHits = 0;
    int32 Finishes = 0;
    bool bWasInterrupted = false;
    float Elapsed = 0;
    float HitTime = -1;
    float LastSourcePosition = 0;
    bool bKeepAbilityActive = false;
    /** 绑定回调后开始播放，不生成实际伤害。 */
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Data) override;
    /** 记录通知时刻，并验证取消后不会继续进入这个回调。 */
    UFUNCTION()
    void Receive(FGameplayTag Tag, FGameplayEventData Payload);
    /** 观察结束帧是否仍交付最终源位置，防止表驱动末帧事件遗漏。 */
    UFUNCTION()
    void Position(float SourcePosition);
    /** 记录结束且立即EndAbility，覆盖同步回调重入。 */
    UFUNCTION()
    void Finish(bool bInterrupted);
};
