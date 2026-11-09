/** GA / graph strategy -> Spawn or SpawnDriven -> mixed driver; graph or compatibility Tick advances behavior. Actor owns replicated endpoints, Surface and Batch6 presentation; driver owns clocks, targeting and child admission. */
// 技能运行入口：施法生成行为节点，逐帧分发弹道与范围行为，经统一目标查询分流到角色效果或实体伤害；结束时清理附属表现。
#include "PureMass/PureMassRuntime.h"
#include "AbilitySystem/Unified/LegacyMixedAbilityDriver.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "PureMass/PureMassAbility.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/Ability/MyDamageGameplayAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain/TerrainSurface.h"
#include "Interface/CombatInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

// 构建可复制的技能行为载体及默认关闭的粒子组件。
/** 构造复制 Actor、移动复制和 Niagara；表现组件初始关闭，等待有效配置。 */
APureMassRuntime::APureMassRuntime()
{
    Driver = MakeShared<FMixedAbilityDriver>(this);
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Visual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Visual"));
    Visual->SetupAttachment(RootComponent);
    Visual->SetAutoActivate(false);
}

// 服务端校验施法上下文与生成预算，延迟创建并填充行为节点。
/** 服务器验证 Context/配置/预算，延迟生成并写入方向、目标与深度后完成生成；失败返回空指针。 */
APureMassRuntime* APureMassRuntime::Spawn(UPrototypeCastContext* InContext, UAbilityPrototype* Config, FVector Position,
                                          FVector InDirection, FPureMassCombatTarget InTarget, int32 InDepth,
                                          bool bInSingle)
{
    return SpawnInternal(InContext, Config, Position, InDirection, InTarget, InDepth, bInSingle, {}, false, nullptr);
}

APureMassRuntime* APureMassRuntime::SpawnDriven(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                                FVector Position, FVector InDirection, FPureMassCombatTarget InTarget,
                                                int32 InDepth, bool bInSingle, FLegacyAbilityChildAdmission Admission,
                                                UAbilityPrototype* PresentationConfig)
{
    return SpawnInternal(InContext, Config, Position, InDirection, InTarget, InDepth, bInSingle, MoveTemp(Admission),
                         true, PresentationConfig);
}

APureMassRuntime* APureMassRuntime::SpawnInternal(UPrototypeCastContext* InContext, UAbilityPrototype* Config,
                                                  FVector Position, FVector InDirection, FPureMassCombatTarget InTarget,
                                                  int32 InDepth, bool bInSingle, FLegacyAbilityChildAdmission Admission,
                                                  bool bDriven, UAbilityPrototype* PresentationConfig)
{
    AActor* Caster = InContext ? InContext->Caster.Get() : nullptr;
    if (!IsValid(Caster) || !Caster->HasAuthority() || !Config || !Config->IsValidConfiguration() ||
        Position.ContainsNaN() || InDirection.ContainsNaN())
        return nullptr;
    if (Caster->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Caster))
        return nullptr;
    if (Config->Kind == EAbilityPrototypeKind::Surface &&
        !APrototypeRuntime::ProjectSurfaceToGround(Caster, Position, Position))
        return nullptr;
    FString SourceError;
    if (!InContext->CaptureSourceLife(SourceError))
    {
        return nullptr;
    }
    FVector CapturedTargetPosition;
    InContext->ResolveTargetLife(InTarget, CapturedTargetPosition);
    if (!InContext->ReserveSpawn(InDepth))
        return nullptr;
    InDirection = InDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    if (Config->Kind == EAbilityPrototypeKind::Beam)
        Position = Caster->GetActorLocation() + FVector(0, 0, 30);
    const FTransform Transform(InDirection.Rotation(), Position);
    auto* R = Caster->GetWorld()->SpawnActorDeferred<APureMassRuntime>(
        StaticClass(), Transform, Caster, Cast<APawn>(Caster), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!R)
        return nullptr;
    R->Context = InContext;
    R->ExecutionPrototype = Config;
    R->Prototype = PresentationConfig ? PresentationConfig : Config;
    R->Driver->Direction = InDirection;
    R->Driver->ChainOrigin = Position;
    R->Driver->Target = InTarget;
    R->Driver->Depth = InDepth;
    R->bSingle = bInSingle;
    R->BeamEnd = Position + InDirection * Config->Range;
    if (Config->Kind == EAbilityPrototypeKind::Beam || Config->Kind == EAbilityPrototypeKind::Chain)
        PureMassCombat::Resolve(Caster->GetWorld(), Caster, InTarget, R->BeamEnd);
    R->bExternallyDriven = bDriven;
    R->Driver->ChildAdmission = MoveTemp(Admission);
    R->FinishSpawning(Transform);
    return R;
}

