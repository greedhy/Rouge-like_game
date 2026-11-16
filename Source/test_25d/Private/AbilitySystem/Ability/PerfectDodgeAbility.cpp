// 调用链：固定按下 -> CanActivate -> Reserve -> Commit -> Begin；组件动作秒结束通知GA归还输入占用。
#include "AbilitySystem/Ability/PerfectDodgeAbility.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PerfectDodge, "Abilities.Common.PerfectDodge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PerfectDodgeCooldown, "Cooldown.Common.PerfectDodge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PerfectDodgeInput, "InputTag.Dodge");

UPerfectDodgeCooldownEffect::UPerfectDodgeCooldownEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(.80f);
    // 蓝图编译器通过CDO授予标签验证冷却；不能只在运行时Spec上动态追加。
    auto* Component = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("PerfectDodgeCooldownTags"));
    GEComponents.Add(Component);
    FInheritedTagContainer Tags;
    Tags.AddTag(TAG_PerfectDodgeCooldown);
    Component->SetAndApplyTargetTagChanges(Tags);
}

UPerfectDodgeAbility::UPerfectDodgeAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    SetAssetTags(FGameplayTagContainer(TAG_PerfectDodge));
    StartupInputTag = TAG_PerfectDodgeInput;
    CooldownGameplayEffectClass = UPerfectDodgeCooldownEffect::StaticClass();
}

bool UPerfectDodgeAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                              const FGameplayTagContainer* SourceTags,
                                              const FGameplayTagContainer* TargetTags,
                                              FGameplayTagContainer* OptionalRelevantTags) const
{
    FString Error;
    auto* Character = Info ? Cast<ACharacter>(Info->AvatarActor.Get()) : nullptr;
    if (!Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, OptionalRelevantTags) ||
        !DodgeSettings.Validate(Error) || !FMath::IsFinite(CooldownSeconds) || CooldownSeconds < 0.f ||
        CooldownSeconds > 60.f || !UPerfectDodgeComponent::CanStart(Character))
    {
        return false;
    }
    // 不授予取消既有攻击的特权；主动输入技能结束后才能进入翻滚，常驻无输入监听器不受影响。
    const auto InputRoot = FGameplayTag::RequestGameplayTag(TEXT("InputTag"));
    for (const auto& Spec : Info->AbilitySystemComponent->GetActivatableAbilities())
    {
        if (Spec.Handle != Handle && Spec.IsActive() && Spec.GetDynamicSpecSourceTags().HasTag(InputRoot))
        {
            return false;
        }
    }
    return true;
}

bool UPerfectDodgeAbility::CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* Info,
                                         FGameplayTagContainer*) const
{
    return Info && Info->AbilitySystemComponent.IsValid() &&
           !Info->AbilitySystemComponent->HasMatchingGameplayTag(TAG_PerfectDodgeCooldown);
}

void UPerfectDodgeAbility::ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* Info,
                                         FGameplayAbilityActivationInfo) const
{
    if (!Info || !Info->IsNetAuthority() || CooldownSeconds <= 0.f)
    {
        return;
    }
    auto* ASC = Info->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(CooldownGameplayEffectClass, 1.f, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(CooldownSeconds, true);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}

void UPerfectDodgeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData*)
{
    auto* Character = Info ? Cast<ACharacter>(Info->AvatarActor.Get()) : nullptr;
    FAbilitySourceToken Source;
    FString Error;
    if (!Character || !AbilityExecutionIdentity::CaptureSource(Character, Source, Error))
    {
        EndAbility(Handle, Info, ActivationInfo, true, true);
        return;
    }
    FVector Direction = Character->GetLastMovementInputVector().GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        Direction = Character->GetVelocity().GetSafeNormal2D();
    }
    if (Direction.IsNearlyZero())
    {
        Direction = Character->GetActorForwardVector().GetSafeNormal2D();
    }
    Defense = UPerfectDodgeComponent::FindOrCreate(Character);
    Execution = Defense->Reserve(Source, this, DodgeSettings, Direction, Error);
    if (!Execution.IsValid())
    {
        EndAbility(Handle, Info, ActivationInfo, true, true);
        return;
    }
    FinishedHandle = Defense->OnFinished.AddUObject(this, &ThisClass::DodgeFinished);
    if (!CommitAbility(Handle, Info, ActivationInfo) || !IsActive() || !Defense->Begin(Execution))
    {
        EndAbility(Handle, Info, ActivationInfo, true, true);
    }
}

void UPerfectDodgeAbility::DodgeFinished(FGuid FinishedExecution, bool bCancelled)
{
    if (Execution == FinishedExecution && IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
    }
}

void UPerfectDodgeAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                                      bool bWasCancelled)
{
    CloseExecutionSession(bWasCancelled);
    if (Defense)
    {
        Defense->OnFinished.Remove(FinishedHandle);
        Defense->Finish(Execution, bWasCancelled);
    }
    FinishedHandle.Reset();
    Execution.Invalidate();
    Super::EndAbility(Handle, Info, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UPerfectDodgeAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* Info, const FGameplayAbilitySpec& Spec)
{
    if (Defense)
    {
        Defense->InvalidateLife();
    }
    Super::OnRemoveAbility(Info, Spec);
}
