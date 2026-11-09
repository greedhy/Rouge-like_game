// 主要调用链：公共预警 GA → 召唤配置快照 → 接触窗口调用角色 Summon；施法与爆发直接复用公共范围攻击。
#include "AbilitySystem/Ability/ShamanEnemyAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityEnemyActionNodeExecutions.h"
#include "Character/ShamanEnemyCharacter.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShCast, "Abilities.Shaman.Cast");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShBurst, "Abilities.Shaman.Burst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShSummon, "Abilities.Shaman.Summon");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShCastCD, "Cooldown.Shaman.Cast");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShBurstCD, "Cooldown.Shaman.Burst");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ShSummonCD, "Cooldown.Shaman.Summon");
UShamanCastAbility::UShamanCastAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_ShCast));
    CooldownIdentity = TAG_ShCastCD;
}
UShamanBurstAbility::UShamanBurstAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_ShBurst));
    CooldownIdentity = TAG_ShBurstCD;
}
UShamanSummonAbility::UShamanSummonAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_ShSummon));
    CooldownIdentity = TAG_ShSummonCD;
}
// 验证召唤配置与现存随从数量，再检查公共攻击条件。
bool UShamanSummonAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                              const FGameplayTagContainer* Source, const FGameplayTagContainer* Target,
                                              FGameplayTagContainer* Relevant) const
{
    const auto* Actor = Info ? Cast<AShamanEnemyCharacter>(Info->AvatarActor.Get()) : nullptr;
    const auto* Config = Cast<UShamanSummonConfig>(AttackConfig);
    return Actor && Config && Config->ValidateSummon() && Actor->GetOwnedMinionCount() < Config->MaxAlive &&
           Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}
// 确认攻击序号和租约有效后显示命中，并让角色使用快照召唤随从。
void UShamanSummonAbility::ResolveHit()
{
    auto* Actor = Cast<AShamanEnemyCharacter>(Golem.Get());
    if (!Actor || !Actor->IsAttackCurrent(Serial, Lease))
        return;
    Actor->ShowImpact(Serial, Center);
    FAbilitySourceToken Source;
    FString Error;
    if (SummonSnapshot && AbilityExecutionIdentity::CaptureSource(Actor, Source, Error))
    {
        AbilityEnemyActions::SummonShamanMinions(Source, *SummonSnapshot);
    }
}
// 复制本次召唤配置，避免预警期间配置变化影响结果。
void UShamanSummonAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    const auto* Config = Cast<UShamanSummonConfig>(AttackConfig);
    SummonSnapshot = Config ? DuplicateObject<UShamanSummonConfig>(Config, this) : nullptr;
    Super::ActivateAbility(Handle, Info, Activation, Event);
}