// 开始时初始化表现，并按原型分发散射、瞬发范围或地表创建。
/** 初始化表现；服务器执行初始散射、瞬发范围或地表生成，后续持续行为交 Tick。 */
void APureMassRuntime::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisual();
    Driver->Start();
}

// 通过统一遮挡查询判断目标是否可见。
/** 委托 PureMassCombat::Visible，从指定起点检查 Actor/实体遮挡并忽略本执行器。 */
bool APureMassRuntime::Visible(FVector Origin, const FPureMassCombatTarget& Candidate) const
{
    return Driver->Visible(Origin, Candidate);
}

// 命中后按角色或实体分流伤害，追加减速、命中特效和子节点。
/** 服务器验证来源 Avatar/世界与目标；Actor 应用 GE，Mass ApplyDamage；随后追加 Batch6 减速、命中表现与 OnHit 子节点。 */
bool APureMassRuntime::Hit(FPureMassCombatTarget Candidate, float Scale)
{
    return Driver->Hit(Candidate, Scale);
}

// 沿共享施法上下文生成下一层子节点并沿用递归预算。
/** 服务器沿同一 Context 以 Depth+1 生成子节点，继承生成预算和取消状态，不再次扣根技能费用。 */
bool APureMassRuntime::Emit(UAbilityPrototype* Child, FVector Position, FPureMassCombatTarget Candidate)
{
    return Driver->Emit(TEXT("OnHit"), Child, Position, Candidate);
}

// 从范围候选中挑选未命中过且可见的最近目标。
/** 范围查询后过滤已命中、不可解析及不可见目标，选择距离最近的混合目标。 */
FPureMassCombatTarget APureMassRuntime::Nearest(FVector Origin, float Radius) const
{
    return Driver->Nearest(Origin, Radius);
}

// 按扇形、环形、直线或波前规则过滤候选并逐一命中。
/** 范围分支入口：按扇形、环形、直线/高度或波前筛选，调用 Hit；波形记录已命中目标避免重复。 */
void APureMassRuntime::Pulse()
{
    Driver->Pulse();
}

// 推进弹道并扫掠命中，处理穿透、追踪、回返及墙体终止。
/** Tick 弹体分支：推进追踪/穿透/返航，统一扫掠 Actor、实体及墙体，结算命中并处理结束。 */
void APureMassRuntime::Projectile(float Delta)
{
    Driver->Projectile(Delta);
}

// 执行一跳连锁伤害，递减倍率并选择下一目标。
/** Tick 连锁分支：结算当前跳及衰减，更新链起点，选择下一未命中目标。 */
void APureMassRuntime::Chain()
{
    Driver->Chain();
}

// 在已提交攻击后更新施法者环绕技能的目标。
/** 成功提交攻击后的入口：将混合目标通知施法者当前环绕节点，允许 Actor 和 Mass 目标。 */
void APureMassRuntime::NotifyCommittedAttack(AActor* Caster, UGameplayAbility* Ability, FPureMassCombatTarget Candidate)
{
    auto* DamageAbility = Cast<UMyDamageGameplayAbility>(Ability);
    if (!IsValid(Caster) || !Caster->HasAuthority() || !DamageAbility || !DamageAbility->HasCommittedOffensiveAttack())
        return;
    FVector Position;
    if (!PureMassCombat::Resolve(Caster->GetWorld(), Caster, Candidate, Position))
        return;
    for (TActorIterator<APureMassRuntime> It(Caster->GetWorld()); It; ++It)
        if (It->GetOwner() == Caster && !It->bFinished && It->Prototype &&
            It->Prototype->Kind == EAbilityPrototypeKind::Orbit && It->Prototype->OrbitSettings &&
            FVector::DistSquared(Caster->GetActorLocation(), Position) <=
                FMath::Square(It->Prototype->OrbitSettings->MaxTargetDistance))
        {
            FVector CurrentPosition;
            if (It->Context && It->Context->ResolveTargetLife(Candidate, CurrentPosition))
            {
                It->Driver->Target = Candidate;
            }
        }
}

// 生成环绕弹体，按发射间隔追踪目标并扫掠结算。
/** Tick 环绕分支：维护轻量弹体状态、发射间隔和追踪扫掠；这些弹体不是每颗一个敌人 Actor。 */
void APureMassRuntime::Orbit(float Delta)
{
    Driver->Orbit(Delta);
}

