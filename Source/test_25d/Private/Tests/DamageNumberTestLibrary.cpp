/** 主要调用链：PIE验证脚本 -> ScheduleDamagePair -> 世界定时器 -> ApplyDamageEffect两次 -> 伤害数字反馈。只接受编辑器权威PIE世界。 */
#include "DamageNumberTestLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
// 将伤害延迟到世界Tick执行，避免编辑器脚本执行保护影响ASC/RPC行为。
bool UDamageNumberTestLibrary::ScheduleDamagePair(const FDamageEffectParams& Params)
{
#if WITH_EDITOR
    UWorld* World =
        IsValid(Params.SourceAbilitySystemComponent) ? Params.SourceAbilitySystemComponent->GetWorld() : nullptr;
    if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() == NM_Client ||
        !IsValid(Params.TargetAbilitySystemComponent))
        return false;
    // Native world-tick execution exercises normal ASC and RPC dispatch, unlike
    // running the damage directly inside the editor Python execution guard.
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(
        Handle,
        FTimerDelegate::CreateWeakLambda(Params.SourceAbilitySystemComponent,
                                         [Params]()
                                         {
                                             if (IsValid(Params.TargetAbilitySystemComponent))
                                                 for (int32 I = 0; I < 2; ++I)
                                                     UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
                                         }),
        .1f, false);
    return true;
#else
    return false;
#endif
}
