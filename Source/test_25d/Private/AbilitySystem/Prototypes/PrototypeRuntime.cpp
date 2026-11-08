/**
 * 框架：一个 Actor 执行一个行为节点；组合靠 Emit 生成更多 Runtime，不是递归激活多个 GA。
 * 调用链：GA Execute -> Spawn -> FinishSpawning -> BeginPlay；随后引擎 Tick -> 行为函数 -> Hit/Emit -> 子 Spawn。
 * 网络：服务端结算伤害；复制字段与 Niagara 呈现客户端状态。OnRep 与后续 Tick 是异步入口，非连续同步调用栈。
 * 生命周期：Context 由 Runtime 的 UPROPERTY 持有；Finish 清理附属对象，短延迟销毁；EndPlay 再次兜底。
 */
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interface/CombatInterface.h"
#include "Terrain/TerrainSurface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Character/BaseCharacter.h"

/** 构造复制 Actor 与两个非自动激活 Niagara 组件；启用移动复制和 Tick，组件随 Actor 生命周期管理。 */
APrototypeRuntime::APrototypeRuntime()
{
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Visual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Visual"));
    Visual->SetupAttachment(RootComponent);
    Visual->SetAutoActivate(false);
    InnerVisual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("InnerVisual"));
    InnerVisual->SetupAttachment(RootComponent);
    InnerVisual->SetAutoActivate(false);
}

/** GA Execute 或 Emit 子节点入口：仅服务器校验配置与预算，延迟生成并在 FinishSpawning 前写入上下文、方向和目标。 */
APrototypeRuntime* APrototypeRuntime::Spawn(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                            const FVector& Position, const FVector& InDirection, AActor* Target,
                                            int32 InDepth, bool bSingle)
{
    AActor* Caster = InContext ? InContext->Caster.Get() : nullptr;
    if (!IsValid(Caster) || !Caster->HasAuthority() || !Config || !Config->IsValidConfiguration() ||
        Position.ContainsNaN() || InDirection.ContainsNaN())
        return nullptr;
    FVector SpawnPosition = Position;
    if (Config->Kind == EAbilityPrototypeKind::Surface && !ProjectSurfaceToGround(Caster, Position, SpawnPosition))
        return nullptr;
    if (!InContext->ReserveSpawn(InDepth))
        return nullptr;
    const FVector SpawnDirection =
        (Config->Kind == EAbilityPrototypeKind::Cone || Config->Kind == EAbilityPrototypeKind::Line)
            ? InDirection.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector)
            : InDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    FTransform Transform(SpawnDirection.Rotation(), SpawnPosition);
    auto* Actor = Caster->GetWorld()->SpawnActorDeferred<APrototypeRuntime>(
        StaticClass(), Transform, Caster, Cast<APawn>(Caster), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Actor)
        return nullptr;
    Actor->Context = InContext;
    Actor->Prototype = Config;
    Actor->Direction = SpawnDirection;
    Actor->AttackTarget = Target;
    Actor->ChainOrigin = Position;
    Actor->BeamEnd = IsValid(Target) ? Target->GetActorLocation() : Position + Actor->Direction * Config->Range;
    Actor->Depth = InDepth;
    Actor->bSingleProjectile = bSingle;
    Actor->FinishSpawning(Transform);
    return Actor;
}

/** 根施法及子节点/表现共用地面投影；忽略角色，以可见性射线寻找地面，失败不返回伪造落点。 */
bool APrototypeRuntime::ProjectSurfaceToGround(AActor* Caster, const FVector& Position, FVector& OutPosition)
{
    if (!IsValid(Caster) || !Caster->GetWorld() || Position.ContainsNaN())
        return false;
    // Event positions are usually capsule centres; use the same resolver before root cost and child spawn.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeGround), false, Caster);
    for (TActorIterator<ABaseCharacter> It(Caster->GetWorld()); It; ++It)
        Query.AddIgnoredActor(*It);
    FHitResult Floor;
    if (!Caster->GetWorld()->LineTraceSingleByChannel(Floor, Position + FVector(0, 0, 100),
                                                      Position - FVector(0, 0, 500), ECC_Visibility, Query))
        return false;
    OutPosition = Floor.ImpactPoint + FVector(0, 0, 2);
    return true;
}

/** 目标过滤公共入口：检查有效性、存活、阵营、战斗接口与 ASC；此执行器针对 Actor。 */
bool APrototypeRuntime::IsEnemy(AActor* Caster, AActor* Target)
{
    return IsValid(Caster) && IsValid(Target) && Caster != Target && Target->Implements<UCombatInterface>() &&
           !ICombatInterface::Execute_IsDead(Target) && UMyAbilitySystemLibrary::IsNotFriend(Caster, Target) &&
           UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
}

