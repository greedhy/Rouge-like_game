/**
 * 框架：GAS 输入壳，不是组合图执行器。输入 -> ActivateAbility -> 目标任务回调 -> Execute/CommitAbility -> Runtime::Spawn。
 * 生命周期：有限 Runtime 可在 GA 结束后独立运行；持续 Runtime 随 EndAbility 取消。客户端目标数据只是请求，服务器校验并扣费。
 */
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/PrototypeFacing.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "TimerManager.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeInput.h"

/** 构造阶段设定每 Actor 实例化与本地预测策略；伤害附加参数默认关闭。 */
UPrototypeAbility::UPrototypeAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    DebuffChance = 0.f;
    KnockbackChance = 0.f;
    KnockbackForceMagnitude = 0.f;
    DeathImpulseMagnitude = 0.f;
    bIsRadialDamage = false;
}

/** 构造稳定的原生持续 GE 类型，供 ApplyCooldown 创建可复制的冷却 Spec。 */
UPrototypeCooldownEffect::UPrototypeCooldownEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1.f);
}

/** 输入层查询开关语义；仅根 Orbit 作为再次按键关闭的技能。 */
bool UPrototypeAbility::IsInputToggleAbility() const
{
    return Prototype && Prototype->Kind == EAbilityPrototypeKind::Orbit;
}

/** GAS 激活检查入口：校验配置、伤害 GE、Avatar 与存活状态，再执行父类限制。 */
bool UPrototypeAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           const FGameplayTagContainer* Source, const FGameplayTagContainer* Target,
                                           FGameplayTagContainer* Relevant) const
{
    if (!Prototype || !Prototype->IsValidConfiguration() || !DamageEffectClass || !Info || !Info->AvatarActor.IsValid())
        return false;
    AActor* Avatar = Info->AvatarActor.Get();
    return Avatar->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Avatar) &&
           Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}

/** GAS 费用检查回调：读取 ASC 当前 Mana，与根节点一次性费用比较，不扣费。 */
bool UPrototypeAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                  FGameplayTagContainer* Relevant) const
{
    auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
    return Prototype && ASC && ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()) &&
           ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) >= Prototype->ManaCost;
}

/** CommitAbility 的费用应用回调：仅权威端经 SpendMana 扣除根节点费用。 */
void UPrototypeAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                  FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority())
        APrototypeRuntime::SpendMana(Info->AvatarActor.Get(), Prototype->ManaCost);
}

/** GAS 冷却检查回调：依据根配置 CooldownTag 查询 ASC 当前冷却状态。 */
bool UPrototypeAbility::CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayTagContainer* Relevant) const
{
    return Prototype && Info && Info->AbilitySystemComponent.IsValid() &&
           (!Prototype->CooldownTag.IsValid() ||
            !Info->AbilitySystemComponent->HasMatchingGameplayTag(Prototype->CooldownTag));
}

/** CommitAbility 的冷却应用回调：服务器创建持续 GE Spec，写入持续时间与冷却 Tag。 */
void UPrototypeAbility::ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                      FGameplayAbilityActivationInfo Activation) const
{
    if (!Info || !Info->IsNetAuthority() || Prototype->Cooldown <= 0.f)
        return;
    auto* ASC = Info->AbilitySystemComponent.Get();
    FGameplayEffectSpecHandle Spec =
        ASC->MakeOutgoingSpec(UPrototypeCooldownEffect::StaticClass(), GetAbilityLevel(), ASC->MakeEffectContext());
    Spec.Data->SetDuration(Prototype->Cooldown, true);
    Spec.Data->DynamicGrantedTags.AddTag(Prototype->CooldownTag);
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

/** GAS 激活入口：重置上次状态；Beam 注册松键任务，自身技能直接执行，其余等待鼠标目标数据和超时。 */
void UPrototypeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    bReceivedTarget = false;
    UnifiedExecution = nullptr;
    if (!Info || !Prototype)
    {
        Abort();
        return;
    }
    if (Prototype->Kind == EAbilityPrototypeKind::Beam)
    {
        auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
        Release->OnRelease.AddDynamic(this, &ThisClass::OnReleased);
        Release->ReadyForActivation();
        if (!IsActive())
            return;
    }
    if (Prototype->bTargetSelf || IsInputToggleAbility())
    {
        if (Info->IsNetAuthority())
            Execute(Info->AvatarActor->GetActorLocation(), nullptr);
        return;
    }
    GetWorld()->GetTimerManager().SetTimer(TargetTimeout, this, &ThisClass::Abort, 3.f);
    auto* Task = UTargetDataUnderMouse::CreateTargetDataUnderMouse(this);
    Task->ValidData.AddDynamic(this, &ThisClass::OnTargetData);
    Task->ReadyForActivation();
}

