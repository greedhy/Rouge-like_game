// 调用链：ASC被动装备激活 -> 绑定本次停用委托 -> 会话关闭先解绑 -> GAS结束；重复装备不会累积监听。

#include "AbilitySystem/Ability/MyPassiveAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
void UMyPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    const auto Session = GetExecutionSession();
    if (!IsActive() || !Session || !Session->IsOpen())
    {
        return;
    }

    if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetAvatarActorFromActorInfo())))
    {
        const FDelegateHandle Listener =
            MyASC->DeactivatePassiveAbility.AddUObject(this, &UMyPassiveAbility::ReceiveDeactivate);
        const TWeakObjectPtr<UMyAbilitySystemComponent> WeakASC(MyASC);
        Session->AddCleanup(
            [WeakASC, Listener]()
            {
                if (auto* ASC = WeakASC.Get())
                {
                    ASC->DeactivatePassiveAbility.Remove(Listener);
                }
            });
    }
}

void UMyPassiveAbility::ReceiveDeactivate(const FGameplayTag& AbilityTag)
{
    if (IsActive() && AbilityTags.HasTagExact(AbilityTag))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}
