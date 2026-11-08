// 主要调用链：公共预警 GA → Grux 阶段推进 → 连击后续段、冲锋扫掠或怒吼护甲 → 公共收尾。
#include "AbilitySystem/Ability/GruxEnemyAbility.h"
#include "Character/GruxEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Roguelike/DungeonRunDirector.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxCombo, "Abilities.Grux.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxCharge, "Abilities.Grux.Charge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxRoar, "Abilities.Grux.Roar");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxComboCD, "Cooldown.Grux.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxChargeCD, "Cooldown.Grux.Charge");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GruxRoarCD, "Cooldown.Grux.Roar");
UGruxComboAbility::UGruxComboAbility()
{
    Kind = EGruxAttackKind::Combo;
    SetAssetTags(FGameplayTagContainer(TAG_GruxCombo));
    CooldownIdentity = TAG_GruxComboCD;
}
UGruxChargeAbility::UGruxChargeAbility()
{
    Kind = EGruxAttackKind::Charge;
    SetAssetTags(FGameplayTagContainer(TAG_GruxCharge));
    CooldownIdentity = TAG_GruxChargeCD;
}
UGruxRoarAbility::UGruxRoarAbility()
{
    Kind = EGruxAttackKind::Roar;
    SetAssetTags(FGameplayTagContainer(TAG_GruxRoar));
    CooldownIdentity = TAG_GruxRoarCD;
}
// 验证共用攻击配置，以及后续段骨骼、冲锋时间窗口或怒吼增益参数。
bool UGruxAttackConfig::ValidateGrux(FString& Error) const
{
    if (!Tuning.Validate(Error))
        return false;
    if (Kind != EGruxAttackKind::Roar &&
        (!Followup.Validate(Error) || Followup.Montage->GetSkeleton() != Tuning.Montage->GetSkeleton()))
        return false;
    if (Kind == EGruxAttackKind::Charge &&
        (Tuning.Shape != EStoneGolemShape::Rectangle || !FMath::IsFinite(ChargeDuration) || ChargeDuration <= 0 ||
         ChargeDuration > 2 || Tuning.HitTime() + ChargeDuration > Tuning.RecoveryEnd ||
         Tuning.ContactTime + ChargeDuration * Tuning.PlayRate >=
             Tuning.Montage->GetPlayLength() - Tuning.Montage->BlendOut.GetBlendTime() - .02f))
    {
        Error = TEXT("Charge requires a corridor and enough active montage/recovery time");
        return false;
    }
    if (Kind == EGruxAttackKind::Roar && (!FMath::IsFinite(RoarArmor) || !FMath::IsFinite(RoarDuration) ||
                                          RoarArmor <= 0 || RoarDuration <= 0 || RoarDuration > 30))
    {
        Error = TEXT("Invalid roar armor or duration");
        return false;
    }
    return true;
}
// 检查攻击种类与专属配置，并防止怒吼护甲重复叠加后再检查公共条件。
bool UGruxEnemyAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           const FGameplayTagContainer* Source, const FGameplayTagContainer* Target,
                                           FGameplayTagContainer* Relevant) const
{
    const auto* Config = Cast<UGruxAttackConfig>(AttackConfig);
    FString Error;
    const auto* Actor = Info ? Cast<AGruxEnemyCharacter>(Info->AvatarActor.Get()) : nullptr;
    return Actor && Config && Config->Kind == Kind && Config->ValidateGrux(Error) &&
           (Kind != EGruxAttackKind::Roar || !Actor->HasRoarArmor()) &&
           Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}