/** 根费用及持续费用入口：服务器校验余额后应用瞬时扣蓝 GE，返回应用成功状态。 */
bool APrototypeRuntime::SpendMana(AActor* Caster, float Amount)
{
    auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Caster);
    if (!Caster || !Caster->HasAuthority() || !ASC || !FMath::IsFinite(Amount) || Amount < 0.f ||
        ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Amount)
        return false;
    if (Amount == 0.f)
        return true;
    auto* Cost = NewObject<UGameplayEffect>();
    Cost->DurationPolicy = EGameplayEffectDurationType::Instant;
    auto& Modifier = Cost->Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetManaAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-Amount);
    return ASC->ApplyGameplayEffectToSelf(Cost, 1.f, ASC->MakeEffectContext()).WasSuccessfullyApplied();
}

/** FinishSpawning/复制生成后的引擎入口：先建立表现；服务器按 Kind 执行首发、创建地表或绑定 Orbit 攻击监听。 */
void APrototypeRuntime::BeginPlay()
{
    Super::BeginPlay();
    OnRep_Prototype();
    if (!HasAuthority() || !Prototype || !Context)
        return;
    using K = EAbilityPrototypeKind;
    switch (Prototype->Kind)
    {
    case K::Multishot:
        if (!bSingleProjectile)
        {
            for (int32 Index = 0; Index < Prototype->Count; ++Index)
            {
                const float Angle =
                    Prototype->Count == 1
                        ? 0.f
                        : -Prototype->SpreadDegrees * .5f + Prototype->SpreadDegrees * Index / (Prototype->Count - 1);
                Spawn(Context, Prototype, GetActorLocation(), Direction.RotateAngleAxis(Angle, FVector::UpVector),
                      nullptr, Depth, true);
            }
            bFinished = true;
            SetLifeSpan(.15f);
            Visual->DeactivateImmediate();
        }
        break;
    case K::Burst:
        Pulse();
        break;
    case K::Cone:
        ConePulse();
        break;
    case K::Ring:
    case K::Line:
        ShapePulse();
        break;
    case K::Surface:
    {
        FTransform Transform(FRotator::ZeroRotator, GetActorLocation());
        Surface = GetWorld()->SpawnActorDeferred<ATerrainSurface>(Prototype->SurfaceClass, Transform, GetOwner(),
                                                                  Cast<APawn>(GetOwner()),
                                                                  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Surface)
        {
            Surface->bOnlyAffectCasterEnemies = true;
            Surface->SurfaceSize = FVector2D(Prototype->Radius * 2.f);
            Surface->FinishSpawning(Transform);
            Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        else
            Finish(true);
        break;
    }
    case K::Orbit:
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(Context->DamageParams.SourceAbilitySystemComponent))
            AttackDelegate = ASC->OffensiveAttackTargeted.AddUObject(this, &ThisClass::OffensiveAttack);
        break;
    default:
        break;
    }
}

/** 配置复制通知，同时由 BeginPlay 主动调用；负责本地 Niagara 初始化，不执行权威伤害。 */
void APrototypeRuntime::OnRep_Prototype()
{
    if (!Prototype || GetNetMode() == NM_DedicatedServer || bPresentationReady)
        return;
    bPresentationReady = true;
    Visual->SetRelativeScale3D(Prototype->LoopVFXScale);
    if (Prototype->bGroundPresentation)
    {
        FVector Ground;
        if (ProjectSurfaceToGround(GetOwner(), GetActorLocation(), Ground))
        {
            Visual->SetWorldLocation(Ground);
            InnerVisual->SetWorldLocation(Ground);
        }
        Visual->SetWorldRotation(FRotator::ZeroRotator);
        InnerVisual->SetWorldRotation(FRotator::ZeroRotator);
    }
    if (!bFinished && Prototype->LoopVFX && (Prototype->Kind != EAbilityPrototypeKind::Multishot || bSingleProjectile))
    {
        Visual->SetAsset(Prototype->LoopVFX);
        Visual->Activate(true);
    }
    if (Prototype->Kind == EAbilityPrototypeKind::Burst && Prototype->ImpactVFX)
        MulticastImpact_Implementation(GetActorLocation());
    if (Prototype->Kind == EAbilityPrototypeKind::Ring && Prototype->LoopVFX)
    {
        Visual->SetRelativeScale3D(FVector(Prototype->Radius / 100.f, Prototype->Radius / 100.f, .25f));
        if (!bFinished && Prototype->InnerRadius > 0.f)
        {
            InnerVisual->SetAsset(Prototype->LoopVFX);
            InnerVisual->SetRelativeScale3D(
                FVector(Prototype->InnerRadius / 100.f, Prototype->InnerRadius / 100.f, .25f));
            InnerVisual->Activate(true);
        }
    }
    if (Prototype->Kind == EAbilityPrototypeKind::Line && Prototype->ImpactVFX)
        for (int32 Index = 1; Index <= 5; ++Index)
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Prototype->ImpactVFX,
                                                           GetActorLocation() +
                                                               GetActorForwardVector() * Prototype->Range * Index / 5.f,
                                                           GetActorRotation(), Prototype->ImpactVFXScale * .4f);
    if (Prototype->Kind == EAbilityPrototypeKind::Cone && Prototype->ImpactVFX)
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const FVector Ray = GetActorForwardVector().RotateAngleAxis((Index - 1) * Prototype->SpreadDegrees * .35f,
                                                                        FVector::UpVector);
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Prototype->ImpactVFX,
                                                           GetActorLocation() + Ray * Prototype->Radius * .55f,
                                                           Ray.Rotation(), FVector(.45f));
        }
    if (bFinished)
        OnRep_Finished();
}

