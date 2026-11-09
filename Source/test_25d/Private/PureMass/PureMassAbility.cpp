/**
 * 混合原型输入层：ASC -> ActivateAbility -> UPureMassTargetData.Ready -> TargetReady -> ValidateCombatTarget -> ExecuteMass/Commit -> APureMassRuntime::Spawn。客户端只提交目标；服务器重解 Actor/实体身份和可见性。有限 Runtime 脱离 GA 存活，Beam/开关技能监听 Runtime 销毁并随 EndAbility 清理。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
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

/** 返回稳定原生 SetByCaller 键；此 GE 内复用 Damage.Physics 只作扣蓝数值键，不表示物理伤害。 */
FGameplayTag UPureMassManaEffect::GetManaTag()
{
    // Reuse the existing native-registered Damage.Physics key; it is only a magnitude key in this effect.
    return UPureMassIncomingDamageEffect::GetDamageTag();
}

/** 构造瞬时 Mana 加法 GE，其数值来自 SetByCaller；每次施法创建独立 Spec。 */
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

/** 服务器检查施法者/ASC/余额及有限非负费用，使用原生 GE Spec 扣蓝；零费用直接成功。 */
bool UPureMassAbility::SpendMana(AActor* Caster, float Amount)
{
    if (!IsValid(Caster) || !Caster->HasAuthority() || !FMath::IsFinite(Amount) || Amount < 0.f)
        return false;
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Caster);
    if (!ASC || !ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()) ||
        ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Amount)
        return false;
    if (Amount == 0.f)
        return true;
    FGameplayEffectSpec Spec(GetDefault<UPureMassManaEffect>(), ASC->MakeEffectContext(), 1.f);
    Spec.SetSetByCallerMagnitude(UPureMassManaEffect::GetManaTag(), -Amount);
    return ASC->ApplyGameplayEffectSpecToSelf(Spec).WasSuccessfullyApplied();
}

/** CommitAbility 费用回调，只由服务器调用 SpendMana 扣根节点费用。 */
void UPureMassAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                 FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority() && Prototype)
        SpendMana(Info->AvatarActor.Get(), Prototype->ManaCost);
}

/** 清空本次目标状态，检查 Mass 子系统；Beam 注册松键回调，自身/开关技能直接执行，其余创建目标任务与超时。 */
void UPureMassAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                       FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    bTargetConsumed = false;
    if (!Info || !Prototype || !GetWorld()->GetSubsystem<UPureMassSubsystem>())
    {
        AbortMass();
        return;
    }
    if (Prototype->Kind == EAbilityPrototypeKind::Beam)
    {
        auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
        Release->OnRelease.AddDynamic(this, &ThisClass::Released);
        Release->ReadyForActivation();
        if (!IsActive())
            return;
    }
    if (Prototype->bTargetSelf || IsInputToggleAbility())
    {
        if (Info->IsNetAuthority())
            ExecuteMass(Info->AvatarActor->GetActorLocation(), INDEX_NONE);
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(MassTargetTimeout, this, &ThisClass::AbortMass, 3.f);
    auto* Task = UPureMassTargetData::Create(this);
    Task->Ready.AddDynamic(this, &ThisClass::TargetReady);
    Task->ReadyForActivation();
}

/** 兼容实体 ID 的入口，将 ID 包装为混合目标后委托 ValidateCombatTarget。 */
bool UPureMassAbility::ValidateTarget(AActor* Caster, const UAbilityPrototype* Config, int32 Id,
                                      const FVector& Requested, FVector& Resolved)
{
    return ValidateCombatTarget(Caster, Config, FPureMassCombatTarget(Id), Requested, Resolved);
}

/** 服务器拒绝非法/冲突身份，重解真实目标位置并限制请求误差；检查射程、必需目标与地面或目标遮挡。 */
bool UPureMassAbility::ValidateCombatTarget(AActor* Caster, const UAbilityPrototype* Config,
                                            const FPureMassCombatTarget& Target, const FVector& Requested,
                                            FVector& Resolved)
{
    if (!IsValid(Caster) || !Caster->HasAuthority() || !Config || Requested.ContainsNaN() ||
        Target.EntityId < INDEX_NONE || Target.EntityId == 0 || (Target.IsActor() && Target.EntityId != INDEX_NONE))
        return false;
    Resolved = Requested;
    if (Target.IsSet())
    {
        if (!PureMassCombat::Resolve(Caster->GetWorld(), Caster, Target, Resolved))
            return false;
        // Prevent a valid ID from smuggling an unrelated aim point; allow snapshot interpolation error.
        if (FVector::DistSquared(Requested, Resolved) > FMath::Square(250.f))
            return false;
    }
    if (FVector::DistSquared(Caster->GetActorLocation(), Resolved) > FMath::Square(Config->Range))
        return false;
    using K = EAbilityPrototypeKind;
    if ((Config->Kind == K::Beam || Config->Kind == K::Homing || Config->Kind == K::Chain) && !Target.IsSet())
        return false;
    if (Target.IsSet())
        return PureMassCombat::Visible(Caster->GetWorld(), Caster, Caster->GetActorLocation() + FVector(0, 0, 30),
                                       Target);
    return PureMassCombat::GroundVisible(Caster->GetWorld(), Caster, Caster->GetActorLocation() + FVector(0, 0, 30),
                                         Resolved);
}

