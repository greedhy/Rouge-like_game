// 调用链：已结算命中 -> UEnemyHitstunAbility 或 UEnemyPoiseBreakAbility -> 各自的标签与计时器。
#include "AbilitySystem/Ability/EnemyCombatReactionAbilities.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace EnemyCombatReactionTags
{
    UE_DEFINE_GAMEPLAY_TAG(Hitstun, "State.Enemy.Hitstun");
    UE_DEFINE_GAMEPLAY_TAG(PoiseBroken, "State.Enemy.PoiseBroken");
    UE_DEFINE_GAMEPLAY_TAG(ReactionAbility, "Abilities.Enemy.Reaction");
}

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EnemyHitstun, "Abilities.Enemy.Reaction.Hitstun");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EnemyPoiseBreak, "Abilities.Enemy.Reaction.PoiseBreak");

UEnemyHitstunAbility::UEnemyHitstunAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    SetAssetTags(FGameplayTagContainer(TAG_EnemyHitstun));
}

bool UEnemyHitstunAbility::ReceiveHit(const FEnemyCombatReactionSettings& Settings)
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (!Settings.bAllowHitstun || Now < GraceEnds)
    {
        return false;
    }
    if (!IsActive())
    {
        return false; // 由宿主发起首击，后续击中才进入此实例。
    }
    if (++Hits >= FMath::Max(1, Settings.HitLimit) || Now >= Started + Settings.HitstunLimitSeconds)
    {
        GraceEnds = Now + FMath::Max(0.f, Settings.EscapeGraceSeconds);
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
        return false;
    }
    UpdateExpiry(Settings, Now);
    return true;
}

bool UEnemyHitstunAbility::ConsumeLaunch()
{
    if (!IsActive() || bUsedLaunch)
    {
        return false;
    }
    bUsedLaunch = true;
    return true;
}

void UEnemyHitstunAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                          FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* Enemy = Info ? Cast<AEnemyCharacter>(Info->AvatarActor.Get()) : nullptr;
    if (!Enemy || !Info->IsNetAuthority() || Enemy->IsDead_Implementation() || Enemy->IsDungeonPoolSleeping())
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    const auto Settings = Enemy->GetCombatReactionSettings();
    const double Now = GetWorld()->GetTimeSeconds();
    if (!Settings.bAllowHitstun || Now < GraceEnds || !CommitAbility(Handle, Info, Activation))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    if (Settings.HitLimit <= 1)
    {
        GraceEnds = Now + FMath::Max(0.f, Settings.EscapeGraceSeconds);
        EndAbility(Handle, Info, Activation, true, false);
        return;
    }
    Hits = 1;
    bUsedLaunch = false;
    Started = Now;
    // 先安装计时器，标签回调可能同步打断敌方行为并结束本次生命。
    UpdateExpiry(Settings, Now);
    bOwnsTag = true;
    Info->AbilitySystemComponent->AddLooseGameplayTag(EnemyCombatReactionTags::Hitstun, 1,
                                                      EGameplayTagReplicationState::TagOnly);
}

void UEnemyHitstunAbility::UpdateExpiry(const FEnemyCombatReactionSettings& Settings, double Now)
{
    const double Limit = Started + FMath::Max(.05f, Settings.HitstunLimitSeconds);
    const float Until = static_cast<float>(FMath::Min(Limit - Now, FMath::Max(.05f, Settings.HitstunSeconds) * 1.0));
    GetWorld()->GetTimerManager().SetTimer(ExpiryTimer, this, &ThisClass::Expire, FMath::Max(.001f, Until));
}

void UEnemyHitstunAbility::Expire()
{
    const auto* Enemy = Cast<AEnemyCharacter>(GetAvatarActorFromActorInfo());
    if (Enemy)
    {
        const auto Settings = Enemy->GetCombatReactionSettings();
        const double Now = GetWorld()->GetTimeSeconds();
        if (Now + .001 >= Started + FMath::Max(.05f, Settings.HitstunLimitSeconds))
        {
            GraceEnds = Now + FMath::Max(0.f, Settings.EscapeGraceSeconds);
        }
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UEnemyHitstunAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                     FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (auto* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ExpiryTimer);
    }
    Hits = 0;
    if (bOwnsTag && Info && Info->AbilitySystemComponent.IsValid())
    {
        bOwnsTag = false;
        Info->AbilitySystemComponent->RemoveLooseGameplayTag(EnemyCombatReactionTags::Hitstun, 1,
                                                             EGameplayTagReplicationState::TagOnly);
    }
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}

UEnemyPoiseBreakAbility::UEnemyPoiseBreakAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    SetAssetTags(FGameplayTagContainer(TAG_EnemyPoiseBreak));
}

void UEnemyPoiseBreakAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                             FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* Enemy = Info ? Cast<AEnemyCharacter>(Info->AvatarActor.Get()) : nullptr;
    auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    if (!Enemy || !ASC || !Info->IsNetAuthority() || Enemy->IsDead_Implementation() || Enemy->IsDungeonPoolSleeping() ||
        ASC->GetNumericAttribute(UMyAttributeSet::GetPoiseAttribute()) > 0.f ||
        !CommitAbility(Handle, Info, Activation))
    {
        EndAbility(Handle, Info, Activation, true, true);
        return;
    }
    RecoveryPerSecond = FMath::Max(1.f, Enemy->GetCombatReactionSettings().PoiseRecoveryPerSecond);
    LastRecovery = GetWorld()->GetTimeSeconds();
    GetWorld()->GetTimerManager().SetTimer(RecoveryTimer, this, &ThisClass::Recover, .05f, true);
    bOwnsTag = true;
    ASC->AddLooseGameplayTag(EnemyCombatReactionTags::PoiseBroken, 1, EGameplayTagReplicationState::TagOnly);
}

void UEnemyPoiseBreakAbility::Recover()
{
    const auto* Enemy = Cast<AEnemyCharacter>(GetAvatarActorFromActorInfo());
    auto* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!Enemy || !ASC || Enemy->IsDead_Implementation() || Enemy->IsDungeonPoolSleeping())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
        return;
    }
    const auto Poise = UMyAttributeSet::GetPoiseAttribute();
    const float Max = ASC->GetNumericAttribute(UMyAttributeSet::GetMaxPoiseAttribute());
    const double Now = GetWorld()->GetTimeSeconds();
    const float Recovered = FMath::Min(Max, ASC->GetNumericAttribute(Poise) +
                                              static_cast<float>(Now - LastRecovery) * RecoveryPerSecond);
    LastRecovery = Now;
    ASC->SetNumericAttributeBase(Poise, Recovered);
    if (Recovered >= Max && IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

void UEnemyPoiseBreakAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (auto* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RecoveryTimer);
    }
    if (bOwnsTag && Info && Info->AbilitySystemComponent.IsValid())
    {
        bOwnsTag = false;
        Info->AbilitySystemComponent->RemoveLooseGameplayTag(EnemyCombatReactionTags::PoiseBroken, 1,
                                                             EGameplayTagReplicationState::TagOnly);
    }
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