/** 引擎逐帧入口：本地更新表现；服务器处理取消、费用、寿命，再按 Kind 分派行为更新。后续 Tick 不在 ActivateAbility 的同步栈中。 */
void APrototypeRuntime::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Prototype)
        return;
    using K = EAbilityPrototypeKind;
    if (Prototype->Kind == K::Wave && GetNetMode() != NM_DedicatedServer)
        Visual->SetRelativeScale3D(
            FVector(FMath::Max(.01f, WaveRadius / 100.f), FMath::Max(.01f, WaveRadius / 100.f), .25f));
    if ((Prototype->Kind == K::Beam || Prototype->Kind == K::Chain) && GetNetMode() != NM_DedicatedServer)
    {
        Visual->SetVariableVec3(TEXT("Beam Start"), Prototype->Kind == K::Beam && IsValid(GetOwner())
                                                        ? GetOwner()->GetActorLocation() + FVector(0, 0, 30)
                                                        : GetActorLocation());
        Visual->SetVariableVec3(TEXT("Beam End"), BeamEnd);
    }
    if (!HasAuthority() || bFinished || !Context)
        return;
    AActor* Caster = Context->Caster.Get();
    if (Context->bCancelled || !IsValid(Caster) ||
        (Caster->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Caster)))
    {
        Finish(true);
        return;
    }
    Age += Delta;
    if (Prototype->Kind == K::Delay)
    {
        if (Age >= Prototype->ArmDelay)
            Finish(false);
        return;
    }
    ManaClock += Delta;
    if (ManaClock >= 1.f && (Prototype->Kind == K::Beam || Prototype->Kind == K::Orbit))
    {
        const int32 Seconds = FMath::FloorToInt(ManaClock);
        ManaClock -= Seconds;
        if (!SpendMana(Caster, Prototype->ManaPerSecond * Seconds))
        {
            Finish(true);
            return;
        }
    }
    if (Prototype->Kind == K::Wave || Prototype->Kind == K::Returning || Prototype->Kind == K::Emitter)
    {
        // Consume the final partial frame before the hard lifetime expires.
        const float ActiveDelta = FMath::Min(Delta, FMath::Max(0.f, Prototype->Duration - (Age - Delta)));
        if (Prototype->Kind == K::Wave)
            TickWave(ActiveDelta);
        else if (Prototype->Kind == K::Returning)
            TickProjectile(ActiveDelta);
        else
            TickEmitter(ActiveDelta);
        if (Age >= Prototype->Duration)
            Finish(false);
        return;
    }
    // Field pulses are scheduled over (0, Duration], including the last boundary.
    // Consume only the active portion of a hitch before emitting the expiry child.
    if (Prototype->Kind == K::Field)
    {
        PeriodClock += FMath::Min(Delta, FMath::Max(0.f, Prototype->Duration - (Age - Delta)));
        while (PeriodClock + KINDA_SMALL_NUMBER >= Prototype->Period && !bFinished && !Context->bCancelled)
        {
            PeriodClock -= Prototype->Period;
            Pulse();
            Emit(TEXT("OnPeriod"), Prototype->OnPeriod, AttackTarget.Get(), GetActorLocation());
        }
        if (Age >= Prototype->Duration)
            Finish(false);
        return;
    }
    // Orbit is explicitly toggled; all other modes have a finite hard lifetime.
    if ((Prototype->Kind != K::Orbit || Depth > 0) && Age >= Prototype->Duration)
    {
        Finish(false);
        return;
    }
    if (Prototype->Kind == K::Chain)
    {
        TickChain(Delta);
        return;
    }
    if (Prototype->Kind == K::Projectile || Prototype->Kind == K::Multishot || Prototype->Kind == K::Homing ||
        Prototype->Kind == K::Piercing)
    {
        TickProjectile(Delta);
        return;
    }
    if (Prototype->Kind == K::Cone || Prototype->Kind == K::Ring || Prototype->Kind == K::Line)
        return;
    if (Prototype->Kind == K::Orbit)
    {
        TickOrbit(Delta);
        return;
    }
    if (Prototype->Kind == K::Beam)
    {
        AActor* Target = AttackTarget.Get();
        if (!IsEnemy(Caster, Target) ||
            FVector::DistSquared(Caster->GetActorLocation(), Target->GetActorLocation()) >
                FMath::Square(Prototype->Range) ||
            !HasLineOfSight(Target))
        {
            Finish(true);
            return;
        }
        SetActorLocation(Caster->GetActorLocation() + FVector(0, 0, 30));
        BeamEnd = Target->GetActorLocation();
    }
    if (Prototype->Kind == K::Trap)
    {
        if (Age < Prototype->ArmDelay)
            return;
        TArray<AActor*> Targets;
        GatherTargets(Targets);
        if (!Targets.IsEmpty())
        {
            Pulse();
            Finish(false);
        }
        return;
    }
    PeriodClock += Delta;
    if (PeriodClock >= Prototype->Period)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Prototype->Period);
        if (Prototype->Kind == K::Field)
            Pulse();
        if (Prototype->Kind == K::Beam)
            Hit(AttackTarget.Get());
        if (Prototype->Kind != K::Burst)
            Emit(TEXT("OnPeriod"), Prototype->OnPeriod, AttackTarget.Get(), GetActorLocation());
    }
}

