// 主要调用链：GAS 激活检查 -> ActivateAbility -> StartStrike -> 定时 Advance -> MoveDash / Hit；GuardHit 延迟触发反击，EndAbility 统一清理。
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Roguelike/DungeonRunDirector.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderComboTag, "Abilities.Stander.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderGuardTag, "Abilities.Stander.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderSpinTag, "Abilities.Stander.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderDashTag, "Abilities.Stander.Dash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderLiftTag, "Abilities.Stander.Lift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderLiftCD, "Cooldown.Stander.Lift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderShieldBashTag, "Abilities.Stander.ShieldBash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderShieldBashCD, "Cooldown.Stander.ShieldBash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderBusyTag, "State.Stander.Busy");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderGuardCD, "Cooldown.Stander.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderSpinCD, "Cooldown.Stander.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderDashCD, "Cooldown.Stander.Dash");

// 设置该技能的默认执行策略、模式和激活约束。
UStanderAbility::UStanderAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationOwnedTags.AddTag(StanderBusyTag);
    ActivationBlockedTags.AddTag(StanderBusyTag);
    DebuffChance = 0;
    KnockbackChance = 0;
    DeathImpulseMagnitude = 1000;
    KnockbackForceMagnitude = 0;
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderComboAbility::UStanderComboAbility()
{
    SetAssetTags(FGameplayTagContainer(StanderComboTag));
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderGuardAbility::UStanderGuardAbility()
{
    Mode = EMode::Guard;
    SetAssetTags(FGameplayTagContainer(StanderGuardTag));
    CooldownIdentity = StanderGuardCD;
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderSpinAbility::UStanderSpinAbility()
{
    Mode = EMode::Spin;
    SetAssetTags(FGameplayTagContainer(StanderSpinTag));
    CooldownIdentity = StanderSpinCD;
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderDashAbility::UStanderDashAbility()
{
    Mode = EMode::Dash;
    SetAssetTags(FGameplayTagContainer(StanderDashTag));
    CooldownIdentity = StanderDashCD;
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderLiftAbility::UStanderLiftAbility()
{
    Mode = EMode::Lift;
    SetAssetTags(FGameplayTagContainer(StanderLiftTag));
    CooldownIdentity = StanderLiftCD;
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderShieldBashAbility::UStanderShieldBashAbility()
{
    Mode = EMode::ShieldBash;
    SetAssetTags(FGameplayTagContainer(StanderShieldBashTag));
    CooldownIdentity = StanderShieldBashCD;
}
// 从当前 Avatar 的角色配置读取对应剑盾数据资产。
const UStanderCombatConfig* UStanderAbility::Config(const FGameplayAbilityActorInfo* I) const
{
    auto* P = I ? Cast<AStanderPlayer>(I->AvatarActor.Get()) : nullptr;
    return P ? Cast<UStanderCombatConfig>(P->SwordConfig) : nullptr;
}
// 根据当前攻击模式读取法力消耗。
float UStanderAbility::Cost(const UStanderCombatConfig* C) const
{
    return Mode == EMode::Lift         ? C->LiftMana
           : Mode == EMode::ShieldBash ? C->ShieldBashMana
           : Mode == EMode::Spin       ? C->SpinMana
           : Mode == EMode::Dash       ? C->DashMana
                                       : 0;
}
// 根据当前攻击模式读取冷却时长。
float UStanderAbility::Cooldown(const UStanderCombatConfig* C) const
{
    return Mode == EMode::Lift         ? C->LiftCooldown
           : Mode == EMode::ShieldBash ? C->ShieldBashCooldown
           : Mode == EMode::Guard      ? C->GuardCooldown
           : Mode == EMode::Spin       ? C->SpinCooldown
           : Mode == EMode::Dash       ? C->DashCooldown
                                       : 0;
}
// 激活前检查角色状态、配置及伤害依赖，再交给 GAS 检查通用条件。
bool UStanderAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                         const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                         FGameplayTagContainer* R) const
{
    auto* P = I ? Cast<AStanderPlayer>(I->AvatarActor.Get()) : nullptr;
    const auto* C = Config(I);
    if (!P || !C || !C->ValidateStander() || !I->AbilitySystemComponent.IsValid() || P->IsDungeonDowned() ||
        P->IsDead_Implementation() || P->bIsStunned || !P->GetCharacterMovement()->IsMovingOnGround() ||
        !P->GetMesh()->GetAnimInstance() || !DamageEffectClass || !DamageType.IsValid())
        return false;
    return Super::CanActivateAbility(H, I, S, T, R);
}
// 根据冷却标签判断当前技能是否仍处于冷却。
bool UStanderAbility::CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                    FGameplayTagContainer*) const
{
    return I && I->AbilitySystemComponent.IsValid() &&
           (!CooldownIdentity.IsValid() || !I->AbilitySystemComponent->HasMatchingGameplayTag(CooldownIdentity));
}
// 服务器施加带冷却标签的限时 GE。
void UStanderAbility::ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                    FGameplayAbilityActivationInfo) const
{
    const auto* C = Config(I);
    if (!C || !I->IsNetAuthority() || !CooldownIdentity.IsValid() || Cooldown(C) <= 0)
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordTimedEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(Cooldown(C), true);
        Spec.Data->DynamicGrantedTags.AddTag(CooldownIdentity);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}
// 读取 ASC 的当前法力，判断是否足够支付本次消耗。
bool UStanderAbility::CheckCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                FGameplayTagContainer*) const
{
    const auto* C = Config(I);
    return C && I->AbilitySystemComponent.IsValid() &&
           I->AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= Cost(C);
}
// 服务器通过 SetByCaller 法力 GE 扣除消耗。
void UStanderAbility::ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                FGameplayAbilityActivationInfo) const
{
    const auto* C = Config(I);
    if (!C || !I->IsNetAuthority() || Cost(C) <= 0)
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordManaEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.SwordShield.Mana")), -Cost(C));
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}
// 重置本段命中状态和起始时间，通过 ASC 播放选定攻击蒙太奇。
bool UStanderAbility::StartStrike(const FSwordStrike& NewStrike)
{
    Strike = NewStrike;
    bHit = false;
    Started = GetWorld()->GetTimeSeconds();
    StrikeTimeline = {};
    StrikeDuration = Strike.Montage->GetPlayLength() / Strike.PlayRate;
    AppliedTimelineRate = Strike.PlayRate;
    if (Timeline)
    {
        FString Error;
        if (!Timeline->ResolveMontage(Strike.Montage, StrikeTimeline, Error, true))
        {
            return false;
        }
        AppliedTimelineRate = StrikeTimeline.RateAt(0.f);
        StrikeDuration = StrikeTimeline.ElapsedAt(Strike.Montage->GetPlayLength());
    }
    return CurrentActorInfo->AbilitySystemComponent->PlayMontage(this, CurrentActivationInfo, Strike.Montage,
                                                                 AppliedTimelineRate) > 0;
}
// 提交技能消耗和冷却，锁定朝向与移动，播放蒙太奇并启动推进定时器。
void UStanderAbility::ActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                      FGameplayAbilityActivationInfo A, const FGameplayEventData*)
{
    bOwnMovement = bHit = bCounter = bCounterPending = bDashBlocked = false;
    DashApplied = 0;
    GuardHandle.Invalidate();
    GuardListener.Reset();
    Player = I ? Cast<AStanderPlayer>(I->AvatarActor.Get()) : nullptr;
    const auto* C = Config(I);
    if (!Player.IsValid() || !C || !C->ValidateStander() || !I->IsNetAuthority())
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    CurrentCombo =
        USwordShieldConfig::ResolveComboIndex(NextCombo, LastComboEnd, GetWorld()->GetTimeSeconds(), C->ComboReset);
    FSwordStrike Selected = Mode == EMode::Lift         ? C->Lift
                            : Mode == EMode::ShieldBash ? C->ShieldBash
                            : Mode == EMode::Spin       ? C->Spin
                            : Mode == EMode::Dash       ? C->Dash
                                                        : C->Combo[CurrentCombo];
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
    {
        Selected.Montage = C->GuardMontage;
        Selected.PlayRate = Selected.Montage->GetPlayLength() / C->GuardDuration;
    }
    if (Timeline)
    {
        FAbilityTimelineSnapshot Preview;
        FString Error;
        if (!Timeline->ResolveMontage(Selected.Montage, Preview, Error, true) ||
            (Mode == EMode::Guard && !Timeline->ResolveMontage(C->Counter.Montage, Preview, Error, true)))
        {
            EndAbility(H, I, A, true, true);
            return;
        }
    }
    if (!CommitAbility(H, I, A) || !IsActive() || !Player.IsValid())
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    LockedForward = Player->GetActorForwardVector().GetSafeNormal2D();
    if (auto* PC = I->PlayerController.Get(); PC && PC->IsLocalController())
    {
        FHitResult HitResult;
        if (PC->GetHitResultUnderCursor(ECC_Visibility, false, HitResult) && !HitResult.ImpactPoint.ContainsNaN())
        {
            FVector Aim = (HitResult.ImpactPoint - Player->GetActorLocation()).GetSafeNormal2D();
            if (!Aim.IsNearlyZero())
                LockedForward = Aim;
        }
    }
    Player->SetActorRotation(LockedForward.Rotation());
    auto* Movement = Player->GetCharacterMovement();
    PreviousMovement = Movement->MovementMode;
    bOwnMovement = true;
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
    OriginRoom = INDEX_NONE;
    if (auto* D = ADungeonRunDirector::Find(GetWorld()))
        OriginRoom = D->GetRoomIndex(Player->GetActorLocation());
    if (!StartStrike(Selected))
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
    {
        auto* ASC = I->AbilitySystemComponent.Get();
        FAbilitySourceToken Source;
        FString Error;
        if (AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
        {
            GuardHandle = AbilityCharacterActions::OpenGuardWindow(Source);
        }
        GuardListener = ASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &ThisClass::GuardHit);
    }
    GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Advance, .01f, true);
}
// 确认伤害上下文标记了格挡且包含正值伤害类型。
bool UStanderAbility::IsCounterDamage(const FGameplayEffectSpec& Spec)
{
    return AbilityCharacterActions::IsBlockedDamage(Spec);
}
// 额外检查 Mass 伤害 GE 类型和命中上下文中的实体编号。
bool UStanderAbility::IsMassCounterDamage(const FGameplayEffectSpec& Spec)
{
    const FHitResult* HitResult = Spec.GetContext().GetHitResult();
    return Spec.Def && Spec.Def->IsA<UPureMassIncomingDamageEffect>() && IsCounterDamage(Spec) && HitResult &&
           HitResult->Item > 0;
}
// 监听自身受击 GE，仅对合法格挡伤害标记延迟反击。
void UStanderAbility::GuardHit(UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
{
    if (!IsActive() || Mode != EMode::Guard || bCounter || bCounterPending || !GuardHandle.IsValid() ||
        !Player.IsValid() || Player->IsDungeonDowned())
        return;
    auto* Source = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
    auto* Attacker = Source ? Source->GetAvatarActor() : nullptr;
    if (IsValid(Attacker) && UMyAbilitySystemLibrary::IsNotFriend(Player.Get(), Attacker) &&
        ADungeonRunDirector::AllowsCombat(Player.Get(), Attacker) && IsCounterDamage(Spec))
        bCounterPending = true; // Defer montage switching until after damage callbacks unwind.
    else if (IsMassCounterDamage(Spec))
    {
        const FHitResult* HitResult = Spec.GetContext().GetHitResult();
        FVector Location;
        if (HitResult && HitResult->Item > 0 &&
            PureMassCombat::Resolve(GetWorld(), Player.Get(), FPureMassCombatTarget(HitResult->Item), Location))
        {
            auto* Director = ADungeonRunDirector::Find(GetWorld());
            if (!Director || Director->GetRoomIndex(Player->GetActorLocation()) == Director->GetRoomIndex(Location))
                bCounterPending = true;
        }
    }
}
// 按动画位移进度分步突进，检查房间、地面和碰撞并在受阻时停止。
void UStanderAbility::MoveDash(float Position)
{
    const auto* C = Config(CurrentActorInfo);
    FAbilitySourceToken Source;
    FString Error;
    if (!C || bDashBlocked || !Player.IsValid() ||
        !AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
    {
        return;
    }
    FAbilityCharacterMoveState State;
    State.AppliedDistance = DashApplied;
    State.bBlocked = bDashBlocked;
    const auto Session = GetExecutionSession();
    AbilityMovementActions::GroundDash(
        Source, LockedForward, C->DashFraction(Position) * C->DashDistance, OriginRoom, State,
        [this, Session]()
        {
            return Session && Session->IsSourceCurrent() && GetExecutionSession() == Session && IsActive();
        });
    if (GetExecutionSession() == Session)
    {
        DashApplied = State.AppliedDistance;
        bDashBlocked = State.bBlocked;
    }
}
// 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
void UStanderAbility::Advance()
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
    const auto* C = Config(CurrentActorInfo);
    if (!C || !Anim || !Anim->Montage_IsActive(Strike.Montage))
    {
        Stop(!bHit);
        return;
    }
    const float Position = Anim->Montage_GetPosition(Strike.Montage);
    // Reuse the shared phase mapping while preserving the legacy guard/counter lifecycle.
    if (!StrikeTimeline.Phases.IsEmpty())
    {
        const float Rate = StrikeTimeline.RateAt(Position);
        auto* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
        if (Rate > 0.f && Rate != AppliedTimelineRate && ASC->GetAnimatingAbility() == this)
        {
            ASC->CurrentMontageSetPlayRate(Rate);
            AppliedTimelineRate = Rate;
        }
    }
    // 反击在定时器阶段切换蒙太奇，避开受击委托的重入调用栈。
    if (Mode == EMode::Guard && !bCounter)
    {
        if (bCounterPending)
        {
            bCounter = true;
            bCounterPending = false;
            auto* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
            ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(GuardListener);
            GuardListener.Reset();
            ASC->RemoveActiveGameplayEffect(GuardHandle);
            GuardHandle.Invalidate();
            if (!IsActive() || !Player.IsValid())
                return;
            if (!StartStrike(C->Counter))
                Stop(true);
            return;
        }
        if (GetWorld()->GetTimeSeconds() - Started >= StrikeDuration)
            Stop(false);
        return;
    }
    if (Mode == EMode::Dash)
        MoveDash(Position);
    if (!IsActive() || !Player.IsValid())
        return;
    // 仅允许接触时间附近结算一次，跳过过期窗口时取消以免补发滞后伤害。
    if (!bHit && Position >= Strike.Contact)
    {
        if (Position > Strike.Contact + .15f)
        {
            Stop(true);
            return;
        }
        bHit = true;
        const TWeakObjectPtr<UStanderAbility> WeakThis(this);
        if (!DispatchExecutionAction(TEXT("Stander.Strike"), CurrentCombo,
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
    }
    if (IsActive() && GetWorld()->GetTimeSeconds() - Started > StrikeDuration + .2f)
        Stop(!bHit);
}
// 筛选范围内合法且无遮挡的目标，应用伤害并播放命中表现。
void UStanderAbility::Hit()
{
    if (!Player.IsValid())
    {
        return;
    }
    const auto* C = Config(CurrentActorInfo);
    if (!C || !C->ValidateDeathImpulses())
    {
        return;
    }
    const FVector LocalImpulse = Mode == EMode::Lift         ? C->LiftDeathImpulse
                                 : Mode == EMode::ShieldBash ? C->ShieldBashDeathImpulse
                                 : Mode == EMode::Guard      ? C->CounterDeathImpulse
                                 : Mode == EMode::Spin       ? C->SpinDeathImpulse
                                 : Mode == EMode::Dash       ? C->DashDeathImpulse
                                                             : C->ComboDeathImpulses[CurrentCombo];
    FAbilitySourceToken Source;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
    {
        return;
    }
    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>(this);
    Settings->Profile = EAbilityCharacterStrikeProfile::StanderMixed;
    Settings->Strike = Strike;
    Settings->LocalDeathImpulse = LocalImpulse;
    Settings->bRadialImpulse = Mode == EMode::Spin;
    Settings->SwingEffect = C->SwingEffect;
    Settings->HitEffect = C->HitEffect;
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
void UStanderAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                 FGameplayAbilityActivationInfo A, bool Replicate, bool Cancelled)
{
    CloseExecutionSession(Cancelled);
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timer);
    // 只清理本次能力持有的格挡句柄和蒙太奇，避免影响其他能力。
    if (I && I->AbilitySystemComponent.IsValid())
    {
        auto* ASC = I->AbilitySystemComponent.Get();
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(GuardListener);
        GuardListener.Reset();
        if (GuardHandle.IsValid())
        {
            auto Handle = GuardHandle;
            GuardHandle.Invalidate();
            ASC->RemoveActiveGameplayEffect(Handle);
        }
        if (ASC->GetAnimatingAbility() == this)
            ASC->CurrentMontageStop(.08f);
    }
    if (bOwnMovement && Player.IsValid())
    {
        bOwnMovement = false;
        if (!Player->IsDungeonDowned() && !Player->IsDead_Implementation())
            Player->GetCharacterMovement()->SetMovementMode(EMovementMode(PreviousMovement));
        if (Mode == EMode::Combo)
        {
            NextCombo = Cancelled ? 0 : (CurrentCombo + 1) % 3;
            LastComboEnd = GetWorld()->GetTimeSeconds();
        }
    }
    Player.Reset();
    bCounterPending = false;
    Super::EndAbility(H, I, A, Replicate, Cancelled);
}
// 按技能模式或动作事件生成界面描述。
FString UStanderAbility::GetDescription(int32)
{
    if (Mode == EMode::Lift)
        return TEXT("<Title>上挑斩</>\n<Default>向前上挑斩击，击杀时将敌人向上抛起。</>");
    if (Mode == EMode::ShieldBash)
        return TEXT("<Title>盾击</>\n<Default>近距离盾击，击杀时将敌人向前击飞。</>");
    // 格挡窗口独立持有 GE；结束或进入反击时必须移除，避免状态残留。
    if (Mode == EMode::Guard)
        return TEXT("<Title>盾防反击</>\n<Default>短暂举盾减半伤害；受到敌方攻击后反击一次。</>");
    if (Mode == EMode::Spin)
        return TEXT("<Title>回旋斩</>\n<Default>挥剑攻击周围敌人，每个目标受到一次物理伤害。</>");
    if (Mode == EMode::Dash)
        return TEXT("<Title>突进斩</>\n<Default>朝锁定方向冲刺并斩击；障碍物阻挡位移。</>");
    return TEXT("<Title>Stander剑盾连击</>\n<Default>连续施放衔接三段挥砍，停顿后重置。</>");
}