// 重置阶段和受击集合，复制配置与基础伤害后进入公共预警流程。
void UGruxEnemyAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    Stage = 0;
    bCharging = false;
    ChargedTargets.Reset();
    AppliedArmor.Invalidate();
    const auto* Config = Cast<UGruxAttackConfig>(AttackConfig);
    FrozenConfig = Config ? DuplicateObject<UGruxAttackConfig>(Config, this) : nullptr;
    const auto* Actor = Info ? Cast<AGruxEnemyCharacter>(Info->AvatarActor.Get()) : nullptr;
    BaseDamageSnapshot = Actor && Actor->GetCombatConfig() ? Actor->GetCombatConfig()->BaseDamage : 0;
    Super::ActivateAbility(Handle, Info, Activation, Event);
}
// 在公共瞄准基础上，按静态障碍、地面与房间边界裁短冲锋走廊。
bool UGruxEnemyAbility::UpdateAim()
{
    if (!Super::UpdateAim())
        return false;
    if (Kind != EGruxAttackKind::Charge || Stage != 0 || !FrozenConfig)
        return true;
    const auto* Capsule = Golem->GetCapsuleComponent();
    const FVector Direction = FRotator(0, Yaw, 0).Vector();
    const FVector Start = Golem->GetActorLocation();
    float Length = FrozenConfig->Tuning.Length;
    FHitResult Hit;
    if (GetWorld()->SweepSingleByObjectType(
            Hit, Start, Start + Direction * Length, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 2),
            FCollisionQueryParams(SCENE_QUERY_STAT(GruxChargePlan), false, Golem.Get())))
        Length = FMath::Max(0.f, Hit.Distance - 2.f);
    const auto* Director = ADungeonRunDirector::Find(GetWorld());
    const int32 Room = Director ? Director->GetRoomIndex(Start) : INDEX_NONE;
    float Safe = 0;
    for (float D = FMath::Min(25.f, Length); D <= Length + .01f; D = FMath::Min(D + 25.f, Length))
    {
        FVector Floor;
        const FVector Point = Center + Direction * D;
        if (!StoneGolemGeometry::Project(GetWorld(), Golem.Get(), Point, Snapshot.HeightTolerance, Floor) ||
            (Director && Director->GetRoomIndex(Point) != Room))
            break;
        Safe = D;
        if (D >= Length)
            break;
    }
    if (Safe < 25)
        return false;
    Snapshot.Length = Safe;
    return true;
}
// 结束前段表现，换入冻结的后续段参数，并重新建立独立预警。
bool UGruxEnemyAbility::BeginFollowup()
{
    if (!FrozenConfig || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
        return false;
    ClearMontageDelegates();
    CurrentActorInfo->AbilitySystemComponent->CurrentMontageStop(.1f);
    Golem->FinishAttack(Serial);
    Stage = 1;
    Snapshot = FrozenConfig->Followup;
    SnapshotDamage = BaseDamageSnapshot * Snapshot.DamageMultiplier;
    Started = GetWorld()->GetTimeSeconds();
    LastWarning = Started;
    bLocked = false;
    bMontageStarted = false;
    bHit = false;
    bCharging = false;
    LockedSurface.Reset();
    LockedTriangles.Reset();
    if (!UpdateAim())
        return false;
    Serial = Golem->BeginAttack(Snapshot, Center, Yaw);
    return Serial != 0;
}
// 在首段后摇完成时切换后续段，再推进公共时序与冲锋运动。
void UGruxEnemyAbility::Advance()
{
    if (!IsActive())
        return;
    if (!FrozenConfig || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease) || Golem->bIsStunned)
    {
        Abort();
        return;
    }
    if (Stage == 0 && Kind != EGruxAttackKind::Roar && bHit &&
        GetWorld()->GetTimeSeconds() - Started >= Snapshot.RecoveryEnd)
    {
        if (!BeginFollowup())
        {
            Abort();
            return;
        }
    }
    Super::Advance();
    if (IsActive())
        ObserveMontage();
    if (IsActive() && bCharging)
        AdvanceCharge();
}
// 绑定当前阶段蒙太奇的中断回调，序号和租约防止旧回调取消新攻击。
void UGruxEnemyAbility::ObserveMontage()
{
    if (!bMontageStarted || !Golem.IsValid() || ObservedMontage == Snapshot.Montage)
        return;
    auto* Anim = Golem->GetMesh()->GetAnimInstance();
    if (!Anim)
        return;
    ObservedMontage = Snapshot.Montage;
    const int32 PhaseSerial = Serial, PhaseLease = Lease;
    auto Interrupted = [this, PhaseSerial, PhaseLease](UAnimMontage*, bool bInterrupted)
    {
        if (bInterrupted && IsActive() && Serial == PhaseSerial && Lease == PhaseLease && Golem.IsValid() &&
            Golem->IsAttackCurrent(PhaseSerial, PhaseLease))
            Abort();
    };
    FOnMontageBlendingOutStarted Blend;
    Blend.BindWeakLambda(this, Interrupted);
    FOnMontageEnded End;
    End.BindWeakLambda(this, Interrupted);
    Anim->Montage_SetBlendingOutDelegate(Blend, ObservedMontage.Get());
    Anim->Montage_SetEndDelegate(End, ObservedMontage.Get());
}
// 解除本技能记录的蒙太奇委托并释放弱引用。
void UGruxEnemyAbility::ClearMontageDelegates()
{
    if (ObservedMontage.IsValid() && Golem.IsValid())
        if (auto* Anim = Golem->GetMesh()->GetAnimInstance())
        {
            FOnMontageBlendingOutStarted Blend;
            FOnMontageEnded End;
            Anim->Montage_SetBlendingOutDelegate(Blend, ObservedMontage.Get());
            Anim->Montage_SetEndDelegate(End, ObservedMontage.Get());
        }
    ObservedMontage.Reset();
}
// 根据攻击类型启动护甲或冲锋；普通命中仍复用父类范围伤害。
void UGruxEnemyAbility::ResolveHit()
{
    if (Kind == EGruxAttackKind::Roar)
    {
        auto* Actor = Cast<AGruxEnemyCharacter>(Golem.Get());
        if (!Actor || !FrozenConfig)
        {
            Abort();
            return;
        }
        AppliedArmor = Actor->ApplyRoarArmor(FrozenConfig->RoarArmor, FrozenConfig->RoarDuration);
        if (!IsActive() || !Actor->IsAttackCurrent(Serial, Lease))
        {
            Actor->RemoveRoarArmor(AppliedArmor);
            return;
        }
        if (!AppliedArmor.IsValid())
        {
            Abort();
            return;
        }
        Actor->ShowImpact(Serial, Center);
        return;
    }
    if (Kind == EGruxAttackKind::Charge && Stage == 0)
    {
        ChargeStart = Golem->GetActorLocation();
        PreviousChargePosition = ChargeStart;
        ChargeStarted = GetWorld()->GetTimeSeconds();
        bCharging = true;
        const auto* Director = ADungeonRunDirector::Find(GetWorld());
        ChargeRoom = Director ? Director->GetRoomIndex(ChargeStart) : INDEX_NONE;
        Golem->ShowActiveFootprint(Serial);
        return;
    }
    Super::ResolveHit();
}
// 沿预警走廊贴地扫掠移动，对实际经过线段中的目标去重结算。
void UGruxEnemyAbility::AdvanceCharge()
{
    if (!FrozenConfig || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
    {
        Abort();
        return;
    }
    auto* Anim = Golem->GetMesh()->GetAnimInstance();
    if (!Anim || !Anim->Montage_IsActive(Snapshot.Montage))
    {
        Abort();
        return;
    }
    const float Alpha =
        FMath::Clamp(float(GetWorld()->GetTimeSeconds() - ChargeStarted) / FrozenConfig->ChargeDuration, 0.f, 1.f);
    FVector Next = ChargeStart + FRotator(0, Yaw, 0).Vector() * Snapshot.Length * Alpha;
    FVector Floor;
    const float Half = Golem->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!StoneGolemGeometry::Project(GetWorld(), Golem.Get(), Next - FVector(0, 0, Half), Snapshot.HeightTolerance,
                                     Floor))
    {
        Abort();
        return;
    }
    Next.Z = Floor.Z + Half + 2;
    if (const auto* D = ADungeonRunDirector::Find(GetWorld()); D && D->GetRoomIndex(Next) != ChargeRoom)
    {
        Abort();
        return;
    }
    // 位移可能同步触发取消或回收；移动后先重验租约，再收集命中。
    FHitResult Hit;
    Golem->SetActorLocation(Next, true, &Hit);
    // Swept movement can synchronously trigger damage, cancellation or pool retirement.
    if (!IsActive() || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
        return;
    const FVector Actual = Golem->GetActorLocation();
    TArray<AActor*> Candidates;
    GatherTargets(Candidates);
    TArray<AActor*> Hits;
    // 用上一实际位置到当前位置的线段扫掠判定，整个冲锋对同一目标仅伤害一次。
    for (auto* Target : Candidates)
    {
        FVector Point = Target->GetActorLocation();
        Point.Z = 0;
        FVector A = PreviousChargePosition, B = Actual;
        A.Z = B.Z = 0;
        if (!ChargedTargets.Contains(Target) && FMath::PointDistToSegment(Point, A, B) <= Snapshot.Width * .5f)
        {
            ChargedTargets.Add(Target);
            Hits.Add(Target);
        }
    }
    ApplyHitTargets(Hits);
    if (!IsActive() || !Golem.IsValid())
        return;
    PreviousChargePosition = Actual;
    if (Hit.bBlockingHit || Alpha >= 1.f)
        bCharging = false;
}
// 解除蒙太奇委托，取消时撤回本次护甲，并清空冲锋状态后走公共收尾。
void UGruxEnemyAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                   FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    ClearMontageDelegates();
    if (Cancelled)
        if (auto* Actor = Cast<AGruxEnemyCharacter>(Golem.Get()))
            Actor->RemoveRoarArmor(AppliedArmor);
    bCharging = false;
    ChargedTargets.Reset();
    FrozenConfig = nullptr;
    AppliedArmor.Invalidate();
    Super::EndAbility(Handle, Info, Activation, Replicate, Cancelled);
}