// 先同步本地表现，再由服务端推进寿命、耗蓝和各类行为周期。
/** 本地更新粒子/Batch6 表现；权威端校验施法者、取消、寿命和扣蓝，再按 Kind 推进周期行为。 */
void APureMassRuntime::Tick(float Delta)
{
    Super::Tick(Delta);
    if (Prototype && GetNetMode() != NM_DedicatedServer)
    {
        if (const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
            Config && Config->bFollowCasterToggle && !bFinished)
            UpdateBatch6Presentation();
        if (Batch6Snow && !bFinished && Batch6Area)
        {
            // A bounded instanced flurry supplements existing Niagara assets; presentation only.
            const float Time = GetWorld()->GetTimeSeconds();
            for (int32 I = 0; I < 64; ++I)
            {
                const float Angle = I * 2.39996323f + Time * .35f;
                const float Radius = Prototype->Radius * FMath::Sqrt((I + .5f) / 64.f);
                const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
                                     30.f + 260.f * (1.f - FMath::Frac(Time * .6f + I * .618034f)));
                Batch6Snow->UpdateInstanceTransform(
                    I, FTransform(FRotator::ZeroRotator, Offset, FVector(.035f + (I % 4) * .015f)), false, I == 63,
                    true);
            }
        }
        Visual->SetVariableVec3(TEXT("BeamEnd"), BeamEnd);
        Visual->SetVariableVec3(TEXT("User.BeamEnd"), BeamEnd);
        Visual->SetVariableVec3(TEXT("Beam Start"), GetActorLocation());
        Visual->SetVariableVec3(TEXT("Beam End"), BeamEnd);
        if (Prototype->Kind == EAbilityPrototypeKind::Wave)
            Visual->SetRelativeScale3D(FVector(FMath::Max(.05f, WaveRadius / 100.f)));
        while (BoltVisuals.Num() < OrbitBolts.Num())
        {
            auto* V = NewObject<UNiagaraComponent>(this);
            V->SetupAttachment(RootComponent);
            V->RegisterComponent();
            V->SetAsset(Prototype->LoopVFX);
            V->Activate(true);
            BoltVisuals.Add(V);
        }
        for (int32 I = 0; I < BoltVisuals.Num(); ++I)
        {
            BoltVisuals[I]->SetVisibility(!bFinished && OrbitBolts.IsValidIndex(I));
            if (OrbitBolts.IsValidIndex(I))
                BoltVisuals[I]->SetWorldLocation(OrbitBolts[I].Position);
        }
    }
    if (!bExternallyDriven)
    {
        Driver->Advance(Delta);
    }
}

// 幂等结束节点；正常到期触发子节点，取消则标记共享上下文并清理表现。
/** 幂等结束：正常结束生成 OnExpire，取消标记共享 Context；销毁地表、清 OrbitBolts、更新复制并延迟销毁。 */
void APureMassRuntime::Finish(bool bCancelled)
{
    Driver->Finish(bCancelled);
}

// 服务端取消整次施法上下文并结束当前节点。
/** 外部/GA 的服务器取消入口：标记整次施法 Context 并结束本节点。 */
void APureMassRuntime::CancelCast()
{
    if (!HasAuthority())
        return;
    if (Context)
        Context->bCancelled = true;
    Finish(true);
}

// 初始播放与复制通知共同更新循环特效和结束表现。
/** BeginPlay/Finish/RepNotify 共用：更新循环和一次结束表现；专用服务器跳过；初始化光束端点后才激活 Niagara。 */
void APureMassRuntime::RefreshVisual()
{
    if (!Prototype || GetNetMode() == NM_DedicatedServer)
        return;
    if (bFinished)
    {
        if (Batch6Area)
            Batch6Area->SetVisibility(false);
        if (Batch6Snow)
            Batch6Snow->SetVisibility(false);
        Visual->DeactivateImmediate();
        for (const auto& V : BoltVisuals)
            if (V)
                V->DeactivateImmediate();
        if (!bPresented && Prototype->ImpactVFX)
        {
            const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
            const FVector Scale =
                Config && Config->bUseAuthoredVFXScale ? Prototype->ImpactVFXScale : FVector::OneVector;
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Prototype->ImpactVFX, GetActorLocation(),
                                                           FRotator::ZeroRotator, Scale);
        }
        bPresented = true;
        return;
    }
    if (Prototype->LoopVFX && Prototype->Kind != EAbilityPrototypeKind::Orbit &&
        (Prototype->Kind != EAbilityPrototypeKind::Multishot || bSingle))
    {
        Visual->SetAsset(Prototype->LoopVFX);
        // Beam emitters read their endpoints during particle spawn. Tick is too late:
        // the first particles otherwise retain the asset's authored world origin.
        Visual->SetVariableVec3(TEXT("Beam Start"), GetActorLocation());
        Visual->SetVariableVec3(TEXT("Beam End"), BeamEnd);
        Visual->SetVariableVec3(TEXT("BeamEnd"), BeamEnd);
        Visual->SetVariableVec3(TEXT("User.BeamEnd"), BeamEnd);
        Visual->Activate(true);
    }
    if (const auto* Config = Cast<UBatch6FieldPrototype>(Prototype); Config && Config->bUseAuthoredVFXScale)
        Visual->SetRelativeScale3D(Prototype->LoopVFXScale);
    else if (Prototype->Kind == EAbilityPrototypeKind::Ring || Prototype->Kind == EAbilityPrototypeKind::Field)
        Visual->SetRelativeScale3D(FVector(Prototype->Radius / 100.f, Prototype->Radius / 100.f, .25f));
    UpdateBatch6Presentation();
}

