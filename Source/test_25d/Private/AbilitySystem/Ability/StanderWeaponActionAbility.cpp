/**
 * 激活链：装备组件授予Spec -> 输入触发GAS -> 服务器CanActivateAbility/Commit -> 锁定朝向 -> 可选Timeline任务或旧定时器 -> AdvanceAt按源动画位置执行事件 -> EndAbility。
 * 生命周期：InstancedPerActor的GA对象可重复激活，一次激活状态必须在结束时清理。EndAbility清定时器、格挡GE、蒙太奇及移动占用；取消时销毁记录的弹体，正常结束则由弹体自己的命中/寿命/代次负责收尾。
 * 网络：NetExecutionPolicy为ServerOnly，伤害与位移在权威端执行；不要把客户端本地GA实例活跃状态当作权威激活状态。人物/蒙太奇与弹体表现依赖各自已有复制通路，本类的事件数组与本次Projectiles记录不是独立复制快照。
 */
// 主要调用链：装备授予动作 GA -> CanActivateAbility -> ActivateAbility -> Advance -> Execute / Move；发射物交由 AStanderWeaponProjectile 推进，EndAbility 清理占用。
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
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
        Execute(E);
        if (!IsActive())
            return;
    }

    LastPosition = Pos;
}
// 按事件类型处理格挡、发射物或近战命中，表现事件只生成特效。
void UStanderWeaponActionAbility::Execute(const FStanderWeaponActionEvent& E)
{
    auto* ASC = CurrentActorInfo->AbilitySystemComponent.Get();
    FVector Origin = Player->GetActorLocation();
    if (E.Effect && E.Kind != EStanderWeaponActionKind::Projectile)
        Player->ShowSwordImpact(E.Effect, Origin + Forward * 60);
    if (E.Kind == EStanderWeaponActionKind::Guard)
    {
        GuardEnds = E.Time + E.Duration;
        if (GuardHandle.IsValid())
            ASC->RemoveActiveGameplayEffect(GuardHandle);
        auto S = ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(), 1, ASC->MakeEffectContext());
        if (S.IsValid())
        {
            // 统一时间轴路径按动画窗口移除，不能让固定秒数GE在慢放时提前失效。
            S.Data->SetDuration(TimelineTask ? UGameplayEffect::INFINITE_DURATION : E.Duration, true);
            GuardHandle = ASC->ApplyGameplayEffectSpecToSelf(*S.Data);
        }
        return;
    }
    auto Params = MakeDamageEffectParamsFromClassDefaults();
    Params.BaseDamage = E.Damage;
    Params.DebuffChance = 0;
    Params.bIsRadialDamage = false;
    Params.DeathImpulse = UStanderCombatConfig::WorldDeathImpulse(E.DeathImpulse, Forward);
    // 按锁定朝向和发射偏角选择辅助瞄准目标，再生成独立推进的投射物。
    if (E.Kind == EStanderWeaponActionKind::Projectile)
    {
        FVector LaunchDirection = Forward.RotateAngleAxis(E.LaunchYaw, FVector::UpVector);
        const FVector Start =
            E.SocketName.IsNone() ? Origin + LaunchDirection * 60 : Player->GetMesh()->GetSocketLocation(E.SocketName);
        // Hand offsets and airborne poses must aim at the same combat target as the
        // locked heading. Keep assistance within a narrow cone; walls remain authoritative.
        const float Range = E.ProjectileSpeed * E.ProjectileLifetime;
        float Nearest = Range * Range;
        for (const auto& Target : PureMassCombat::Gather(GetWorld(), Player.Get(), Origin, Range))
        {
            FVector Location;
            if (!PureMassCombat::Resolve(GetWorld(), Player.Get(), Target, Location) ||
                FMath::Abs(Location.Z - Origin.Z) > 250.f ||
                !UStanderWeaponActionConfig::IsInAimCone(Forward.RotateAngleAxis(E.LaunchYaw, FVector::UpVector),
                                                         Location - Origin) ||
                !PureMassCombat::Visible(GetWorld(), Player.Get(), Origin, Target))
                continue;
            if (auto* D = ADungeonRunDirector::Find(GetWorld()); D && D->GetRoomIndex(Location) != OriginRoom)
                continue;
            const float Distance = FVector::DistSquared2D(Origin, Location);
            if (Distance < Nearest)
            {
                Nearest = Distance;
                LaunchDirection = (Location - Start).GetSafeNormal();
            }
        }
        Params.DeathImpulse =
            UStanderCombatConfig::WorldDeathImpulse(E.DeathImpulse, LaunchDirection.GetSafeNormal2D());
        FActorSpawnParameters Spawn;
        Spawn.Owner = Player.Get();
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Projectile = GetWorld()->SpawnActor<AStanderWeaponProjectile>(Start, LaunchDirection.Rotation(), Spawn);
        if (Projectile)
        {
            Projectile->Initialize(Player.Get(), E, Params, LaunchDirection);
            Projectiles.Add(Projectile);
        }
        return;
    }

    if (E.Kind != EStanderWeaponActionKind::Melee)
        return;
    // 同一事件内按统一战斗目标身份去重；Actor 与 Mass 后续分别走既有伤害入口。
    TSet<FPureMassCombatTarget> Seen;
    for (const auto& T : PureMassCombat::Gather(GetWorld(), Player.Get(), Origin, E.Radius + 100))
    {
        FVector L;
        if (Seen.Contains(T))
            continue;
        Seen.Add(T);
        if (!PureMassCombat::Resolve(GetWorld(), Player.Get(), T, L) || FMath::Abs(Origin.Z - L.Z) > 180.f ||
            FVector::DistSquared2D(Origin, L) > FMath::Square(E.Radius) ||
            (E.Angle < 360 && FVector::DotProduct(Forward, (L - Origin).GetSafeNormal2D()) <
                                  FMath::Cos(FMath::DegreesToRadians(E.Angle * .5f))) ||
            !PureMassCombat::Visible(GetWorld(), Player.Get(), Origin, T))
            continue;
        if (auto* D = ADungeonRunDirector::Find(GetWorld()); D && D->GetRoomIndex(Origin) != D->GetRoomIndex(L))
            continue;
        const FVector HitForward = E.Angle >= 359.f ? (L - Origin).GetSafeNormal2D() : Forward;
        Params.DeathImpulse = UStanderCombatConfig::WorldDeathImpulse(E.DeathImpulse, HitForward);
        if (T.IsActor())
        {
            auto P = MakeDamageEffectParamsFromClassDefaults(T.Actor.Get());
            P.BaseDamage = E.Damage;
            P.DeathImpulse = Params.DeathImpulse;
            P.DebuffChance = 0;
            P.bIsRadialDamage = false;
            if (P.TargetAbilitySystemComponent)
                UMyAbilitySystemLibrary::ApplyDamageEffect(P);
        }
        else if (auto* M = GetWorld()->GetSubsystem<UPureMassSubsystem>())
        {
            FPureMassDamage D;
            D.Source = Player.Get();
            D.Amount = E.Damage;
            D.DamageType = DamageType;
            D.DeathImpulse = Params.DeathImpulse;
            M->ApplyDamage(T.EntityId, D);
        }
        if (!IsActive() || !Player.IsValid())
            return;
    }
}
// 清除定时器、技能效果和动画占用，按角色状态恢复移动并结束 GAS 激活。
void UStanderWeaponActionAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                             FGameplayAbilityActivationInfo A, bool R, bool C)
{
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
    // 用上次与本次动画进度计算增量，位移受阻后保留该事件的阻挡状态。
    const float Before = FMath::Clamp((LastPosition - E.Time) / E.Duration, 0.f, 1.f);
    const float After = FMath::Clamp((Position - E.Time) / E.Duration, 0.f, 1.f);
    if (After <= Before || !Player.IsValid())
        return;
    const int32 Key = &E - ActionConfig->Events.GetData();
    float Distance = BlockedMoves.Contains(Key) ? 0.f : E.MoveDistance * (After - Before);
    const float Height = UStanderWeaponActionConfig::JumpHeight(After, E.MoveHeight);
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Distance) / 20.f));
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        const FVector From = Player->GetActorLocation();
        FVector Next = From + Forward * (Distance / Steps);
        auto* Director = ADungeonRunDirector::Find(GetWorld());
        if (Director && (OriginRoom == INDEX_NONE || Director->GetRoomIndex(Next) != OriginRoom))
        {
            BlockedMoves.Add(Key);
            Next = From;
            Distance = 0;
        }
        const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float GroundCenter = From.Z - JumpOffset;
        FHitResult Floor;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(WeaponMoveFloor), false, Player.Get());
        FVector Probe(Next.X, Next.Y, GroundCenter);
        if (!GetWorld()->LineTraceSingleByObjectType(Floor, Probe + FVector(0, 0, 30), Probe - FVector(0, 0, Half + 70),
                                                     FCollisionObjectQueryParams(ECC_WorldStatic), Q) ||
            Floor.ImpactNormal.Z < .7f || FMath::Abs(Floor.ImpactPoint.Z + Half + 2 - GroundCenter) > 35)
        {
            BlockedMoves.Add(Key);
            return;
        }
        Next.Z = Floor.ImpactPoint.Z + Half + 2 + Height;
        FHitResult Hit;
        Player->SetActorLocation(Next, true, &Hit);
        JumpOffset = FMath::Max(0.f, Player->GetActorLocation().Z - (Floor.ImpactPoint.Z + Half + 2));
        if (Hit.bBlockingHit)
        {
            BlockedMoves.Add(Key);
            return;
        }
    }
}
