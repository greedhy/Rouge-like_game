// 主要调用链：GAS 激活检查 -> ActivateAbility -> 定时 Advance -> Hit -> EndAbility；消耗与冷却复用本文件定义的 GE。
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "Character/SwordShieldPlayer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Roguelike/DungeonRunDirector.h"
#include "MyGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordComboTag, "Abilities.SwordShield.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardTag, "Abilities.SwordShield.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordSpinTag, "Abilities.SwordShield.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordBusyTag, "State.SwordShield.Busy");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardCD, "Cooldown.SwordShield.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordSpinCD, "Cooldown.SwordShield.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordManaTag, "Data.SwordShield.Mana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardActiveTag, "State.SwordShield.Guarding");
// 返回格挡状态标签，供伤害逻辑识别。
FGameplayTag USwordGuardEffect::StateTag()
{
    return SwordGuardActiveTag;
}
// 设置限时 GE 的默认持续策略。
USwordTimedEffect::USwordTimedEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1);
}
// 创建无限持续的格挡标签 GE。
USwordGuardEffect::USwordGuardEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    // Player secondary-stat effects already override BlockChance. A separate tag
    // guarantees guard without replacing or competing with equipment attributes.
    auto* Component = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GuardTags"));
    GEComponents.Add(Component);
    FInheritedTagContainer Tags;
    Tags.AddTag(SwordGuardActiveTag);
    Component->SetAndApplyTargetTagChanges(Tags);
}
// 配置瞬时法力加法修正，数值由调用方传入。
USwordManaEffect::USwordManaEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Mod;
    Mod.Attribute = UMyAttributeSet::GetManaAttribute();
    Mod.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Value;
    Value.DataTag = SwordManaTag;
    Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(Value);
    Modifiers.Add(Mod);
}
// 设置该技能的默认执行策略、模式和激活约束。
USwordShieldAbility::USwordShieldAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationOwnedTags.AddTag(SwordBusyTag);
    ActivationBlockedTags.AddTag(SwordBusyTag);
    DebuffChance = 0;
    KnockbackChance = 0;
    DeathImpulseMagnitude = 0;
    KnockbackForceMagnitude = 0;
}
// 设置该技能的默认执行策略、模式和激活约束。
USwordComboAbility::USwordComboAbility()
{
    SetAssetTags(FGameplayTagContainer(SwordComboTag));
}
// 设置该技能的默认执行策略、模式和激活约束。
USwordGuardAbility::USwordGuardAbility()
{
    Mode = EMode::Guard;
    SetAssetTags(FGameplayTagContainer(SwordGuardTag));
    CooldownIdentity = SwordGuardCD;
}
// 设置该技能的默认执行策略、模式和激活约束。
USwordSpinAbility::USwordSpinAbility()
{
    Mode = EMode::Spin;
    SetAssetTags(FGameplayTagContainer(SwordSpinTag));
    CooldownIdentity = SwordSpinCD;
}
// 从当前 Avatar 的角色配置读取对应剑盾数据资产。
const USwordShieldConfig* USwordShieldAbility::Config(const FGameplayAbilityActorInfo* Info) const
{
    auto* P = Info ? Cast<ASwordShieldPlayer>(Info->AvatarActor.Get()) : nullptr;
    return P ? P->SwordConfig.Get() : nullptr;
}
// 激活前检查角色状态、配置及伤害依赖，再交给 GAS 检查通用条件。
bool USwordShieldAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                             const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                             FGameplayTagContainer* R) const
{
    const auto* C = Config(I);
    auto* P = I ? Cast<ASwordShieldPlayer>(I->AvatarActor.Get()) : nullptr;
    if (UAbilityMovementOwnershipComponent::HasActiveClaim(P))
    {
        return false;
    }
    if (!P || !C || !C->Validate() || !I->AbilitySystemComponent.IsValid() || P->IsDungeonDowned() ||
        P->IsDead_Implementation() || P->bIsStunned || !P->GetMesh()->GetAnimInstance() || !DamageEffectClass ||
        !DamageType.IsValid())
        return false;
    return Super::CanActivateAbility(H, I, S, T, R);
}
// 根据冷却标签判断当前技能是否仍处于冷却。
bool USwordShieldAbility::CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                        FGameplayTagContainer*) const
{
    return I && I->AbilitySystemComponent.IsValid() &&
           (!CooldownIdentity.IsValid() || !I->AbilitySystemComponent->HasMatchingGameplayTag(CooldownIdentity));
}
// 服务器施加带冷却标签的限时 GE。
void USwordShieldAbility::ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                        FGameplayAbilityActivationInfo) const
{
    const auto* C = Config(I);
    if (!C || !I->IsNetAuthority() || !CooldownIdentity.IsValid())
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordTimedEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(Mode == EMode::Guard ? C->GuardCooldown : C->SpinCooldown, true);
        Spec.Data->DynamicGrantedTags.AddTag(CooldownIdentity);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}
