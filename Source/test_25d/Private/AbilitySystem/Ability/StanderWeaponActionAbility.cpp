/**
 * 激活链：装备组件授予Spec -> 输入触发GAS -> 服务器CanActivateAbility/Commit -> 锁定朝向 -> 可选Timeline任务或旧定时器 -> AdvanceAt按源动画位置执行事件 -> EndAbility。
 * 生命周期：InstancedPerActor的GA对象可重复激活，一次激活状态必须在结束时清理。EndAbility清定时器、格挡GE、蒙太奇及移动占用；取消时销毁记录的弹体，正常结束则由弹体自己的命中/寿命/代次负责收尾。
 * 网络：NetExecutionPolicy为ServerOnly，伤害与位移在权威端执行；不要把客户端本地GA实例活跃状态当作权威激活状态。人物/蒙太奇与弹体表现依赖各自已有复制通路，本类的事件数组与本次Projectiles记录不是独立复制快照。
 */
// 主要调用链：装备授予动作 GA -> CanActivateAbility -> ActivateAbility -> Advance -> Execute / Move；发射物交由 AStanderWeaponProjectile 推进，EndAbility 清理占用。
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Timeline/AbilityTask_RunTimeline.h"
#include "Actor/StanderWeaponProjectile.h"
#include "Player/StanderCombatConfig.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Roguelike/DungeonRunDirector.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

// 根据冷却标签判断当前技能是否仍处于冷却。
bool UStanderWeaponActionAbility::CheckCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                                FGameplayTagContainer*) const
{
    return ActionConfig && I && I->AbilitySystemComponent.IsValid() &&
           (!ActionConfig->CooldownTag.IsValid() ||
            !I->AbilitySystemComponent->HasMatchingGameplayTag(ActionConfig->CooldownTag));
}
// 服务器施加带冷却标签的限时 GE。
void UStanderWeaponActionAbility::ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                                FGameplayAbilityActivationInfo) const
{
    const auto* C = ActionConfig.Get();
    if (!C || !I->IsNetAuthority() || !ActionConfig->CooldownTag.IsValid() || C->Cooldown <= 0)
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordTimedEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetDuration(C->Cooldown, true);
        Spec.Data->DynamicGrantedTags.AddTag(ActionConfig->CooldownTag);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}
// 读取 ASC 的当前法力，判断是否足够支付本次消耗。
bool UStanderWeaponActionAbility::CheckCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                            FGameplayTagContainer*) const
{
    const auto* C = ActionConfig.Get();
    return C && I->AbilitySystemComponent.IsValid() &&
           I->AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= C->ManaCost;
}
// 服务器通过 SetByCaller 法力 GE 扣除消耗。
void UStanderWeaponActionAbility::ApplyCost(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* I,
                                            FGameplayAbilityActivationInfo) const
{
    const auto* C = ActionConfig.Get();
    if (!C || !I->IsNetAuthority() || C->ManaCost <= 0)
        return;
    auto* ASC = I->AbilitySystemComponent.Get();
    auto Spec = ASC->MakeOutgoingSpec(USwordManaEffect::StaticClass(), 1, ASC->MakeEffectContext());
    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.SwordShield.Mana")),
                                           -C->ManaCost);
        ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
}