/** TargetDataUnderMouse 的异步委托入口：客户端停止等待；服务器验证命中、距离、遮挡和目标类型后进入 Execute。 */
void UPrototypeAbility::OnTargetData(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bReceivedTarget)
        return;
    if (!CurrentActorInfo->IsNetAuthority())
    {
        bReceivedTarget = true;
        GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
        return;
    }
    const FHitResult* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    if (!Hit || !Hit->bBlockingHit || Hit->ImpactPoint.ContainsNaN())
    {
        Abort();
        return;
    }
    AActor* Avatar = GetAvatarActorFromActorInfo();
    AActor* Target = APrototypeRuntime::IsEnemy(Avatar, Hit->GetActor()) ? Hit->GetActor() : nullptr;
    const FVector Position = Target ? Target->GetActorLocation() : FVector(Hit->ImpactPoint);
    if (Prototype->Kind == EAbilityPrototypeKind::Line)
    {
        // A Line uses the finite cursor point only as a direction request. The server's
        // ShapePulse enforces length, width, height and visibility per affected enemy.
        Execute(Position, Target);
        return;
    }
    if (FVector::DistSquared(Avatar->GetActorLocation(), Position) > FMath::Square(Prototype->Range))
    {
        Abort();
        return;
    }
    FHitResult Obstruction;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeAim), false, Avatar);
    // Validate against server geometry; the cursor result is only an aiming request.
    if (GetWorld()->LineTraceSingleByChannel(Obstruction, Avatar->GetActorLocation() + FVector(0, 0, 30), Position,
                                             ECC_Visibility, Query) &&
        Obstruction.GetActor() != Target &&
        FVector::DistSquared(Obstruction.ImpactPoint, Position) > FMath::Square(50.f))
    {
        Abort();
        return;
    }
    if ((Prototype->Kind == EAbilityPrototypeKind::Beam || Prototype->Kind == EAbilityPrototypeKind::Homing ||
         Prototype->Kind == EAbilityPrototypeKind::Chain) &&
        !Target)
    {
        Abort();
        return;
    }
    Execute(Position, Target);
}

/** OnTargetData/自身施法入口：拒绝重复目标，提交费用与冷却，建立共享上下文并生成根 Runtime。有限节点脱离 GA 存活；Beam/Orbit 保留销毁监听。 */
void UPrototypeAbility::Execute(const FVector& Position, AActor* Target)
{
    if (bReceivedTarget || !IsActive())
        return;
    bReceivedTarget = true;
    GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
    AActor* Avatar = GetAvatarActorFromActorInfo();
    FVector Origin = Position;
    if (Prototype->Kind == EAbilityPrototypeKind::Surface &&
        !APrototypeRuntime::ProjectSurfaceToGround(Avatar, Position, Origin))
    {
        Abort();
        return;
    }
    SetOffensiveAttackTarget(Target);
    FString GraphError;
    UAbilityGraphDefinition* Graph = PrepareUnifiedGraph(TEXT("LegacyActor.v1"), GraphError);
    if (!Graph)
    {
        UE_LOG(LogTemp, Error, TEXT("Unified GA %s: %s"), *GetName(), *GraphError);
        Abort();
        return;
    }
    if (!CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) ||
        !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    {
        Abort();
        return;
    }
    auto* Context = NewObject<UPrototypeCastContext>(Avatar);
    Context->Caster = Avatar;
    Context->CastId = FGuid::NewGuid();
    Context->Level = GetAbilityLevel();
    Context->DamageParams = MakeDamageEffectParamsFromClassDefaults();
    FVector Direction = (Position - Avatar->GetActorLocation()).GetSafeNormal();
    if (Direction.IsNearlyZero())
        Direction = Avatar->GetActorForwardVector();
    using K = EAbilityPrototypeKind;
    if (Prototype->Kind == K::Projectile || Prototype->Kind == K::Multishot || Prototype->Kind == K::Homing ||
        Prototype->Kind == K::Piercing)
    {
        Origin = Avatar->GetActorLocation() + FVector(0, 0, 30) + Direction * 60.f;
        Direction = ((Target ? Position : Position + FVector(0, 0, 30)) - Origin).GetSafeNormal();
    }
    if (Prototype->Kind == K::Chain)
        Origin = Avatar->GetActorLocation() + FVector(0, 0, 30);
    if (Prototype->Kind == K::Cone || Prototype->Kind == K::Line)
        Origin = Avatar->GetActorLocation();
    if (Prototype->Kind == K::Returning)
    {
        Direction =
            (Position - Avatar->GetActorLocation()).GetSafeNormal2D(SMALL_NUMBER, Avatar->GetActorForwardVector());
        Origin = Avatar->GetActorLocation() + Direction * 60.f;
        if (Target)
            Origin.Z = Position.Z;
    }
    const bool bHold = Prototype->Kind == K::Beam || Prototype->Kind == K::Orbit;
    if (!StartUnifiedGraph(Graph, Context, Origin, Direction, FPureMassCombatTarget::FromActor(Target), bHold,
                           GraphError))
    {
        Abort();
        return;
    }
    PrototypeFacing::Apply(Avatar, Position, Prototype->bTargetSelf);
    if (!bHold)
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    }
}