// 读取 ASC 的当前法力，判断是否足够支付本次消耗。
bool USwordShieldAbility::CheckCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                    FGameplayTagContainer*) const
{
    const auto* C = Config(I);
    return C && I->AbilitySystemComponent.IsValid() &&
           (Mode != EMode::Spin ||
            I->AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= C->SpinMana);
}
// 服务器通过 SetByCaller 法力 GE 扣除消耗。
void USwordShieldAbility::ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                    FGameplayAbilityActivationInfo) const
{
    const auto* C = Config(I);
    if (!C || !I->IsNetAuthority() || Mode != EMode::Spin)
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordManaEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(SwordManaTag, -C->SpinMana);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}
// 提交技能消耗和冷却，锁定朝向与移动，播放蒙太奇并启动推进定时器。
void USwordShieldAbility::ActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                          FGameplayAbilityActivationInfo A, const FGameplayEventData*)
{
    bOwnMovement = false;
    bHit = false;
    GuardHandle.Invalidate();
    Player = I ? Cast<ASwordShieldPlayer>(I->AvatarActor.Get()) : nullptr;
    const auto* C = Config(I);
    if (!Player.IsValid() || !C || !C->Validate() || !I->IsNetAuthority())
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    Started = GetWorld()->GetTimeSeconds();
    CurrentCombo = USwordShieldConfig::ResolveComboIndex(NextCombo, LastComboEnd, Started, C->ComboReset);
    Strike = Mode == EMode::Spin ? C->Spin : C->Combo[CurrentCombo];
    GuardTime = C->GuardDuration;
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
    {
        Strike.Montage = C->GuardMontage;
        Strike.PlayRate = Strike.Montage->GetPlayLength() / GuardTime;
    }
    auto* ASC = I->AbilitySystemComponent.Get();
    if (!CommitAbility(H, I, A))
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    FVector Aim = Player->GetActorForwardVector();
    if (auto* PC = I->PlayerController.Get())
        if (PC->IsLocalController())
        {
            FHitResult HitResult;
            if (PC->GetHitResultUnderCursor(ECC_Visibility, false, HitResult) && !HitResult.ImpactPoint.ContainsNaN())
            {
                const FVector Delta = (HitResult.ImpactPoint - Player->GetActorLocation()).GetSafeNormal2D();
                if (!Delta.IsNearlyZero())
                    Aim = Delta;
            }
        }
    LockedForward = Aim.GetSafeNormal2D();
    Player->SetActorRotation(LockedForward.Rotation());
    auto* Movement = Player->GetCharacterMovement();
    PreviousMovement = Movement->MovementMode;
    bOwnMovement = true;
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
    if (ASC->PlayMontage(this, A, Strike.Montage, Strike.PlayRate) <= 0)
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
    {
        FAbilitySourceToken Source;
        FString Error;
        if (AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
        {
            GuardHandle = AbilityCharacterActions::OpenGuardWindow(Source);
        }
    }
    GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Advance, .01f, true);
}
// 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
void USwordShieldAbility::Advance()
{
    if (!IsActive())
        return;
    auto Stop = [this](bool Cancel)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, Cancel);
    };
    if (!Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation() || Player->bIsStunned)
    {
        Stop(true);
        return;
    }
    auto* Anim = Player->GetMesh()->GetAnimInstance();
    const float Elapsed = GetWorld()->GetTimeSeconds() - Started;
    if (Mode == EMode::Guard && Elapsed >= GuardTime)
    {
        Stop(false);
        return;
    }
    if (!Anim || !Anim->Montage_IsActive(Strike.Montage))
    {
        Stop(Mode != EMode::Guard ? !bHit : Elapsed < GuardTime - .12f);
        return;
    }
    if (Mode != EMode::Guard && !bHit && Anim->Montage_GetPosition(Strike.Montage) >= Strike.Contact)
    {
        if (Anim->Montage_GetPosition(Strike.Montage) > Strike.Contact + .15f)
        {
            Stop(true);
            return;
        }
        bHit = true;
        const TWeakObjectPtr<USwordShieldAbility> WeakThis(this);
        if (!DispatchExecutionAction(TEXT("SwordShield.Strike"), 0,
                                     [WeakThis](int32)
                                     {
                                         if (auto* Self = WeakThis.Get())
                                         {
                                             Self->Hit();
                                         }
                                     }))
        {
            Stop(true);
            return;
        }
        if (!IsActive())
            return;
    }
    if (Elapsed > Strike.Montage->GetPlayLength() / Strike.PlayRate + .2f)
        Stop(Mode != EMode::Guard && !bHit);
}
// 筛选范围内合法且无遮挡的目标，应用伤害并播放命中表现。
void USwordShieldAbility::Hit()
{
    FAbilitySourceToken Source;
    FString Error;
    if (!Player.IsValid() || !AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
    {
        return;
    }
    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>(this);
    Settings->Profile = EAbilityCharacterStrikeProfile::SwordShieldActor;
    Settings->Strike = Strike;
    Settings->HitEffect = Player->SwordConfig ? Player->SwordConfig->HitEffect.Get() : nullptr;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [this, Session = GetExecutionSession()]()
    {
        return Session && Session->IsSourceCurrent() && GetExecutionSession() == Session && IsActive() &&
               Player.IsValid() && !Player->IsDungeonDowned() && !Player->IsDead_Implementation();
    };
    Callbacks.BeforeActorDamage = [this](AActor* Target)
    {
        ReportOffensiveAttack(Target);
    };
    AbilityCharacterActions::ExecuteStrike(Source, MakeDamageEffectParamsFromClassDefaults(), *Settings,
                                           Player->GetActorLocation(), LockedForward, Callbacks);
}
// 清除定时器、技能效果和动画占用，按角色状态恢复移动并结束 GAS 激活。
void USwordShieldAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                     FGameplayAbilityActivationInfo A, bool Replicate, bool Cancelled)
{
    CloseExecutionSession(Cancelled);
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timer);
    // 只清理本次能力持有的格挡句柄和蒙太奇，避免影响其他能力。
    if (I && I->AbilitySystemComponent.IsValid())
    {
        if (GuardHandle.IsValid())
            I->AbilitySystemComponent->RemoveActiveGameplayEffect(GuardHandle);
        if (I->AbilitySystemComponent->GetAnimatingAbility() == this)
            I->AbilitySystemComponent->CurrentMontageStop(.08f);
    }
    GuardHandle.Invalidate();
    if (bOwnMovement && Player.IsValid())
    {
        if (!Player->IsDungeonDowned() && !Player->IsDead_Implementation())
            Player->GetCharacterMovement()->SetMovementMode(EMovementMode(PreviousMovement));
        if (Mode == EMode::Combo)
        {
            NextCombo = Cancelled ? 0 : (CurrentCombo + 1) % 3;
            LastComboEnd = GetWorld()->GetTimeSeconds();
        }
    }
    bOwnMovement = false;
    Player.Reset();
    Super::EndAbility(H, I, A, Replicate, Cancelled);
}
// 按技能模式或动作事件生成界面描述。
FString USwordShieldAbility::GetDescription(int32)
{
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
        return TEXT("<Title>盾防</>\n<Default>短暂举盾，期间格挡伤害并减半；不能同时攻击或移动。</>");
    if (Mode == EMode::Spin)
        return TEXT("<Title>旋转斩</>\n<Default>原地挥剑，对周围敌人各造成一次物理伤害。</>");
    return TEXT("<Title>剑盾连击</>\n<Default>连续攻击衔接三段挥砍，停止攻击后重置连段。</>");
}
