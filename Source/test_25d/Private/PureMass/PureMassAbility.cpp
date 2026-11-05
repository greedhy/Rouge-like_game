#include "PureMass/PureMassAbility.h"
#include "AbilitySystem/Prototypes/PrototypeFacing.h"
#include "PureMass/PureMassRuntime.h"
#include "PureMass/PureMassTargetData.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Engine/World.h"
#include "TimerManager.h"

FGameplayTag UPureMassManaEffect::GetManaTag()
{
    // Reuse the existing native-registered Damage.Physics key; it is only a magnitude key in this effect.
    return UPureMassIncomingDamageEffect::GetDamageTag();
}

UPureMassManaEffect::UPureMassManaEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FSetByCallerFloat Amount;
    Amount.DataTag = GetManaTag();
    FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetManaAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}

bool UPureMassAbility::SpendMana(AActor* Caster, float Amount)
{
    if (!IsValid(Caster) || !Caster->HasAuthority() || !FMath::IsFinite(Amount) || Amount < 0.f) return false;
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Caster);
    if (!ASC || !ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute())
        || ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Amount) return false;
    if (Amount == 0.f) return true;
    FGameplayEffectSpec Spec(GetDefault<UPureMassManaEffect>(), ASC->MakeEffectContext(), 1.f);
    Spec.SetSetByCallerMagnitude(UPureMassManaEffect::GetManaTag(), -Amount);
    return ASC->ApplyGameplayEffectSpecToSelf(Spec).WasSuccessfullyApplied();
}

void UPureMassAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority() && Prototype) SpendMana(Info->AvatarActor.Get(), Prototype->ManaCost);
}

void UPureMassAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    bTargetConsumed = false;
    MassRuntime = nullptr;
    if (!Info || !Prototype || !GetWorld()->GetSubsystem<UPureMassSubsystem>()) { AbortMass(); return; }
    if (Prototype->Kind == EAbilityPrototypeKind::Beam)
    {
        auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
        Release->OnRelease.AddDynamic(this, &ThisClass::Released);
        Release->ReadyForActivation();
        if (!IsActive()) return;
    }
    if (Prototype->bTargetSelf || IsInputToggleAbility())
    {
        if (Info->IsNetAuthority()) ExecuteMass(Info->AvatarActor->GetActorLocation(), INDEX_NONE);
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(MassTargetTimeout, this, &ThisClass::AbortMass, 3.f);
    auto* Task = UPureMassTargetData::Create(this);
    Task->Ready.AddDynamic(this, &ThisClass::TargetReady);
    Task->ReadyForActivation();
}

bool UPureMassAbility::ValidateTarget(AActor* Caster, const UAbilityPrototype* Config, int32 Id, const FVector& Requested, FVector& Resolved)
{
    return ValidateCombatTarget(Caster, Config, FPureMassCombatTarget(Id), Requested, Resolved);
}

bool UPureMassAbility::ValidateCombatTarget(AActor* Caster, const UAbilityPrototype* Config, const FPureMassCombatTarget& Target,
    const FVector& Requested, FVector& Resolved)
{
    if (!IsValid(Caster) || !Caster->HasAuthority() || !Config || Requested.ContainsNaN()
        || Target.EntityId < INDEX_NONE || Target.EntityId == 0 || (Target.IsActor() && Target.EntityId != INDEX_NONE)) return false;
    Resolved = Requested;
    if (Target.IsSet())
    {
        if (!PureMassCombat::Resolve(Caster->GetWorld(), Caster, Target, Resolved)) return false;
        // Prevent a valid ID from smuggling an unrelated aim point; allow snapshot interpolation error.
        if (FVector::DistSquared(Requested, Resolved) > FMath::Square(250.f)) return false;
    }
    if (FVector::DistSquared(Caster->GetActorLocation(), Resolved) > FMath::Square(Config->Range)) return false;
    using K = EAbilityPrototypeKind;
    if ((Config->Kind == K::Beam || Config->Kind == K::Homing || Config->Kind == K::Chain) && !Target.IsSet()) return false;
    if (Target.IsSet()) return PureMassCombat::Visible(Caster->GetWorld(), Caster, Caster->GetActorLocation()+FVector(0,0,30), Target);
    return PureMassCombat::GroundVisible(Caster->GetWorld(), Caster, Caster->GetActorLocation() + FVector(0,0,30), Resolved);
}

