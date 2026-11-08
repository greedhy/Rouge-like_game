#include "AbilitySystem/Ability/MyOrbitingFireBoltAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "MyGameplayTags.h"
#include "TimerManager.h"
#include "GameplayEffect.h"

UMyOrbitingFireBoltAbility::UMyOrbitingFireBoltAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    Damage = FScalableFloat(10.f);
}

bool UMyOrbitingFireBoltAbility::CheckCost(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           FGameplayTagContainer* OptionalRelevantTags) const
{
    const auto* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
    return Config && ASC && ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()) &&
           ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= Config->ActivationManaCost &&
           Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
}

void UMyOrbitingFireBoltAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo) const
{
    // CommitAbility invokes this once, after CheckCost has accepted the activation.
    Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
    SpendMana(Config->ActivationManaCost);
}

bool UMyOrbitingFireBoltAbility::SpendMana(float Amount) const
{
    auto* ASC = GetAbilitySystemComponentFromActorInfo();
    if (!ASC || !GetAvatarActorFromActorInfo() || !GetAvatarActorFromActorInfo()->HasAuthority() ||
        ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Amount)
        return false;
    if (Amount == 0.f)
        return true;
    // Instant effects update the replicated Mana attribute through the normal GAS pipeline.
    auto* Cost = NewObject<UGameplayEffect>(GetTransientPackage());
    Cost->DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Modifier = Cost->Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetManaAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-Amount);
    const FActiveGameplayEffectHandle Applied =
        ASC->ApplyGameplayEffectToSelf(Cost, GetAbilityLevel(), ASC->MakeEffectContext());
    return Applied.WasSuccessfullyApplied();
}

void UMyOrbitingFireBoltAbility::PayUpkeep()
{
    if (IsActive() && !SpendMana(Config->ManaPerSecond))
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

bool UMyOrbitingFireBoltAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                    const FGameplayAbilityActorInfo* ActorInfo,
                                                    const FGameplayTagContainer* SourceTags,
                                                    const FGameplayTagContainer* TargetTags,
                                                    FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() ||
        !Cast<UMyAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) || !Config ||
        !Config->IsValidConfiguration() || !DamageEffectClass || !DamageType.IsValid())
        return false;
    AActor* Avatar = ActorInfo->AvatarActor.Get();
    return Avatar->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Avatar) &&
           Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UMyOrbitingFireBoltAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                 const FGameplayAbilityActorInfo* ActorInfo,
                                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                                 const FGameplayEventData* TriggerEventData)
{
    // Native execution only: no duplicate Blueprint ActivateAbility event.
    if (!ActorInfo || !ActorInfo->IsNetAuthority() || !CanActivateAbility(Handle, ActorInfo) ||
        !CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());
    AttackDelegateHandle = ASC->OffensiveAttackTargeted.AddUObject(this, &ThisClass::HandleOffensiveAttack);
    DeactivateDelegateHandle = ASC->DeactivatePassiveAbility.AddUObject(this, &ThisClass::HandleDeactivate);
    AActor* Avatar = ActorInfo->AvatarActor.Get();
    CastChecked<ICombatInterface>(Avatar)->GetOnDeathDelegate().AddDynamic(this, &ThisClass::OnOwnerDied);
    Avatar->OnDestroyed.AddDynamic(this, &ThisClass::OnOwnerDestroyed);
    GetWorld()->GetTimerManager().SetTimer(SpawnTimer, this, &ThisClass::Replenish, Config->SpawnInterval, true);
    GetWorld()->GetTimerManager().SetTimer(ManaTimer, this, &ThisClass::PayUpkeep, 1.f, true);
}

bool UMyOrbitingFireBoltAbility::IsValidEnemy(AActor* Target) const
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    return IsValid(Avatar) && IsValid(Target) && Target != Avatar && Config && Target->Implements<UCombatInterface>() &&
           !ICombatInterface::Execute_IsDead(Target) && UMyAbilitySystemLibrary::IsNotFriend(Avatar, Target) &&
           UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target) &&
           FVector::DistSquared(Avatar->GetActorLocation(), Target->GetActorLocation()) <=
               FMath::Square(Config->MaxTargetDistance);
}

void UMyOrbitingFireBoltAbility::HandleOffensiveAttack(UGameplayAbility* SourceAbility, AActor* Target)
{
    if (!IsActive() || !SourceAbility || SourceAbility == this || !IsValidEnemy(Target) ||
        SourceAbility->GetAssetTags().HasTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Passive"))))
        return;
    if (AttackTarget.Get() != Target)
    {
        ClearTarget();
        AttackTarget = Target;
        CastChecked<ICombatInterface>(Target)->GetOnDeathDelegate().AddDynamic(this, &ThisClass::OnTargetDied);
    }
    // Do not reset the cadence on repeated casts at the same target.
    if (!GetWorld()->GetTimerManager().IsTimerActive(LaunchTimer))
        GetWorld()->GetTimerManager().SetTimer(LaunchTimer, this, &ThisClass::LaunchNext, Config->LaunchInterval, true,
                                               .001f);
}

void UMyOrbitingFireBoltAbility::Replenish()
{
    if (!IsActive())
        return;
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!IsValid(Avatar) || ICombatInterface::Execute_IsDead(Avatar))
    {
        OnOwnerDied(Avatar);
        return;
    }
    FireBolts.RemoveAll(
        [](const auto& Ball)
        {
            return !IsValid(Ball);
        });
    if (FireBolts.Num() >= Config->MaxFireBolts)
        return;
    const FTransform Transform(FRotator::ZeroRotator,
                               Avatar->GetActorLocation() + FVector(Config->OrbitRadius, 0, Config->OrbitHeight));
    auto* Ball = Cast<AMyOrbitingFireBolt>(CreateProjectileDeferred(Config->FireBoltClass, Transform));
    if (!Ball)
        return;
    Ball->InitializeOrbit(Avatar, Config, 0.f);
    Ball->FinishSpawning(Transform);
    FireBolts.Add(Ball);
    ArrangeOrbit();
}