/** WaitInputRelease 的异步松键回调，经 Abort 取消持续技能。 */
void UPrototypeAbility::OnReleased(float)
{
    Abort();
}
/** 超时、非法目标或松键共用的取消入口；仅在 GA 激活时调用 EndAbility。 */
void UPrototypeAbility::Abort()
{
    if (IsActive())
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
/** GAS 结束入口：清理目标超时与销毁监听，取消仍由 GA 持有的 Runtime，再交父类释放任务。 */
void UPrototypeAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                   FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
    if (UnifiedExecution)
    {
        UnifiedExecution->OnFinished.RemoveAll(this);
        if (bCancelled)
        {
            UnifiedExecution->Cancel();
        }
    }
    UnifiedExecution = nullptr;
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}

UAbilityGraphDefinition* UPrototypeAbility::PrepareUnifiedGraph(FName Profile, FString& Error)
{
    const auto Session = GetExecutionSession();
    if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority() || !Session || !Session->IsSourceCurrent())
    {
        Error = TEXT("The original activation source is no longer current.");
        return nullptr;
    }
    return UAbilityLegacyGraphCompiler::Compile(this, Prototype, Profile, Error);
}

bool UPrototypeAbility::StartUnifiedGraph(UAbilityGraphDefinition* Graph, UPrototypeCastContext* Context,
                                          FVector Origin, FVector Direction, const FPureMassCombatTarget& Target,
                                          bool bHoldAbility, FString& Error)
{
    if (!Context || !Context->CaptureSourceLife(Error))
    {
        return false;
    }
    FAbilityNodeInput Input;
    Input.SourceToken = Context->GetSourceLife();
    Input.Origin = Origin;
    Input.Direction = Direction;
    Input.EventCenter = Origin;
    Input.Context = Context;
    Input.Level = Context->Level;
    Input.DamageParams = Context->DamageParams;
    if (Target.IsSet())
    {
        FVector Location;
        if (!Context->ResolveTargetLife(Target, Location))
        {
            Error = TEXT("The selected target life is no longer valid.");
            return false;
        }
        Input.LiveTargetToken = *Context->FindTargetLife(Target);
    }
    UnifiedExecution = UAbilityLegacyGraphCompiler::Execute(Graph, Context, Input, Error);
    if (!UnifiedExecution)
    {
        return false;
    }
    if (bHoldAbility)
    {
        if (UnifiedExecution->IsFinished())
        {
            OnUnifiedFinished(UnifiedExecution->GetEndReason());
        }
        else
        {
            UnifiedExecution->OnFinished.AddUObject(this, &ThisClass::OnUnifiedFinished);
        }
    }
    return true;
}

void UPrototypeAbility::OnUnifiedFinished(EAbilityGraphEndReason Reason)
{
    UnifiedExecution = nullptr;
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true,
                   Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired);
    }
}