void UPureMassAbility::TargetReady(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bTargetConsumed || !CurrentActorInfo) return;
    if (!CurrentActorInfo->IsNetAuthority())
    {
        bTargetConsumed = true;
        GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
        return;
    }
    const FHitResult* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    FVector Point;
    if (!Hit || !Hit->bBlockingHit || (Hit->GetActor() && Hit->Item != INDEX_NONE)) { AbortMass(); return; }
    const auto Target = Hit->GetActor() ? FPureMassCombatTarget::FromActor(Hit->GetActor()) : FPureMassCombatTarget(Hit->Item);
    if (!ValidateCombatTarget(GetAvatarActorFromActorInfo(), Prototype, Target, Hit->ImpactPoint, Point))
    { AbortMass(); return; }
    ExecuteMass(Point, Target);
}

void UPureMassAbility::ExecuteMass(const FVector& Point, FPureMassCombatTarget Target)
{
    if (bTargetConsumed || !IsActive() || !CurrentActorInfo->IsNetAuthority()) return;
    bTargetConsumed = true;
    GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
    AActor* Avatar = GetAvatarActorFromActorInfo();
    FVector Origin = Point;
    using K = EAbilityPrototypeKind;
    if (Prototype->Kind == K::Surface && !APrototypeRuntime::ProjectSurfaceToGround(Avatar, Point, Origin)) { AbortMass(); return; }
    SetOffensiveAttackTarget(Target.Actor.Get());
    if (!CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) || !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    { AbortMass(); return; }
    auto* Context = NewObject<UPrototypeCastContext>(Avatar);
    Context->Caster = Avatar;
    Context->CastId = FGuid::NewGuid();
    Context->Level = GetAbilityLevel();
    Context->DamageParams = MakeDamageEffectParamsFromClassDefaults();
    FVector Direction = (Point - Avatar->GetActorLocation()).GetSafeNormal(SMALL_NUMBER, Avatar->GetActorForwardVector());
    if (Prototype->Kind == K::Projectile || Prototype->Kind == K::Multishot || Prototype->Kind == K::Homing || Prototype->Kind == K::Piercing || Prototype->Kind == K::Returning)
    {
        Origin = Avatar->GetActorLocation() + FVector(0,0,30);
        Direction = (Point + (!Target.IsSet() ? FVector(0,0,30) : FVector::ZeroVector) - Origin).GetSafeNormal(SMALL_NUMBER, Direction);
    }
    if (Prototype->Kind == K::Cone || Prototype->Kind == K::Line || Prototype->Kind == K::Chain) Origin = Avatar->GetActorLocation();
    MassRuntime = APureMassRuntime::Spawn(Context, Prototype, Origin, Direction, Target);
    if (!IsValid(MassRuntime)) { AbortMass(); return; }
    PrototypeFacing::Apply(Avatar, Point, Prototype->bTargetSelf);
    if (Prototype->Kind != K::Orbit) APureMassRuntime::NotifyCommittedAttack(Avatar, this, Target);
    if (Prototype->Kind == K::Beam || IsInputToggleAbility())
        MassRuntime->OnDestroyed.AddDynamic(this, &ThisClass::RuntimeDestroyed);
    else
    {
        MassRuntime = nullptr;
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

void UPureMassAbility::Released(float) { AbortMass(); }
void UPureMassAbility::RuntimeDestroyed(AActor*)
{
    MassRuntime = nullptr;
    if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
void UPureMassAbility::AbortMass()
{
    if (IsActive()) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
void UPureMassAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
    if (IsValid(MassRuntime))
    {
        MassRuntime->OnDestroyed.RemoveDynamic(this, &ThisClass::RuntimeDestroyed);
        MassRuntime->CancelCast();
    }
    MassRuntime = nullptr;
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