void UMyOrbitingFireBoltAbility::ArrangeOrbit()
{
    int32 Index = 0;
    const int32 Count = GetLiveFireBoltCount() - GetLaunchedFireBoltCount();
    for (const auto& Ball : FireBolts)
        if (IsValid(Ball) && !Ball->IsLaunched())
            Ball->SetOrbitPhase(360.f * Index++ / FMath::Max(1, Count));
}

void UMyOrbitingFireBoltAbility::LaunchNext()
{
    if (!IsActive())
        return;
    if (!IsValidEnemy(AttackTarget.Get()))
    {
        ClearTarget();
        return;
    }
    for (const auto& Ball : FireBolts)
    {
        if (IsValid(Ball) && !Ball->IsLaunched())
        {
            Ball->LaunchAt(AttackTarget.Get());
            ArrangeOrbit();
            break;
        }
    }
}

void UMyOrbitingFireBoltAbility::ClearTarget()
{
    if (auto* Combat = Cast<ICombatInterface>(AttackTarget.Get()))
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnTargetDied);
    AttackTarget.Reset();
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(LaunchTimer);
    for (const auto& Ball : FireBolts)
        if (IsValid(Ball))
            Ball->ReturnToOrbit();
    ArrangeOrbit();
}

void UMyOrbitingFireBoltAbility::OnTargetDied(AActor* Target)
{
    if (Target == AttackTarget.Get())
        ClearTarget();
}
void UMyOrbitingFireBoltAbility::OnOwnerDied(AActor*)
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
void UMyOrbitingFireBoltAbility::OnOwnerDestroyed(AActor* OwnerActor)
{
    OnOwnerDied(OwnerActor);
}
void UMyOrbitingFireBoltAbility::HandleDeactivate(const FGameplayTag& Tag)
{
    if (GetAssetTags().HasTagExact(Tag))
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UMyOrbitingFireBoltAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            const FGameplayAbilityActivationInfo ActivationInfo,
                                            bool bReplicateEndAbility, bool bWasCancelled)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(SpawnTimer);
        GetWorld()->GetTimerManager().ClearTimer(ManaTimer);
    }
    ClearTarget();
    if (ActorInfo)
    {
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
        {
            ASC->OffensiveAttackTargeted.Remove(AttackDelegateHandle);
            ASC->DeactivatePassiveAbility.Remove(DeactivateDelegateHandle);
        }
        if (AActor* Avatar = ActorInfo->AvatarActor.Get())
        {
            if (auto* Combat = Cast<ICombatInterface>(Avatar))
                Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnOwnerDied);
            Avatar->OnDestroyed.RemoveDynamic(this, &ThisClass::OnOwnerDestroyed);
        }
    }
    for (const auto& Ball : FireBolts)
        if (IsValid(Ball))
            Ball->Destroy();
    FireBolts.Reset();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UMyOrbitingFireBoltAbility::GetLiveFireBoltCount() const
{
    int32 Count = 0;
    for (const auto& Ball : FireBolts)
        if (IsValid(Ball))
            ++Count;
    return Count;
}
int32 UMyOrbitingFireBoltAbility::GetLaunchedFireBoltCount() const
{
    int32 Count = 0;
    for (const auto& Ball : FireBolts)
        if (IsValid(Ball) && Ball->IsLaunched())
            ++Count;
    return Count;
}
FString UMyOrbitingFireBoltAbility::GetDescription(int32 Level)
{
    return FString::Printf(
        TEXT("<Title>ORBITING FIRE BOLTS</>\n"
             "<Small>Level: </><Level>%d</><Small> | MP: </><ManaCost>%.0f</><Small> + </><ManaCost>%.0f/s</>\n"
             "<Default>Toggle: 1 bolt every %.1fs, max %d.\n"
             "Attacks launch bolts for </><Damage>%.0f</><Default> fire damage.</>"),
        Level, Config ? Config->ActivationManaCost : 10.f, Config ? Config->ManaPerSecond : 1.f,
        Config ? Config->SpawnInterval : 3.f, Config ? Config->MaxFireBolts : 3, Damage.GetValueAtLevel(Level));
}
FString UMyOrbitingFireBoltAbility::GetNextLevelDescription(int32 Level)
{
    return FString::Printf(TEXT("<Title>NEXT LEVEL</>\n"
                                "<Small>Level: </><Level>%d</>\n"
                                "<Small>Fire damage: </><Damage>%.0f</>"),
                           Level, Damage.GetValueAtLevel(Level));
}