/** 目标任务异步回调：客户端停止等待；服务器解析 Actor/Hit.Item 实体 ID，验证后调用 ExecuteMass。 */
void UPureMassAbility::TargetReady(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bTargetConsumed || !CurrentActorInfo)
        return;
    if (!CurrentActorInfo->IsNetAuthority())
    {
        bTargetConsumed = true;
        GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
        return;
    }
    const FHitResult* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    FVector Point;
    if (!Hit || !Hit->bBlockingHit || (Hit->GetActor() && Hit->Item != INDEX_NONE))
    {
        AbortMass();
        return;
    }
    const auto Target =
        Hit->GetActor() ? FPureMassCombatTarget::FromActor(Hit->GetActor()) : FPureMassCombatTarget(Hit->Item);
    if (!ValidateCombatTarget(GetAvatarActorFromActorInfo(), Prototype, Target, Hit->ImpactPoint, Point))
    {
        AbortMass();
        return;
    }
    ExecuteMass(Point, Target);
}

/** 防重复提交，必要时先投影地表；Commit 后建立 Context、计算各行为起点并生成 Runtime，成功后转向并通知环绕攻击。 */
void UPureMassAbility::ExecuteMass(const FVector& Point, FPureMassCombatTarget Target)
{
    if (bTargetConsumed || !IsActive() || !CurrentActorInfo->IsNetAuthority())
        return;
    bTargetConsumed = true;
    GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
    AActor* Avatar = GetAvatarActorFromActorInfo();
    FVector Origin = Point;
    using K = EAbilityPrototypeKind;
    if (Prototype->Kind == K::Surface && !APrototypeRuntime::ProjectSurfaceToGround(Avatar, Point, Origin))
    {
        AbortMass();
        return;
    }
    SetOffensiveAttackTarget(Target.Actor.Get());
    FString GraphError;
    UAbilityGraphDefinition* Graph = PrepareUnifiedGraph(TEXT("LegacyMixed.v1"), GraphError);
    if (!Graph)
    {
        UE_LOG(LogTemp, Error, TEXT("Unified GA %s: %s"), *GetName(), *GraphError);
        AbortMass();
        return;
    }
    if (!CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) ||
        !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    {
        AbortMass();
        return;
    }
    auto* Context = NewObject<UPrototypeCastContext>(Avatar);
    Context->Caster = Avatar;
    Context->CastId = FGuid::NewGuid();
    Context->Level = GetAbilityLevel();
    Context->DamageParams = MakeDamageEffectParamsFromClassDefaults();
    FVector Direction =
        (Point - Avatar->GetActorLocation()).GetSafeNormal(SMALL_NUMBER, Avatar->GetActorForwardVector());
    if (Prototype->Kind == K::Projectile || Prototype->Kind == K::Multishot || Prototype->Kind == K::Homing ||
        Prototype->Kind == K::Piercing || Prototype->Kind == K::Returning)
    {
        Origin = Avatar->GetActorLocation() + FVector(0, 0, 30);
        Direction = (Point + (!Target.IsSet() ? FVector(0, 0, 30) : FVector::ZeroVector) - Origin)
                        .GetSafeNormal(SMALL_NUMBER, Direction);
    }
    if (Prototype->Kind == K::Cone || Prototype->Kind == K::Line || Prototype->Kind == K::Chain)
        Origin = Avatar->GetActorLocation();
    const bool bHold = Prototype->Kind == K::Beam || IsInputToggleAbility();
    if (!StartUnifiedGraph(Graph, Context, Origin, Direction, Target, bHold, GraphError))
    {
        AbortMass();
        return;
    }
    PrototypeFacing::Apply(Avatar, Point, Prototype->bTargetSelf);
    if (Prototype->Kind != K::Orbit)
        APureMassRuntime::NotifyCommittedAttack(Avatar, this, Target);
    if (!bHold)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

/** Beam 松键任务回调，进入 AbortMass 取消持续技能。 */
void UPureMassAbility::Released(float)
{
    AbortMass();
}
/** 超时或失败统一入口，对仍激活技能调用取消式 EndAbility。 */
void UPureMassAbility::AbortMass()
{
    if (IsActive())
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
/** 清除目标超时，解绑 Runtime 销毁回调后取消仍持有节点，清引用并交父类清理。 */
void UPureMassAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                  FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(MassTargetTimeout);
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