// 在蓝图默认对象上同步动作配置的技能标签。
void UStanderWeaponActionAbility::InitializeActionIdentity()
{
    // Editor-authored Blueprint CDO defaults must be serialized after assigning the
    // data asset. UE 5.8's SetAssetTags is constructor-only and ensures in PostLoad.
    if (HasAnyFlags(RF_ClassDefaultObject) && ActionConfig && ActionConfig->ActionTag.IsValid())
    {
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        AbilityTags = FGameplayTagContainer(ActionConfig->ActionTag);
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
}
// 加载完成后同步动作身份。
void UStanderWeaponActionAbility::PostLoad()
{
    Super::PostLoad();
    InitializeActionIdentity();
}
// 编辑器中重算蒙太奇长度、标脏并检查长度一致性；运行时返回失败。
bool UStanderWeaponActionAbility::RebuildActionMontage(UAnimMontage* Montage)
{
#if WITH_EDITOR
    if (!IsValid(Montage))
        return false;
    Montage->Modify();
    Montage->SetCompositeLength(Montage->CalculateSequenceLength());
    Montage->PostEditChange();
    Montage->MarkPackageDirty();
    return FMath::IsNearlyEqual(Montage->GetPlayLength(), Montage->CalculateSequenceLength(), .001f);
#else
    return false;
#endif
}
// 设置该技能的默认执行策略、模式和激活约束。
UStanderWeaponActionAbility::UStanderWeaponActionAbility()
{
    const auto Busy = FGameplayTag::RequestGameplayTag(TEXT("State.Stander.Busy"));
    ActivationOwnedTags.AddTag(Busy);
    ActivationBlockedTags.AddTag(Busy);
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    DebuffChance = KnockbackChance = 0;
}
// 激活前检查角色状态、配置及伤害依赖，再交给 GAS 检查通用条件。
bool UStanderWeaponActionAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                                     const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                                     FGameplayTagContainer* R) const
{
    auto* P = I ? Cast<AStanderPlayer>(I->AvatarActor.Get()) : nullptr;
    auto* Equipment =
        P && P->GetPlayerState() ? P->GetPlayerState()->FindComponentByClass<UEquipmentComponent>() : nullptr;
    return P && Equipment && Equipment->GetWeaponSet() && ActionConfig &&
           Equipment->GetWeaponSet()->Family == ActionConfig->WeaponFamily && ActionConfig->Validate() &&
           !P->IsDead_Implementation() && !P->IsDungeonDowned() && !P->bIsStunned &&
           P->GetCharacterMovement()->IsMovingOnGround() && P->GetMesh()->GetAnimInstance() && DamageType.IsValid() &&
           DamageEffectClass && Super::CanActivateAbility(H, I, S, T, R);
}
// 提交技能消耗和冷却，锁定朝向与移动，播放蒙太奇并启动推进定时器。
void UStanderWeaponActionAbility::ActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                                  FGameplayAbilityActivationInfo A, const FGameplayEventData*)
{
    Player = I ? Cast<AStanderPlayer>(I->AvatarActor.Get()) : nullptr;
    NextEvent = 0;
    LastPosition = 0;
    GuardEnds = -1;
    Projectiles.Reset();
    OwnMovement = false;
    JumpOffset = 0;
    BlockedMoves.Reset();
    if (!Player.IsValid() || !ActionConfig || !ActionConfig->Validate() || !I->IsNetAuthority() ||
        !CommitAbility(H, I, A))
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    Forward = Player->GetActorForwardVector().GetSafeNormal2D();
    if (auto* PC = I->PlayerController.Get(); PC && PC->IsLocalController())
    {
        FHitResult Hit;
        if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit) && !Hit.ImpactPoint.ContainsNaN())
        {
            const FVector Aim = (Hit.ImpactPoint - Player->GetActorLocation()).GetSafeNormal2D();
            if (!Aim.IsNearlyZero())
                Forward = Aim;
        }
    }
    Player->SetActorRotation(Forward.Rotation());
    OriginRoom = INDEX_NONE;
    if (auto* D = ADungeonRunDirector::Find(GetWorld()))
        OriginRoom = D->GetRoomIndex(Player->GetActorLocation());
    auto* M = Player->GetCharacterMovement();
    PreviousMovement = M->MovementMode;
    OwnMovement = true;
    M->StopMovementImmediately();
    M->DisableMovement();
    if (ActionConfig->Timeline)
    {
        TimelineTask =
            UAbilityTask_RunTimeline::RunAbilityTimeline(this, ActionConfig->Montage, ActionConfig->Timeline);
        TimelineTask->OnPosition.AddDynamic(this, &ThisClass::TimelinePosition);
        TimelineTask->OnFinished.AddDynamic(this, &ThisClass::TimelineFinished);
        TimelineTask->ReadyForActivation();
        return;
    }
    if (I->AbilitySystemComponent->PlayMontage(this, A, ActionConfig->Montage, ActionConfig->PlayRate) <= 0)
    {
        EndAbility(H, I, A, true, true);
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Advance, .01f, true);
}
// Task已经检查实例所有权；原Advance继续负责玩家死亡、事件和位移窗口。
void UStanderWeaponActionAbility::TimelinePosition(float SourcePosition)
{
    AdvanceAt(SourcePosition);
}

