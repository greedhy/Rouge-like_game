// 调用链：ASC 被动装备 -> 监听本次停用 -> UnifiedGraphAbility -> 统一图与 GE 句柄清理。
#include "AbilitySystem/Unified/MageGraphAbility.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeteorCinders, "Abilities.Mage.MeteorCinders");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeteorCindersCooldown, "Cooldown.Mage.MeteorCinders");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RimeSigil, "Abilities.Mage.RimeSigil");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RimeSigilCooldown, "Cooldown.Mage.RimeSigil");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PiercingTempest, "Abilities.Mage.PiercingTempest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PiercingTempestCooldown, "Cooldown.Mage.PiercingTempest");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ArcaneSentry, "Abilities.Mage.ArcaneSentry");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ArcaneSentryCooldown, "Cooldown.Mage.ArcaneSentry");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ArcaneMeditation, "Abilities.Mage.ArcaneMeditation");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ElementalWard, "Abilities.Mage.ElementalWard");

/** 显示资产描述，费用和冷却从实际提交的 GE 读取。 */
FString UMageGraphAbility::GetDescription(int32 Level)
{
    FString CostLine;
    if (GetCostGameplayEffect() || GetCooldownGameplayEffect())
    {
        CostLine = FString::Printf(TEXT("<Small>MP %.0f | CD %.1fs</>\n"), GetManaCost(Level), GetCooldown(Level));
    }
    return FString::Printf(TEXT("<Title>%s</>\n%s<Default>%s</>"), *SpellName.ToString(), *CostLine,
                           *SpellSummary.ToString());
}

/** 参数采用首批固定数值，升级预览与实际配置一致。 */
FString UMageGraphAbility::GetNextLevelDescription(int32 Level)
{
    return GetDescription(Level);
}

UMagePassiveGraphAbility::UMagePassiveGraphAbility()
{
    TargetMode = EUnifiedAbilityTargetMode::Self;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

/** 在启动统一图前监听 ASC 的被动换槽事件，并将绑定归本次会话持有。 */
void UMagePassiveGraphAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
                                                const FGameplayAbilityActorInfo* Info,
                                                FGameplayAbilityActivationInfo Activation,
                                                const FGameplayEventData* Event)
{
    auto* ASC = Info ? Cast<UMyAbilitySystemComponent>(Info->AbilitySystemComponent.Get()) : nullptr;
    const auto Session = GetExecutionSession();
    if (!ASC || !Info->IsNetAuthority() || !Session || !Session->IsOpen() ||
        TargetMode != EUnifiedAbilityTargetMode::Self || ExecutionLifetime != EAbilityExecutionLifetime::BoundToAbility)
    {
        AbortCast();
        return;
    }
    const FDelegateHandle Listener = ASC->DeactivatePassiveAbility.AddUObject(this, &ThisClass::PassiveDeactivated);
    const TWeakObjectPtr<UMyAbilitySystemComponent> WeakASC(ASC);
    Session->AddCleanup(
        [WeakASC, Listener]()
        {
            if (auto* CurrentASC = WeakASC.Get())
            {
                CurrentASC->DeactivatePassiveAbility.Remove(Listener);
            }
        });
    Super::ActivateAbility(Handle, Info, Activation, Event);
}

/** 只结束与被换出标签相符的能力，其他槽位的 GE 继续保留。 */
void UMagePassiveGraphAbility::PassiveDeactivated(const FGameplayTag& AbilityTag)
{
    if (IsActive() && GetAssetTags().HasTagExact(AbilityTag))
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
    }
}