/** Beam 目标可见性检查，供服务器 Tick 决定是否继续引导。 */
bool APrototypeRuntime::HasLineOfSight(AActor* Target) const
{
    if (!IsValid(Target) || !IsValid(GetOwner()))
        return false;
    FHitResult HitResult;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeLOS), false, GetOwner());
    Query.AddIgnoredActor(this);
    return !GetWorld()->LineTraceSingleByChannel(HitResult, GetOwner()->GetActorLocation() + FVector(0, 0, 30),
                                                 Target->GetActorLocation(), ECC_Visibility, Query) ||
           HitResult.GetActor() == Target;
}

/** 范围行为的候选 Actor 查询，供 Pulse/Trap 等筛选敌方目标。 */
void APrototypeRuntime::GatherTargets(TArray<AActor*>& Out) const
{
    Out.Reset();
    if (!Context || !Prototype || !Context->Caster.IsValid())
        return;
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Candidates, {Context->Caster.Get()}, Prototype->Radius,
                                                        GetActorLocation());
    for (auto* Target : Candidates)
        if (IsEnemy(Context->Caster.Get(), Target) &&
            FVector::DistSquared(Target->GetActorLocation(), GetActorLocation()) <= FMath::Square(Prototype->Radius))
            Out.AddUnique(Target);
}

/** 服务器行为命中入口：构造节点伤害参数并调用 ApplyDamageEffect，随后 Emit(OnHit) 执行组合子节点。 */
void APrototypeRuntime::Hit(AActor* Target, float Scale)
{
    if (!HasAuthority() || !Context || Context->bCancelled || !IsEnemy(Context->Caster.Get(), Target))
        return;
    FDamageEffectParams Params = Context->DamageParams;
    Params.WorldContextObject = this;
    Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
    Params.BaseDamage = Prototype->DamageAtLevel(Context->Level) * Scale;
    Params.DamageType = Prototype->DamageType;
    Params.bIsRadialDamage = false;
    UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    Emit(TEXT("OnHit"), Prototype->OnHit, Target, Target->GetActorLocation());
    if (ICombatInterface::Execute_IsDead(Target))
        BehaviorEvent.Broadcast(TEXT("OnKill"), Target, Target->GetActorLocation());
}

/** Burst/Field/Trap 的范围脉冲入口：查询候选并调用 Hit，驱动命中表现。 */
void APrototypeRuntime::Pulse()
{
    TArray<AActor*> Targets;
    GatherTargets(Targets);
    for (auto* Target : Targets)
        Hit(Target);
    if (Prototype->Kind != EAbilityPrototypeKind::Burst)
        MulticastImpact(GetActorLocation());
}