// 正常结束保留已发射弹体，取消沿用现有投射物/格挡/移动清理策略。
void UStanderWeaponActionAbility::TimelineFinished(bool bInterrupted)
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bInterrupted);
    }
}
// 定时读取蒙太奇位置，处理命中或事件窗口，并在失效、打断或结束时收尾。
void UStanderWeaponActionAbility::Advance()
{
    auto Stop = [this](bool C)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, C);
    };
    if (!IsActive())
        return;
    if (!Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation() || Player->bIsStunned)
    {
        Stop(true);
        return;
    }
    auto* Anim = Player->GetMesh()->GetAnimInstance();
    if (!Anim || !Anim->Montage_IsActive(ActionConfig->Montage))
    {
        Stop(LastPosition < ActionConfig->Montage->GetPlayLength() - .15f);
        return;
    }
    if (CurrentActorInfo->AbilitySystemComponent->GetAnimatingAbility() != this)
    {
        Stop(true);
        return;
    }
    float Pos = Anim->Montage_GetPosition(ActionConfig->Montage);
    AdvanceAt(Pos);
}

// Task可在自然混出期间继续提供源位置，不查询只包含活跃实例的Montage_GetPosition。
void UStanderWeaponActionAbility::AdvanceAt(float Pos)
{
    auto Stop = [this](bool C)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, C);
    };
    if (!IsActive())
    {
        return;
    }
    if (!Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation() || Player->bIsStunned)
    {
        Stop(true);
        return;
    }
    if (Pos < LastPosition)
    {
        Stop(true);
        return;
    }
    if (GuardHandle.IsValid() && Pos >= GuardEnds)
    {
        CurrentActorInfo->AbilitySystemComponent->RemoveActiveGameplayEffect(GuardHandle);
        GuardHandle.Invalidate();
    }
    for (const auto& E : ActionConfig->Events)
        if (E.Kind == EStanderWeaponActionKind::Move)
            Move(E, Pos);
    while (NextEvent < ActionConfig->Events.Num() && ActionConfig->Events[NextEvent].Time <= Pos)
    {
        auto E = ActionConfig->Events[NextEvent++];
        const float LateSeconds = TimelineTask ? TimelineTask->DurationBetween(E.Time, Pos) : (Pos - E.Time);
        if (LateSeconds > .15f)
        {
            Stop(true);
            return;
        }
        // 事件索引是本次动作的稳定调用序号；Timeline和旧动画时钟共用唯一执行入口。
        const TWeakObjectPtr<UStanderWeaponActionAbility> WeakThis(this);
        if (!DispatchExecutionAction(TEXT("Weapon.Action"), NextEvent - 1,
                                     [WeakThis](int32 Index)
                                     {
                                         if (auto* Self = WeakThis.Get();
                                             Self && Self->ActionConfig &&
                                             Self->ActionConfig->Events.IsValidIndex(Index))
                                         {
                                             const FStanderWeaponActionEvent Event = Self->ActionConfig->Events[Index];
                                             Self->Execute(Event);
                                         }
                                     }))
        {
            Stop(true);
            return;
        }
        if (!IsActive())
            return;
    }

    LastPosition = Pos;
}
// 按事件类型处理格挡、发射物或近战命中，表现事件只生成特效。
void UStanderWeaponActionAbility::Execute(const FStanderWeaponActionEvent& E)
{
    FAbilitySourceToken Source;
    FString Error;
    if (!Player.IsValid() || !AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
    {
        return;
    }
    auto* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
    const FVector Origin = Player->GetActorLocation();
    if (E.Effect && E.Kind != EStanderWeaponActionKind::Projectile)
    {
        Player->ShowSwordImpact(E.Effect, Origin + Forward * 60);
    }
    if (E.Kind == EStanderWeaponActionKind::Guard)
    {
        GuardEnds = E.Time + E.Duration;
        AbilityCharacterActions::CloseGuardWindow(ASC, GuardHandle);
        GuardHandle = AbilityCharacterActions::OpenGuardWindow(Source, TimelineTask ? -1.f : E.Duration);
        return;
    }
    const auto Params = MakeDamageEffectParamsFromClassDefaults();
    if (E.Kind == EStanderWeaponActionKind::Projectile)
    {
        if (auto* Projectile =
                AbilityCharacterActions::SpawnWeaponProjectile(Source, Params, E, Origin, Forward, OriginRoom))
        {
            Projectiles.Add(Projectile);
        }
        return;
    }
    if (E.Kind != EStanderWeaponActionKind::Melee)
    {
        return;
    }
    auto* Settings = NewObject<UAbilityCharacterStrikeNodeConfig>(this);
    Settings->Profile = EAbilityCharacterStrikeProfile::WeaponMixed;
    Settings->Strike.Damage = E.Damage;
    Settings->Strike.Radius = E.Radius;
    Settings->Strike.Angle = E.Angle;
    Settings->LocalDeathImpulse = E.DeathImpulse;
    Settings->bRadialImpulse = E.Angle >= 359.f;
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [this, Session = GetExecutionSession()]()
    {
        return Session && Session->IsSourceCurrent() && GetExecutionSession() == Session && IsActive() &&
               Player.IsValid();
    };
    AbilityCharacterActions::ExecuteStrike(Source, Params, *Settings, Origin, Forward, Callbacks);
}
// 清除定时器、技能效果和动画占用，按角色状态恢复移动并结束 GAS 激活。
void UStanderWeaponActionAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                             FGameplayAbilityActivationInfo A, bool R, bool C)
{
    CloseExecutionSession(C);
    // 先解绑并关闭事件入口，后面的蒙太奇停止不能再触发一次结束回调。
    if (TimelineTask)
    {
        auto* Task = TimelineTask.Get();
        TimelineTask = nullptr;
        Task->OnPosition.RemoveAll(this);
        Task->OnFinished.RemoveAll(this);
        Task->EndTask();
    }
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timer);
    if (C)
        for (auto P : Projectiles)
            if (P.IsValid())
                P->Destroy();
    Projectiles.Reset();
    // 只清理本次能力持有的格挡句柄和蒙太奇，避免影响其他能力。
    if (I && I->AbilitySystemComponent.IsValid())
    {
        auto* ASC = I->AbilitySystemComponent.Get();
        if (GuardHandle.IsValid())
            ASC->RemoveActiveGameplayEffect(GuardHandle);
        GuardHandle.Invalidate();
        if (ASC->GetAnimatingAbility() == this)
            ASC->CurrentMontageStop(.08f);
    }
    if (OwnMovement && Player.IsValid() && !Player->IsDungeonDowned() && !Player->IsDead_Implementation())
        Player->GetCharacterMovement()->SetMovementMode(JumpOffset > 5.f ? MOVE_Falling
                                                                         : EMovementMode(PreviousMovement));
    OwnMovement = false;
    Player.Reset();
    Super::EndAbility(H, I, A, R, C);
}
// 按技能模式或动作事件生成界面描述。
FString UStanderWeaponActionAbility::GetDescription(int32)
{
    if (!ActionConfig)
        return FString();
    FString Text = FString::Printf(TEXT("%s\nMana: %.0f  Cooldown: %.1fs"), *ActionConfig->DisplayName.ToString(),
                                   ActionConfig->ManaCost, ActionConfig->Cooldown);
    float MinimumDamage = MAX_flt, MaximumDamage = 0, Reach = 0, Distance = 0, Height = 0, Guard = 0;
    FAbilityTimelineSnapshot DisplayTimeline;
    FString TimelineError;
    const bool bTimelineDisplay = ActionConfig->Timeline && ActionConfig->Timeline->ResolveMontage(
                                                                ActionConfig->Montage, DisplayTimeline, TimelineError);
    int32 Hits = 0;
    for (const auto& E : ActionConfig->Events)
    {
        if (E.Kind == EStanderWeaponActionKind::Melee || E.Kind == EStanderWeaponActionKind::Projectile)
        {
            ++Hits;
            MinimumDamage = FMath::Min(MinimumDamage, E.Damage);
            MaximumDamage = FMath::Max(MaximumDamage, E.Damage);
            Reach = FMath::Max(
                Reach, E.Kind == EStanderWeaponActionKind::Melee ? E.Radius : E.ProjectileSpeed * E.ProjectileLifetime);
        }
        if (E.Kind == EStanderWeaponActionKind::Move)
        {
            Distance += FMath::Abs(E.MoveDistance);
            Height = FMath::Max(Height, E.MoveHeight);
        }
        if (E.Kind == EStanderWeaponActionKind::Guard)
            Guard += bTimelineDisplay
                         ? DisplayTimeline.ElapsedAt(E.Time + E.Duration) - DisplayTimeline.ElapsedAt(E.Time)
                         : E.Duration / ActionConfig->PlayRate;
    }
    if (Hits)
        Text += FString::Printf(TEXT("\nDamage per hit: %.0f-%.0f | Hit events: %d\nReach: %.1fm"), MinimumDamage,
                                MaximumDamage, Hits, Reach / 100.f);
    if (Guard > 0)
        Text += FString::Printf(TEXT("\nGuard window: %.1fs"), Guard);
    if (Distance > 0 || Height > 0)
        Text += FString::Printf(TEXT("\nMovement: %.1fm | Jump: %.1fm"), Distance / 100.f, Height / 100.f);
    return Text + TEXT("\nAvailable while this weapon is equipped.");
}

