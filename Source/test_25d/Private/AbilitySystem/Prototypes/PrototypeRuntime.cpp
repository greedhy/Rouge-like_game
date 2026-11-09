/** GA / graph strategy -> Spawn or SpawnDriven -> driver Start; compatibility Tick or graph Advance -> driver behavior and child admission. This Actor owns only replication, presentation resources and compatibility delegates. */
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Unified/LegacyActorAbilityDriver.h"
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
    Driver = MakeShared<FActorAbilityDriver>(this);
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
    return SpawnInternal(InContext, Config, Position, InDirection, Target, InDepth, bSingle, {}, false, nullptr);
}

APrototypeRuntime* APrototypeRuntime::SpawnDriven(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                                  const FVector& Position, const FVector& InDirection, AActor* Target,
                                                  int32 InDepth, bool bSingle, FLegacyAbilityChildAdmission Admission,
                                                  UAbilityPrototype* PresentationConfig)
{
    return SpawnInternal(InContext, Config, Position, InDirection, Target, InDepth, bSingle, MoveTemp(Admission), true,
                         PresentationConfig);
}

APrototypeRuntime* APrototypeRuntime::SpawnInternal(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                                    const FVector& Position, const FVector& InDirection, AActor* Target,
                                                    int32 InDepth, bool bSingle, FLegacyAbilityChildAdmission Admission,
                                                    bool bDriven, UAbilityPrototype* PresentationConfig)
{
    AActor* Caster = InContext ? InContext->Caster.Get() : nullptr;
    if (!IsValid(Caster) || !Caster->HasAuthority() || !Config || !Config->IsValidConfiguration() ||
        Position.ContainsNaN() || InDirection.ContainsNaN())
        return nullptr;
    FVector SpawnPosition = Position;
    if (Config->Kind == EAbilityPrototypeKind::Surface && !ProjectSurfaceToGround(Caster, Position, SpawnPosition))
        return nullptr;
    FString SourceError;
    if (!InContext->CaptureSourceLife(SourceError))
    {
        return nullptr;
    }
    FVector CapturedTargetPosition;
    InContext->ResolveTargetLife(FPureMassCombatTarget::FromActor(Target), CapturedTargetPosition);
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
    Actor->ExecutionPrototype = Config;
    Actor->Prototype = PresentationConfig ? PresentationConfig : Config;
    Actor->Driver->Direction = SpawnDirection;
    Actor->Driver->AttackTarget = Target;
    Actor->Driver->ChainOrigin = Position;
    Actor->BeamEnd = IsValid(Target) ? Target->GetActorLocation() : Position + Actor->Driver->Direction * Config->Range;
    Actor->Driver->Depth = InDepth;
    Actor->bSingleProjectile = bSingle;
    Actor->bExternallyDriven = bDriven;
    Actor->Driver->ChildAdmission = MoveTemp(Admission);
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
    Driver->Start();
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
    if (!bExternallyDriven)
    {
        Driver->Advance(Delta);
    }
}

/** Beam 目标可见性检查，供服务器 Tick 决定是否继续引导。 */
bool APrototypeRuntime::HasLineOfSight(AActor* Target) const
{
    return Driver->HasLineOfSight(Target);
}

/** 范围行为的候选 Actor 查询，供 Pulse/Trap 等筛选敌方目标。 */
void APrototypeRuntime::GatherTargets(TArray<AActor*>& Out) const
{
    Driver->GatherTargets(Out);
}

/** 服务器行为命中入口：构造节点伤害参数并调用 ApplyDamageEffect，随后 Emit(OnHit) 执行组合子节点。 */
void APrototypeRuntime::Hit(AActor* Target, float Scale)
{
    Driver->Hit(Target, Scale);
}

/** Burst/Field/Trap 的范围脉冲入口：查询候选并调用 Hit，驱动命中表现。 */
void APrototypeRuntime::Pulse()
{
    Driver->Pulse();
}

/** Tick 的弹体分支：推进飞行、检测碰撞和行为结束，处理追踪、穿透及返航等节点差异。 */
void APrototypeRuntime::TickProjectile(float Delta)
{
    Driver->TickProjectile(Delta);
}

/** BeginPlay 的扇形分支：筛选夹角内目标并执行节点命中。 */
void APrototypeRuntime::ConePulse()
{
    Driver->ConePulse();
}

/** BeginPlay 的环形/直线分支：按配置形状与高度筛选目标后结算。 */
void APrototypeRuntime::ShapePulse()
{
    Driver->ShapePulse();
}

/** Tick 的扩散波分支：推进半径并记录已命中目标，避免同一波反复命中。 */
void APrototypeRuntime::TickWave(float Delta)
{
    Driver->TickWave(Delta);
}

/** Tick 的发射器分支：按周期选择目标并通过 OnPeriod 生成子节点。 */
void APrototypeRuntime::TickEmitter(float Delta)
{
    Driver->TickEmitter(Delta);
}

/** 从指定起点检查目标可见性，供需要独立发射/链起点的行为调用。 */
bool APrototypeRuntime::VisibleFrom(const FVector& Origin, AActor* Target) const
{
    return Driver->VisibleFrom(Origin, Target);
}

/** Tick 的连锁分支：按当前链状态选择后续目标，更新连线端点与命中记录。 */
void APrototypeRuntime::TickChain(float Delta)
{
    Driver->TickChain(Delta);
}

/** 行为事件连接器：服务器先广播本地 BehaviorEvent，再用同一 Context 和 Depth+1 生成 Child；该委托本身不是网络广播。 */
bool APrototypeRuntime::Emit(FName Event, UAbilityPrototype* Child, AActor* Target, FVector Position)
{
    return Driver->Emit(Event, Child, Target, Position);
}

/** 正常到期/取消汇合点：幂等设置结束标志；正常路径触发 OnExpire，取消路径标记共享 Context；清理附属对象并保留短暂复制寿命。 */
void APrototypeRuntime::Finish(bool bCancelled)
{
    Driver->Finish(bCancelled);
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
    Driver->OffensiveAttack(Ability, Target);
}

/** Tick 的环绕分支：清理失效引用、定期补充弹体、发射/返航并分配环绕相位；弹体伤害完成后回调 OrbitHit。 */
void APrototypeRuntime::TickOrbit(float Delta)
{
    Driver->TickOrbit(Delta);
}

/** Finish 与 EndPlay 共用清理：服务器解绑 ASC 监听并销毁地表/环绕弹体，随后清空持有引用。 */
void APrototypeRuntime::CleanupOwnedActors()
{
    Driver->CleanupOwnedActors();
}

/** 引擎销毁/世界退出兜底入口：清理所属对象与委托，再交父类释放组件。 */
void APrototypeRuntime::EndPlay(const EEndPlayReason::Type Reason)
{
    // World teardown or external destruction closes the invocation without emitting gameplay children.
    bFinished = true;
    CleanupOwnedActors();
    Super::EndPlay(Reason);
}

/** 环绕弹体 DamageApplied 委托入口：转发 OnHit 子图，并在目标已死亡时广播本地 OnKill。 */
void APrototypeRuntime::OrbitHit(AActor* Target)
{
    Driver->OrbitHit(Target);
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