/** Tick 的弹体分支：推进飞行、检测碰撞和行为结束，处理追踪、穿透及返航等节点差异。 */
void APrototypeRuntime::TickProjectile(float Delta)
{
    const bool bReturnKind = Prototype->Kind == EAbilityPrototypeKind::Returning;
    float Remaining = FMath::Max(0.f, Prototype->Range - Distance);
    if (bReturnKind && bReturning)
    {
        const FVector ToCaster = GetOwner()->GetActorLocation() - GetActorLocation();
        Remaining = ToCaster.Size();
        if (Remaining <= Prototype->CollisionRadius)
        {
            Finish(false);
            return;
        }
        Direction = ToCaster.GetSafeNormal();
        SetActorRotation(Direction.Rotation());
    }
    if (Prototype->Kind == EAbilityPrototypeKind::Homing)
    {
        if (!IsEnemy(GetOwner(), AttackTarget.Get()))
        {
            Finish(false);
            return;
        }
        Direction = FMath::VInterpNormalRotationTo(
            Direction, (AttackTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal(), Delta,
            Prototype->HomingTurnRate);
        SetActorRotation(Direction.Rotation());
    }
    const float Step = FMath::Min(Prototype->Speed * Delta, Remaining);
    const FVector Start = GetActorLocation(), End = Start + Direction * Step;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeProjectile), false, GetOwner());
    Query.AddIgnoredActor(this);
    for (const auto& Previous : HitTargets)
        if (Previous.IsValid())
            Query.AddIgnoredActor(Previous.Get());
    // A multi-channel sweep stops at the first blocking hit. Ignore friends before
    // querying so their blocking capsules cannot hide enemies farther along the segment.
    for (TActorIterator<ABaseCharacter> It(GetWorld()); It; ++It)
        if (!IsEnemy(GetOwner(), *It))
            Query.AddIgnoredActor(*It);
    TArray<FHitResult> Hits;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    // Object queries include the character capsule even when its Visibility response
    // ignores traces (the project's small goblin meshes do not cover the capsule centre).
    GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
                                       FCollisionShape::MakeSphere(Prototype->CollisionRadius), Query);
    Hits.Sort(
        [](const FHitResult& A, const FHitResult& B)
        {
            return A.Time < B.Time;
        });
    for (const FHitResult& Result : Hits)
    {
        AActor* Target = Result.GetActor();
        if (Target == GetOwner() || Target == this || Cast<APrototypeRuntime>(Target))
            continue;
        if (IsValid(Target) && Target->Implements<UCombatInterface>() && !IsEnemy(GetOwner(), Target))
            continue;
        // Overlap-only trigger volumes (portals, pickups, encounter areas) are not walls.
        if (!IsEnemy(GetOwner(), Target) &&
            (!Result.GetComponent() ||
             Result.GetComponent()->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block))
            continue;
        SetActorLocation(Result.Location);
        if (IsEnemy(GetOwner(), Target))
        {
            if (HitTargets.Contains(Target))
                continue;
            if (bReturnKind && HitTargets.Num() >= Prototype->Count)
                continue;
            Hit(Target, bReturnKind ? (bReturning ? Prototype->DamageFalloff : 1.f)
                                    : FMath::Pow(Prototype->DamageFalloff, HitTargets.Num()));
            HitTargets.Add(Target);
            if (bReturnKind)
            {
                MulticastImpact(Result.Location);
                continue;
            }
            if (Prototype->Kind == EAbilityPrototypeKind::Piercing && HitTargets.Num() < Prototype->Count)
            {
                MulticastImpact(Result.Location);
                continue;
            }
        }
        bImpactOnFinish = true;
        Finish(false);
        return;
    }
    SetActorLocation(End);
    Distance += Step;
    if (bReturnKind)
    {
        if (bReturning && Step >= Remaining)
        {
            Finish(false);
            return;
        }
        if (!bReturning && Distance >= Prototype->Range)
        {
            bReturning = true;
            HitTargets.Reset();
            ForceNetUpdate();
            // The outbound endpoint can occur partway through this frame.
            const float ReturnDelta = FMath::Max(0.f, Delta - Step / Prototype->Speed);
            if (ReturnDelta > SMALL_NUMBER)
                TickProjectile(ReturnDelta);
        }
    }
    else if (Distance >= Prototype->Range)
        Finish(false);
}

/** BeginPlay 的扇形分支：筛选夹角内目标并执行节点命中。 */
void APrototypeRuntime::ConePulse()
{
    TArray<AActor*> Targets;
    GatherTargets(Targets);
    const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Prototype->SpreadDegrees * .5f));
    const FVector Facing = Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector);
    for (auto* Target : Targets)
    {
        const FVector Offset = Target->GetActorLocation() - GetActorLocation();
        if ((Offset.IsNearlyZero() ||
             FVector::DotProduct(Facing, Offset.GetSafeNormal2D()) + KINDA_SMALL_NUMBER >= CosHalf) &&
            VisibleFrom(GetActorLocation(), Target))
            Hit(Target);
    }
}

/** BeginPlay 的环形/直线分支：按配置形状与高度筛选目标后结算。 */
void APrototypeRuntime::ShapePulse()
{
    const bool bLine = Prototype->Kind == EAbilityPrototypeKind::Line;
    const float Bounds =
        FMath::Sqrt(FMath::Square(bLine ? Prototype->Range : Prototype->Radius) +
                    (bLine ? FMath::Square(Prototype->Width * .5f) : 0.f) + FMath::Square(Prototype->ShapeHalfHeight));
    TArray<AActor*> Targets;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Targets, {Context->Caster.Get()}, Bounds,
                                                        GetActorLocation());
    const FVector Forward = Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    for (auto* Target : Targets)
    {
        if (!IsEnemy(Context->Caster.Get(), Target))
            continue;
        const FVector Offset = Target->GetActorLocation() - GetActorLocation();
        if (FMath::Abs(Offset.Z) > Prototype->ShapeHalfHeight)
            continue;
        const float Along = FVector::DotProduct(Offset, Forward);
        const bool bInside = bLine ? Along >= 0.f && Along <= Prototype->Range &&
                                         FMath::Abs(FVector::DotProduct(Offset, Right)) <= Prototype->Width * .5f
                                   : Offset.SizeSquared2D() >= FMath::Square(Prototype->InnerRadius) &&
                                         Offset.SizeSquared2D() <= FMath::Square(Prototype->Radius);
        if (bInside && VisibleFrom(GetActorLocation(), Target))
            Hit(Target);
    }
}