// 根据两次动画位置的差值分段移动，叠加跳跃弧线并阻止跨房间或穿过障碍。
void UStanderWeaponActionAbility::Move(const FStanderWeaponActionEvent& E, float Position)
{
    const float Before = FMath::Clamp((LastPosition - E.Time) / E.Duration, 0.f, 1.f);
    const float After = FMath::Clamp((Position - E.Time) / E.Duration, 0.f, 1.f);
    FAbilitySourceToken Source;
    FString Error;
    if (After <= Before || !Player.IsValid() || !AbilityExecutionIdentity::CaptureSource(Player.Get(), Source, Error))
    {
        return;
    }
    const int32 Key = &E - ActionConfig->Events.GetData();
    FAbilityCharacterMoveState State;
    State.bBlocked = BlockedMoves.Contains(Key);
    State.JumpOffset = JumpOffset;
    const auto Session = GetExecutionSession();
    AbilityMovementActions::WeaponArc(Source, Forward, E.MoveDistance, E.MoveHeight, Before, After, OriginRoom, State,
                                      [this, Session]()
                                      {
                                          return Session && Session->IsSourceCurrent() &&
                                                 GetExecutionSession() == Session && IsActive();
                                      });
    if (GetExecutionSession() == Session)
    {
        JumpOffset = State.JumpOffset;
        if (State.bBlocked)
        {
            BlockedMoves.Add(Key);
        }
    }
}
