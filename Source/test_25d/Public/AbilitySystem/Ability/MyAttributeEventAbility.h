// 调用链：启动被动GA -> 真实ASC GameplayEvent等待 -> 原Tag/等级/SBC规则 -> 共享GE动作 -> 原属性结算。
// 订阅随 World/Avatar/ASC/池租约存活，可先于 RunId 初始化；权威事件各自捕获当前 Run 的严格动作来源。
#pragma once
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityExecutionIdentity.h"
#include "MyAttributeEventAbility.generated.h"

UCLASS()
class TEST_25D_API UMyAttributeEventAbility : public UMyGameplayAbility
{
    GENERATED_BODY()
public:
    UMyAttributeEventAbility();
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    TSubclassOf<UGameplayEffect> EventEffectClass;
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    FGameplayTag ListenTag;
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    bool bMatchEventExactly = false;
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    float EventEffectLevel = 1.f;
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    bool bInitializeListedTags = true;
    UPROPERTY(EditDefaultsOnly, Category = "Attribute Events")
    TArray<FGameplayTag> InitializedEventTags;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                            FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;

protected:
    /** 常驻属性事件订阅属于当前 Avatar 的生命，不属于单次攻击或尚未就绪的 Run。 */
    virtual bool IsExecutionSessionBoundToRun() const override;

private:
    /** 两端检查订阅生命；客户端保持原 ASC 效果权限，不把订阅快照作为权威动作来源。 */
    bool IsEventSourceCurrent() const;
    UFUNCTION()
    void ReceiveAttributeEvent(FGameplayEventData Payload);
    FAbilitySourceToken EventSource;
    bool bAuthoritySource = false;
};