/** Tick 的扩散波分支：推进半径并记录已命中目标，避免同一波反复命中。 */
void APrototypeRuntime::TickWave(float Delta)
{
    const float Previous = WaveRadius;
    WaveRadius = FMath::Min(Prototype->Radius, WaveRadius + Prototype->Speed * Delta);
    TArray<AActor*> Targets;
    GatherTargets(Targets);
    for (auto* Target : Targets)
    {
        const float DistanceToTarget = FVector::Distance(GetActorLocation(), Target->GetActorLocation());
        if (!HitTargets.Contains(Target) && DistanceToTarget <= WaveRadius &&
            (Previous == 0.f || DistanceToTarget > Previous) && VisibleFrom(GetActorLocation(), Target))
        {
            HitTargets.Add(Target);
            Hit(Target);
            MulticastImpact(Target->GetActorLocation());
        }
    }
    if (WaveRadius >= Prototype->Radius)
        Finish(false);
}

/** Tick 的发射器分支：按周期选择目标并通过 OnPeriod 生成子节点。 */
void APrototypeRuntime::TickEmitter(float Delta)
{
    PeriodClock += Delta;
    while (!bFinished && PeriodClock + SMALL_NUMBER >= Prototype->Period)
    {
        PeriodClock = FMath::Max(0.f, PeriodClock - Prototype->Period);
        AActor* Best = nullptr;
        float BestDistance = FMath::Square(Prototype->Radius);
        TArray<AActor*> Targets;
        GatherTargets(Targets);
        for (auto* Target : Targets)
        {
            const float CandidateDistance = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
            if (CandidateDistance <= BestDistance && VisibleFrom(GetActorLocation(), Target))
            {
                Best = Target;
                BestDistance = CandidateDistance;
            }
        }
        if (Best)
        {
            AttackTarget = Best;
            Direction =
                (Best->GetActorLocation() - GetActorLocation()).GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
            BeamEnd = Best->GetActorLocation();
            if (Emit(TEXT("OnPeriod"), Prototype->OnPeriod, Best, GetActorLocation()))
            {
                if (++Emissions >= Prototype->Count)
                    Finish(false);
                continue;
            }
        }
        // Idle or rejected shots consume time, not the successful-emission budget.
        // Drop missed idle periods so acquiring a target does not release a backlog.
        PeriodClock = FMath::Fmod(PeriodClock, Prototype->Period);
        return;
    }
}

/** 从指定起点检查目标可见性，供需要独立发射/链起点的行为调用。 */
bool APrototypeRuntime::VisibleFrom(const FVector& Origin, AActor* Target) const
{
    if (!IsValid(Target))
        return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PrototypeChainLOS), false, GetOwner());
    Query.AddIgnoredActor(this);
    for (TActorIterator<ABaseCharacter> It(GetWorld()); It; ++It)
        if (*It != Target)
            Query.AddIgnoredActor(*It);
    FHitResult Obstruction;
    return !GetWorld()->LineTraceSingleByChannel(Obstruction, Origin, Target->GetActorLocation(), ECC_Visibility,
                                                 Query) ||
           Obstruction.GetActor() == Target;
}

/** Tick 的连锁分支：按当前链状态选择后续目标，更新连线端点与命中记录。 */
void APrototypeRuntime::TickChain(float Delta)
{
    PeriodClock += Delta;
    if (!HitTargets.IsEmpty() && PeriodClock < Prototype->Period)
        return;
    PeriodClock = 0.f;
    AActor* Target = AttackTarget.Get();
    const float Reach = HitTargets.IsEmpty() ? Prototype->Range : Prototype->Radius;
    if (HitTargets.Num() >= Prototype->Count || !IsEnemy(GetOwner(), Target) || HitTargets.Contains(Target) ||
        FVector::DistSquared(ChainOrigin, Target->GetActorLocation()) > FMath::Square(Reach) ||
        !VisibleFrom(ChainOrigin, Target))
    {
        Finish(false);
        return;
    }
    SetActorLocation(ChainOrigin);
    BeamEnd = Target->GetActorLocation();
    Hit(Target, FMath::Pow(Prototype->DamageFalloff, HitTargets.Num()));
    HitTargets.Add(Target);
    LastChainTarget = Target;
    ChainOrigin = BeamEnd;
    AttackTarget = nullptr;
    float BestDistance = FMath::Square(Prototype->Radius);
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Candidates, {GetOwner()}, Prototype->Radius, ChainOrigin);
    for (auto* Candidate : Candidates)
    {
        const float DistanceSquared = FVector::DistSquared(ChainOrigin, Candidate->GetActorLocation());
        if (IsEnemy(GetOwner(), Candidate) && !HitTargets.Contains(Candidate) && DistanceSquared <= BestDistance &&
            VisibleFrom(ChainOrigin, Candidate))
        {
            BestDistance = DistanceSquared;
            AttackTarget = Candidate;
        }
    }
    ForceNetUpdate();
}

