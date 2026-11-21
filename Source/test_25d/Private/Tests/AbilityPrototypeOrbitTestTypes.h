// 调用链：测试真实ASC激活/CommitAbility -> 生产提交标记和通知门；本测试GA本身没有伤害或目标任务。
#pragma once
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilityPrototypeOrbitTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class UPrototypeOrbitCommittedTestAbility : public UMyDamageGameplayAbility
{
    GENERATED_BODY()
public:
    UPrototypeOrbitCommittedTestAbility()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
        SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Attack"))));
    }
    /** GAS先完成PreActivate再进入这里；测试显式调用真正CommitAbility，保持活动实例等待提交。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override
    {
    }
};