// 按范围配置创建贴地圆盘与雪花实例，仅用于表现。
/** 配置驱动的地面圆盘/雪花组件创建与位置更新，仅用于表现；组件由 Runtime 持有，不参与命中。 */
void APureMassRuntime::UpdateBatch6Presentation()
{
    const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
    if (!Config || bFinished || GetNetMode() == NM_DedicatedServer)
        return;
    FVector Ground = GetActorLocation();
    if (Config->bGroundPresentation || Config->bShowAreaDisc)
        APrototypeRuntime::ProjectSurfaceToGround(GetOwner() ? GetOwner() : this, GetActorLocation(), Ground);
    if (Config->bGroundPresentation)
        Visual->SetWorldLocation(Ground);
    if (!Config->bShowAreaDisc || Config->Kind != EAbilityPrototypeKind::Field || !Config->AreaMaterial)
        return;
    if (!Batch6Area)
    {
        Batch6Area = NewObject<UStaticMeshComponent>(this);
        Batch6Area->SetupAttachment(RootComponent);
        Batch6Area->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
        Batch6Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch6Area->SetCastShadow(false);
        Batch6Area->SetMaterial(0, Config->AreaMaterial);
        Batch6Area->RegisterComponent();
    }
    Batch6Area->SetWorldLocation(Ground + FVector(0, 0, 1));
    Batch6Area->SetWorldRotation(FRotator::ZeroRotator);
    Batch6Area->SetWorldScale3D(FVector(Config->Radius / 50.f, Config->Radius / 50.f, .01f));
    if (Config->bSnowfall && !Batch6Snow)
    {
        Batch6Snow = NewObject<UInstancedStaticMeshComponent>(this);
        Batch6Snow->SetupAttachment(RootComponent);
        Batch6Snow->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Batch6Snow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch6Snow->SetCastShadow(false);
        Batch6Snow->SetMaterial(0, Config->AreaMaterial);
        Batch6Snow->RegisterComponent();
        for (int32 I = 0; I < 64; ++I)
            Batch6Snow->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, 150), FVector(.035f)), false);
    }
    if (Batch6Snow)
    {
        Batch6Snow->SetWorldLocation(Ground);
        Batch6Snow->SetWorldRotation(FRotator::ZeroRotator);
        Batch6Snow->SetWorldScale3D(FVector::OneVector);
    }
}

// 接收命中特效广播，在非专用服务器播放粒子。
/** 非专用服务器生成 ImpactVFX，并根据 Batch6 配置应用资产缩放。 */
void APureMassRuntime::Impact_Implementation(FVector Position)
{
    if (Prototype && Prototype->ImpactVFX && GetNetMode() != NM_DedicatedServer)
    {
        const auto* Config = Cast<UBatch6FieldPrototype>(Prototype);
        const FVector Scale = Config && Config->bUseAuthoredVFXScale ? Prototype->ImpactVFXScale : FVector::OneVector;
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Prototype->ImpactVFX, Position, FRotator::ZeroRotator,
                                                       Scale);
    }
}

// 结束生命周期时清理本对象拥有的资源，再调用父类收尾。
/** 引擎退出兜底清理所属 Surface，再由父类回收 Actor 及其组件。 */
void APureMassRuntime::EndPlay(EEndPlayReason::Type Reason)
{
    // World teardown or external destruction closes the invocation without emitting gameplay children.
    bFinished = true;
    if (HasAuthority() && IsValid(Surface))
        Surface->Destroy();
    Surface = nullptr;
    OrbitBolts.Reset();
    Super::EndPlay(Reason);
}

// 登记行为配置、目标端点、波半径和结束状态等复制字段。
/** 注册配置、光束端点、波半径、环绕状态、结束标记和单弹体状态；伤害 Context 不复制。 */
void APureMassRuntime::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APureMassRuntime, Prototype);
    DOREPLIFETIME(APureMassRuntime, BeamEnd);
    DOREPLIFETIME(APureMassRuntime, WaveRadius);
    DOREPLIFETIME(APureMassRuntime, OrbitBolts);
    DOREPLIFETIME(APureMassRuntime, bFinished);
    DOREPLIFETIME(APureMassRuntime, bSingle);
}