/** 行为事件连接器：服务器先广播本地 BehaviorEvent，再用同一 Context 和 Depth+1 生成 Child；该委托本身不是网络广播。 */
bool APrototypeRuntime::Emit(FName Event, UAbilityPrototype* Child, AActor* Target, FVector Position)
{
    if (!HasAuthority() || !Context || Context->bCancelled)
        return false;
    BehaviorEvent.Broadcast(Event, Target, Position);
    return Child && Spawn(Context, Child, Position, Direction, Target, Depth + 1) != nullptr;
}

/** 正常到期/取消汇合点：幂等设置结束标志；正常路径触发 OnExpire，取消路径标记共享 Context；清理附属对象并保留短暂复制寿命。 */
void APrototypeRuntime::Finish(bool bCancelled)
{
    if (bFinished)
        return;
    bFinished = true;
    if (bCancelled)
    {
        if (Context)
            Context->bCancelled = true;
        BehaviorEvent.Broadcast(TEXT("OnCancel"), nullptr, GetActorLocation());
    }
    else if (Prototype->Kind == EAbilityPrototypeKind::Chain && !HitTargets.IsEmpty())
        Emit(TEXT("OnExpire"), Prototype->OnExpire, LastChainTarget.Get(), ChainOrigin);
    else
        Emit(TEXT("OnExpire"), Prototype->OnExpire, AttackTarget.Get(), GetActorLocation());
    CleanupOwnedActors();
    OnRep_Finished();
    ForceNetUpdate();
    // Keep the replicated impact state alive long enough to open an actor channel,
    // including projectiles that hit during their very first server tick.
    SetLifeSpan(.35f);
}

/** 结束状态复制通知及服务器本地主动调用：关闭循环特效，并通过 bFinishPresented 防止重复结束表现。 */
void APrototypeRuntime::OnRep_Finished()
{
    if (!bFinished)
        return;
    Visual->DeactivateImmediate();
    InnerVisual->DeactivateImmediate();
    if (!Prototype || bFinishPresented)
        return;
    bFinishPresented = true;
    if (bImpactOnFinish && Prototype->ImpactVFX && GetNetMode() != NM_DedicatedServer)
        MulticastImpact_Implementation(GetActorLocation());
}

/** GA 或外部的服务器取消入口：标记共享 Context，使同次施法其他 Runtime 在后续更新中停止。 */
void APrototypeRuntime::CancelCast()
{
    if (!HasAuthority())
        return;
    if (Context)
        Context->bCancelled = true;
    Finish(true);
}

/** 多播接收端/本地表现入口：非专用服务器生成 Niagara；需要时投影地面。 */
void APrototypeRuntime::MulticastImpact_Implementation(FVector Position)
{
    if (Prototype && Prototype->ImpactVFX && GetNetMode() != NM_DedicatedServer)
    {
        FVector Location = Position;
        if (Prototype->bGroundPresentation)
            ProjectSurfaceToGround(GetOwner(), Position, Location);
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Prototype->ImpactVFX, Location, FRotator::ZeroRotator,
                                                       Prototype->ImpactVFXScale);
    }
}

/** 查询仍有效的环绕弹体数量；只读，不创建或回收对象。 */
int32 APrototypeRuntime::GetOrbitCount() const
{
    int32 Count = 0;
    for (AMyOrbitingFireBolt* Bolt : Bolts)
        if (IsValid(Bolt))
            ++Count;
    return Count;
}

/** ASC OffensiveAttackTargeted 委托入口：校验敌方目标与距离后记录 Orbit 的攻击目标。 */
void APrototypeRuntime::OffensiveAttack(UGameplayAbility* Ability, AActor* Target)
{
    if (!Ability || !IsEnemy(GetOwner(), Target))
        return;
    if (FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) >
        FMath::Square(Prototype->OrbitSettings->MaxTargetDistance))
        return;
    AttackTarget = Target;
}

