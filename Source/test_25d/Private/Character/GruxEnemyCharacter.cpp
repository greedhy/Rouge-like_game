#include "Character/GruxEnemyCharacter.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxArmor, "Data.Grux.RoarArmor");
UGruxRoarArmorEffect::UGruxRoarArmorEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(6);
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = UMyAttributeSet::GetArmorAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Magnitude;
    Magnitude.DataTag = TAG_GruxArmor;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
    Modifiers.Add(Modifier);
}
bool AGruxEnemyCharacter::HasRoarArmor() const
{
    return RoarArmor.IsValid() && AbilitySystemComponent &&
           AbilitySystemComponent->GetActiveGameplayEffect(RoarArmor) != nullptr;
}
FActiveGameplayEffectHandle AGruxEnemyCharacter::ApplyRoarArmor(float Armor, float Duration)
{
    if (!HasAuthority() || bDead || IsDungeonPoolSleeping() || HasRoarArmor() || !FMath::IsFinite(Armor) ||
        !FMath::IsFinite(Duration) || Armor <= 0 || Duration <= 0 || Duration > 30)
        return {};
    auto Spec = AbilitySystemComponent->MakeOutgoingSpec(UGruxRoarArmorEffect::StaticClass(), 1,
                                                         AbilitySystemComponent->MakeEffectContext());
    if (!Spec.IsValid())
        return {};
    Spec.Data->SetDuration(Duration, true);
    Spec.Data->SetSetByCallerMagnitude(TAG_GruxArmor, Armor);
    const int32 Lease = GetDungeonPoolLease();
    const auto Applied = AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    if (bDead || IsDungeonPoolSleeping() || GetDungeonPoolLease() != Lease)
    {
        AbilitySystemComponent->RemoveActiveGameplayEffect(Applied);
        return {};
    }
    RoarArmor = Applied;
    return RoarArmor;
}
void AGruxEnemyCharacter::RemoveRoarArmor(FActiveGameplayEffectHandle Expected)
{
    if (!HasAuthority() || !Expected.IsValid() || Expected != RoarArmor)
        return;
    const auto Removed = RoarArmor;
    RoarArmor.Invalidate();
    AbilitySystemComponent->RemoveActiveGameplayEffect(Removed);
}
void AGruxEnemyCharacter::InvalidateEnemyAttack()
{
    RemoveRoarArmor(RoarArmor);
    Super::InvalidateEnemyAttack();
}
void AGruxEnemyCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    RemoveRoarArmor(RoarArmor);
    Super::EndPlay(Reason);
}
