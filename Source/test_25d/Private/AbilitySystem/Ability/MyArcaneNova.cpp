#include "AbilitySystem/Ability/MyArcaneNova.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

UMyArcaneNova::UMyArcaneNova()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    Damage = FScalableFloat(45.f);
    DebuffChance = 0.f;
    KnockbackChance = 0.f;
    KnockbackForceMagnitude = 0.f;
    bIsRadialDamage = false;
}

bool UMyArcaneNova::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo,
                                       const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                       FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid() || !ActorInfo->AvatarActor.IsValid() ||
        !DamageEffectClass || !DamageType.IsValid() || !CostGameplayEffectClass || !CooldownGameplayEffectClass ||
        !FMath::IsFinite(NovaRadius) || NovaRadius <= 0.f)
    {
        return false;
    }
    const AActor* Avatar = ActorInfo->AvatarActor.Get();
    if (!Avatar->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Avatar))
    {
        return false;
    }
    return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

float UMyArcaneNova::GetNovaDamage(int32 Level) const
{
    const int32 SafeLevel = FMath::Max(1, Level);
    return FMath::Max(0.f, Damage.GetValueAtLevel(SafeLevel) + BonusDamagePerLevel * (SafeLevel - 1));
}

void UMyArcaneNova::GatherNovaTargets(AActor* SourceAvatar, TArray<AActor*>& OutTargets) const
{
    OutTargets.Reset();
    if (!IsValid(SourceAvatar) || !FMath::IsFinite(NovaRadius) || NovaRadius <= 0.f)
        return;
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(SourceAvatar, Candidates, {SourceAvatar}, NovaRadius,
                                                        SourceAvatar->GetActorLocation());
    for (AActor* Target : Candidates)
    {
        if (IsValid(Target) && Target != SourceAvatar && Target->Implements<UCombatInterface>() &&
            !ICombatInterface::Execute_IsDead(Target) && UMyAbilitySystemLibrary::IsNotFriend(SourceAvatar, Target) &&
            FVector::DistSquared(SourceAvatar->GetActorLocation(), Target->GetActorLocation()) <=
                FMath::Square(NovaRadius) &&
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
        {
            OutTargets.AddUnique(Target);
        }
    }
}

void UMyArcaneNova::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    const FGameplayEventData* TriggerEventData)
{
    // All gameplay work is performed on the server. Do not forward to a Blueprint Activate event.
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !CanActivateAbility(Handle, ActorInfo) ||
        !CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    AActor* Avatar = ActorInfo->AvatarActor.Get();
    TArray<AActor*> Targets;
    GatherNovaTargets(Avatar, Targets);
    Targets.Sort(
        [Avatar](const AActor& A, const AActor& B)
        {
            return FVector::DistSquared(Avatar->GetActorLocation(), A.GetActorLocation()) <
                   FVector::DistSquared(Avatar->GetActorLocation(), B.GetActorLocation());
        });
    if (!Targets.IsEmpty())
        ReportOffensiveAttack(Targets[0]);
    for (AActor* Target : Targets)
    {
        if (!IsValid(Target) || ICombatInterface::Execute_IsDead(Target))
            continue;
        FDamageEffectParams Params = MakeDamageEffectParamsFromClassDefaults(Target);
        if (!IsValid(Params.TargetAbilitySystemComponent))
            continue;
        Params.BaseDamage = GetNovaDamage(GetAbilityLevel());
        // The overlap already selects targets. Each target receives one full-strength GE.
        Params.bIsRadialDamage = false;
        Params.KnockbackForce = FVector::ZeroVector;
        UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    }

    if (NovaCueTag.IsValid())
    {
        FGameplayCueParameters Cue;
        Cue.Location = Avatar->GetActorLocation();
        if (const ACharacter* Character = Cast<ACharacter>(Avatar))
        {
            Cue.Location.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        }
        Cue.Normal = FVector::UpVector;
        Cue.RawMagnitude = NovaRadius;
        Cue.Instigator = Avatar;
        Cue.EffectCauser = Avatar;
        ActorInfo->AbilitySystemComponent->ExecuteGameplayCue(NovaCueTag, Cue);
    }
    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

FString UMyArcaneNova::GetDescription(int32 Level)
{
    return FString::Printf(TEXT("<Title>ARCANE NOVA</>\n\n<Small>Level: </><Level>%d</>\n"
                                "<Small>ManaCost: </><ManaCost>%.1f</>\n<Small>Cooldown: </><Cooldown>%.1f</>\n\n"
                                "<Default>Release a pulse within %.0f units, dealing </><Damage>%.0f</>"
                                "<Default> arcane damage to each living enemy once.</>"),
                           Level, FMath::Abs(GetManaCost(Level)), GetCooldown(Level), NovaRadius, GetNovaDamage(Level));
}

FString UMyArcaneNova::GetNextLevelDescription(int32 Level)
{
    return FString(TEXT("<Title>NEXT LEVEL</>\n")) + GetDescription(Level);
}