/** Tick 的环绕分支：清理失效引用、定期补充弹体、发射/返航并分配环绕相位；弹体伤害完成后回调 OrbitHit。 */
void APrototypeRuntime::TickOrbit(float Delta)
{
    auto* Settings = Prototype->OrbitSettings.Get();
    SetActorLocation(GetOwner()->GetActorLocation());
    Bolts.RemoveAll(
        [](const auto& Bolt)
        {
            return !IsValid(Bolt);
        });
    PeriodClock += Delta;
    LaunchClock += Delta;
    if (PeriodClock >= Settings->SpawnInterval)
    {
        PeriodClock = FMath::Fmod(PeriodClock, Settings->SpawnInterval);
        if (Bolts.Num() < Settings->MaxFireBolts)
        {
            FTransform Transform(FRotator::ZeroRotator,
                                 GetActorLocation() + FVector(Settings->OrbitRadius, 0, Settings->OrbitHeight));
            auto* Bolt = GetWorld()->SpawnActorDeferred<AMyOrbitingFireBolt>(
                Settings->FireBoltClass, Transform, GetOwner(), Cast<APawn>(GetOwner()),
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (Bolt)
            {
                Bolt->DamageEffectParams = Context->DamageParams;
                Bolt->DamageEffectParams.BaseDamage = Prototype->DamageAtLevel(Context->Level);
                Bolt->DamageEffectParams.DamageType = Prototype->DamageType;
                Bolt->InitializeOrbit(GetOwner(), Settings, 0.f);
                Bolt->DamageApplied.AddUObject(this, &ThisClass::OrbitHit);
                Bolt->FinishSpawning(Transform);
                Bolts.Add(Bolt);
            }
        }
        Emit(TEXT("OnPeriod"), Prototype->OnPeriod, AttackTarget.Get(), GetActorLocation());
    }
    if (!IsEnemy(GetOwner(), AttackTarget.Get()) ||
        FVector::DistSquared(GetActorLocation(), AttackTarget->GetActorLocation()) >
            FMath::Square(Settings->MaxTargetDistance))
    {
        AttackTarget.Reset();
        for (AMyOrbitingFireBolt* Bolt : Bolts)
            if (Bolt->IsLaunched())
                Bolt->ReturnToOrbit();
    }
    else if (LaunchClock >= Settings->LaunchInterval)
    {
        LaunchClock = 0.f;
        for (AMyOrbitingFireBolt* Bolt : Bolts)
            if (!Bolt->IsLaunched())
            {
                Bolt->LaunchAt(AttackTarget.Get());
                break;
            }
    }
    int32 OrbitCount = 0, Index = 0;
    for (AMyOrbitingFireBolt* Bolt : Bolts)
        if (!Bolt->IsLaunched())
            ++OrbitCount;
    for (AMyOrbitingFireBolt* Bolt : Bolts)
        if (!Bolt->IsLaunched())
            Bolt->SetOrbitPhase(360.f * Index++ / FMath::Max(1, OrbitCount));
}

/** Finish 与 EndPlay 共用清理：服务器解绑 ASC 监听并销毁地表/环绕弹体，随后清空持有引用。 */
void APrototypeRuntime::CleanupOwnedActors()
{
    if (HasAuthority())
    {
        if (Context)
            if (auto* ASC = Cast<UMyAbilitySystemComponent>(Context->DamageParams.SourceAbilitySystemComponent))
                ASC->OffensiveAttackTargeted.Remove(AttackDelegate);
        if (IsValid(Surface))
            Surface->Destroy();
        for (AMyOrbitingFireBolt* Bolt : Bolts)
            if (IsValid(Bolt))
                Bolt->Destroy();
    }
    Bolts.Reset();
    Surface = nullptr;
}

/** 引擎销毁/世界退出兜底入口：清理所属对象与委托，再交父类释放组件。 */
void APrototypeRuntime::EndPlay(const EEndPlayReason::Type Reason)
{
    CleanupOwnedActors();
    Super::EndPlay(Reason);
}

/** 环绕弹体 DamageApplied 委托入口：转发 OnHit 子图，并在目标已死亡时广播本地 OnKill。 */
void APrototypeRuntime::OrbitHit(AActor* Target)
{
    if (!IsValid(Target) || !Context || Context->bCancelled)
        return;
    Emit(TEXT("OnHit"), Prototype->OnHit, Target, Target->GetActorLocation());
    if (Target->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Target))
        BehaviorEvent.Broadcast(TEXT("OnKill"), Target, Target->GetActorLocation());
}

/** 引擎复制注册入口：声明配置与表现状态字段；Context 和伤害执行保持服务器本地。 */
void APrototypeRuntime::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APrototypeRuntime, Prototype);
    DOREPLIFETIME(APrototypeRuntime, BeamEnd);
    DOREPLIFETIME(APrototypeRuntime, WaveRadius);
    DOREPLIFETIME(APrototypeRuntime, bReturning);
    DOREPLIFETIME(APrototypeRuntime, bSingleProjectile);
    DOREPLIFETIME(APrototypeRuntime, bImpactOnFinish);
    DOREPLIFETIME(APrototypeRuntime, bFinished);
}