/** 技能 UI 查询入口：按当前节点 Kind 和等级参数生成说明；不执行技能或消耗资源。 */
FString UPrototypeAbility::GetDescription(int32 Level)
{
    if (!Prototype)
        return TEXT("<Default>Missing prototype configuration</>");
    const auto& P = *Prototype;
    const float HitDamage = P.DamageAtLevel(Level);
    FString Detail;
    using K = EAbilityPrototypeKind;
    switch (P.Kind)
    {
    case K::Projectile:
        Detail = FString::Printf(TEXT("One shot: %.0f damage. Range %.0f."), HitDamage, P.Range);
        break;
    case K::Multishot:
        Detail =
            FString::Printf(TEXT("%d shots, %.0f-degree fan. %.0f damage each."), P.Count, P.SpreadDegrees, HitDamage);
        break;
    case K::Burst:
        Detail = FString::Printf(TEXT("Burst: %.0f damage. Radius %.0f."), HitDamage, P.Radius);
        break;
    case K::Beam:
        Detail = FString::Printf(TEXT("Hold: %.0f damage/%.1fs, %.0f MP/s. Up to %.0fs."), HitDamage, P.Period,
                                 P.ManaPerSecond, P.Duration);
        break;
    case K::Surface:
        Detail =
            FString::Printf(TEXT("Enemy-only ground: %.0fx%.0f, lasts %.0fs."), P.Radius * 2, P.Radius * 2, P.Duration);
        break;
    case K::Orbit:
        Detail = P.OrbitSettings ? FString::Printf(TEXT("Toggle: %.0f MP/s. One bolt/%.1fs; cap %d."), P.ManaPerSecond,
                                                   P.OrbitSettings->SpawnInterval, P.OrbitSettings->MaxFireBolts)
                                 : TEXT("Missing orbit settings.");
        break;
    case K::Field:
        Detail = FString::Printf(TEXT("%.0f damage/%.1fs, radius %.0f. Lasts %.0fs."), HitDamage, P.Period, P.Radius,
                                 P.Duration);
        break;
    case K::Trap:
        Detail = FString::Printf(TEXT("Arms in %.1fs: %.0f damage. Lasts %.0fs."), P.ArmDelay, HitDamage, P.Duration);
        break;
    case K::Homing:
        Detail = FString::Printf(TEXT("Tracks one enemy: %.0f damage. Range %.0f."), HitDamage, P.Range);
        break;
    case K::Piercing:
        Detail = FString::Printf(TEXT("Pierces %d foes: %.0f damage, %.0f%% per hit."), P.Count, HitDamage,
                                 P.DamageFalloff * 100);
        break;
    case K::Chain:
        Detail =
            FString::Printf(TEXT("%d foes: %.0f damage, %.0f%% per hop."), P.Count, HitDamage, P.DamageFalloff * 100);
        break;
    case K::Cone:
        Detail =
            FString::Printf(TEXT("%.0f-degree arc, radius %.0f. %.0f damage."), P.SpreadDegrees, P.Radius, HitDamage);
        break;
    case K::Wave:
        Detail = FString::Printf(TEXT("Expands to %.0f: %.0f damage, once per foe."), P.Radius, HitDamage);
        break;
    case K::Returning:
        Detail = FString::Printf(TEXT("Out: %.0f damage. Return: %.0f%%. %d foes/pass."), HitDamage,
                                 P.DamageFalloff * 100, P.Count);
        break;
    case K::Emitter:
        Detail = FString::Printf(TEXT("Auto-fire every %.1fs. Up to %d shots."), P.Period, P.Count);
        break;
    case K::Delay:
        Detail = FString::Printf(TEXT("After %.1fs: %s"), P.ArmDelay,
                                 P.OnExpire ? *P.OnExpire->DisplayName.ToString().Left(24) : TEXT("Missing child"));
        break;
    case K::Ring:
        Detail = FString::Printf(TEXT("Ring %.0f-%.0f: %.0f damage."), P.InnerRadius, P.Radius, HitDamage);
        break;
    case K::Line:
        Detail = FString::Printf(TEXT("Line %.0fx%.0f: %.0f damage."), P.Range, P.Width, HitDamage);
        break;
    }
    if ((P.OnHit || P.OnExpire || P.OnPeriod) && P.Kind != K::Emitter && P.Kind != K::Delay)
    {
        const UAbilityPrototype* Child = P.OnHit ? P.OnHit.Get() : P.OnExpire ? P.OnExpire.Get() : P.OnPeriod.Get();
        const TCHAR* Trigger = P.OnHit ? TEXT("Hit") : P.OnExpire ? TEXT("End") : TEXT("Tick");
        Detail += FString::Printf(TEXT(" %s: %s"), Trigger, *Child->DisplayName.ToString().Left(24));
    }
    return FString::Printf(TEXT("<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>%s</>"),
                           *P.DisplayName.ToString(), Level, P.ManaCost, P.Cooldown, *Detail);
}
/** 技能 UI 下一等级说明入口，复用描述生成逻辑。 */
FString UPrototypeAbility::GetNextLevelDescription(int32 Level)
{
    return GetDescription(Level);
}
