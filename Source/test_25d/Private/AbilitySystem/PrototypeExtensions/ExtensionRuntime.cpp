/**
 * 扩展行为执行器：Execute/Emit -> Spawn -> FinishSpawning/BeginPlay；后续引擎 Tick 驱动周期、弹体、转移和结束。Hit 分流 Actor GE 与 Mass 伤害；Stack/Displace 将状态交给世界子系统。Node/端点/结束状态复制给客户端，Context 不复制。Finish 清理地表并延迟销毁；EndPlay 为地表清理兜底。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Terrain/TerrainSurface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

/** 构造复制 Actor、移动复制及 Niagara 子组件；在 PostUpdateWork Tick 推进，Visual 随 Actor 销毁。 */
AExtensionRuntime::AExtensionRuntime()
{
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Visual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Visual"));
    Visual->SetupAttachment(RootComponent);
    Visual->SetAutoActivate(false);
}
/** 服务器验证施法者、整图与坐标，限制地表数和共享生成预算；延迟生成期间写入 Context/目标/事件中心后 FinishSpawning。 */
AExtensionRuntime* AExtensionRuntime::Spawn(UExtensionCastContext* C, UExtensionNode* N, FVector P, FVector D,
                                            FPureMassCombatTarget T, int32 InDepth, FVector EventCenter,
                                            bool ConfirmedHit)
{
    if (!UExtensionWorldSubsystem::CasterAlive(C) || !C->Caster->HasAuthority() || !N || !N->ValidateGraph() ||
        P.ContainsNaN() || D.ContainsNaN() || EventCenter.ContainsNaN())
        return nullptr;
    if (N->Behavior == EExtensionKind::Surface)
    {
        if (C->Surfaces >= 8 || !APrototypeRuntime::ProjectSurfaceToGround(C->Caster.Get(), P, P))
            return nullptr;
        ++C->Surfaces;
    }
    if (!C->ReserveSpawn(InDepth))
        return nullptr;
    const FTransform Transform(D.Rotation(), P);
    auto* R = C->Caster->GetWorld()->SpawnActorDeferred<AExtensionRuntime>(
        StaticClass(), Transform, C->Caster.Get(), Cast<APawn>(C->Caster.Get()),
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!R)
        return nullptr;
    R->Context = C;
    R->Node = N;
    R->Target = T;
    R->Depth = InDepth;
    R->Center = EventCenter;
    R->bConfirmedHit = ConfirmedHit;
    R->Direction = D.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    R->LastPosition = P;
    R->BeamEnd = P;
    R->BeamStart = P;
    R->FinishSpawning(Transform);
    return R;
}
/** 父行为事件连接器：沿用 Context、方向及确认命中快照，以 Depth+1 调用 Spawn；不重新 Commit GA。 */
bool AExtensionRuntime::Emit(UExtensionNode* Child, FVector P, FPureMassCombatTarget T, FVector EventCenter,
                             bool ConfirmedHit)
{
    return Child && Spawn(Context, Child, P, Direction, T, Depth + 1, EventCenter, ConfirmedHit);
}
/** 初始化本地表现；服务器按 Behavior 执行 Burst、转交 Stack、发起 Chain、解析 Attach、请求 Displace 或创建 Surface。 */
void AExtensionRuntime::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisual();
    if (!HasAuthority() || !Context || !Node)
        return;
    using K = EExtensionKind;
    if (Node->Behavior == K::Burst)
    {
        for (auto T : PureMassCombat::Gather(GetWorld(), GetOwner(), GetActorLocation(), Node->Radius))
            if (PureMassCombat::Visible(GetWorld(), GetOwner(), GetActorLocation() + FVector(0, 0, 2), T, this))
                Hit(T);
        Finish();
    }
    else if (Node->Behavior == K::Stack)
    {
        GetWorld()->GetSubsystem<UExtensionWorldSubsystem>()->AddStack(Context, Node, Target, GetActorLocation(),
                                                                       Direction, Depth, bConfirmedHit);
        // Completion belongs to the persistent stack, not the application node.
        bFinished = true;
        RefreshVisual();
        SetLifeSpan(.35f);
    }
    else if (Node->Behavior == K::Chain)
        ChainStep();
    else if (Node->Behavior == K::Attach)
    {
        if (!PureMassCombat::Resolve(GetWorld(), GetOwner(), Target, LastPosition))
        {
            Finish(true);
            return;
        }
        SetActorLocation(LastPosition);
        HitTargets.Add(Target);
    }
    else if (Node->Behavior == K::Displace)
    {
        auto* S = GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
        auto Targets = Node->bSingleTarget
                           ? TArray<FPureMassCombatTarget>{Target}
                           : PureMassCombat::Gather(GetWorld(), GetOwner(), GetActorLocation(), Node->Radius);
        for (auto T : Targets)
        {
            // Cursor ground hits lie on the collision surface; lift only the sight ray.
            if (!PureMassCombat::Visible(GetWorld(), GetOwner(), GetActorLocation() + FVector(0, 0, 2), T, this))
                continue;
            FGuid Token =
                S->RequestDisplacement(Context, Node, T, Node->bSingleTarget ? Center : GetActorLocation(), Direction);
            if (Token.IsValid())
                Moves.Add(Token);
        }
        if (Node->bSingleTarget && Moves.IsEmpty())
            Finish(true);
    }
    else if (Node->Behavior == K::Surface)
    {
        FTransform Transform(FRotator::ZeroRotator, GetActorLocation());
        Surface = GetWorld()->SpawnActorDeferred<ATerrainSurface>(Node->SurfaceClass, Transform, GetOwner(),
                                                                  Cast<APawn>(GetOwner()),
                                                                  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Surface)
        {
            Finish(true);
            return;
        }
        Surface->bOnlyAffectCasterEnemies = true;
        Surface->bAffectPlayers = false;
        Surface->bAffectEnemies = true;
        Surface->SurfaceSize = FVector2D(Node->Radius * 2);
        Surface->FinishSpawning(Transform);
        if (Surface->Ground)
            Surface->Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}
/** 校验存活目标后分流 Actor/Mass 伤害，再多播表现并执行 HitChild；命中前保存的位置用于致死后子节点快照。 */
bool AExtensionRuntime::Hit(FPureMassCombatTarget T, float Scale)
{
    FVector P;
    if (bFinished || !HasAuthority() || !UExtensionWorldSubsystem::CasterAlive(Context) ||
        !PureMassCombat::Resolve(GetWorld(), GetOwner(), T, P))
        return false;
    const float Damage = Node->DamageAtLevel(Context->Level) * Scale;
    if (T.IsActor())
    {
        auto Params = Context->DamageParams;
        Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(T.Actor.Get());
        if (!Params.SourceAbilitySystemComponent || !Params.TargetAbilitySystemComponent ||
            !Params.DamageGameplayEffectClass)
            return false;
        Params.WorldContextObject = this;
        Params.BaseDamage = Damage;
        Params.DamageType = Node->DamageType;
        Params.bIsRadialDamage = false;
        Params.DebuffChance = 0;
        if (Damage > 0)
            UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    }
    else
    {
        FPureMassDamage D;
        D.Source = Context->Caster;
        D.Amount = Damage;
        D.DamageType = Node->DamageType;
        GetWorld()->GetSubsystem<UPureMassSubsystem>()->ApplyDamage(T.EntityId, D);
    }
    Impact(P);
    // Snapshot before damage: a lethal hit may already have removed the entity.
    Emit(Node->HitChild, P, T, GetActorLocation(), true);
    return true;
}
/** 初次 BeginPlay/后续 Tick 调用：结算一跳、记录去重目标和衰减，复制两端位置，再选可见的最近未命中目标。 */
void AExtensionRuntime::ChainStep()
{
    FVector P;
    if (!PureMassCombat::Resolve(GetWorld(), GetOwner(), Target, P) ||
        !PureMassCombat::Visible(GetWorld(), GetOwner(), LastPosition, Target, this))
    {
        Finish();
        return;
    }
    BeamStart = LastPosition;
    BeamEnd = P;
    HitTargets.Add(Target);
    Hit(Target, FMath::Pow(Node->DamageFalloff, Hits++));
    LastPosition = P;
    ForceNetUpdate();
    if (Hits >= Node->Count)
    {
        Finish();
        return;
    }
    float Best = Node->Radius * Node->Radius;
    FPureMassCombatTarget Next;
    for (auto T : PureMassCombat::Gather(GetWorld(), GetOwner(), P, Node->Radius))
    {
        FVector Q;
        if (HitTargets.Contains(T) || !PureMassCombat::Resolve(GetWorld(), GetOwner(), T, Q))
            continue;
        float Dist = FVector::DistSquared(P, Q);
        if (Dist < Best && PureMassCombat::Visible(GetWorld(), GetOwner(), P, T, this))
        {
            Best = Dist;
            Next = T;
        }
    }
    Target = Next;
    if (!Target.IsSet())
        Finish();
}
/** 客户端仅更新连线参数；服务器验证 Context，按剩余寿命推进跟随/转移/扫掠/周期与结束；与 GA 激活不处于同一同步栈。 */
void AExtensionRuntime::Tick(float Delta)
{
    Super::Tick(Delta);
    if (Visual && Node && Node->Behavior == EExtensionKind::Chain)
    {
        Visual->SetVariableVec3(TEXT("Beam Start"), BeamStart);
        Visual->SetVariableVec3(TEXT("Beam End"), BeamEnd);
    }
    if (!HasAuthority())
    {
        if (Visual && Node && Node->Behavior == EExtensionKind::Chain)
            Visual->SetVariableVec3(TEXT("BeamEnd"), BeamEnd);
        return;
    }
    if (bFinished || !Node)
        return;
    if (!UExtensionWorldSubsystem::CasterAlive(Context))
    {
        Finish(true);
        return;
    }
    using K = EExtensionKind;
    Delta = FMath::Max(0.f, Delta);
    const float ActiveDelta = FMath::Min(Delta, FMath::Max(0.f, Node->Duration - Age - 2 * KINDA_SMALL_NUMBER));
    Age += Delta;
    const bool bExpired = Age + KINDA_SMALL_NUMBER >= Node->Duration;
    if (Node->Behavior == K::Attach)
    {
        FVector P;
        if (!PureMassCombat::Resolve(GetWorld(), GetOwner(), Target, P))
        {
            FPureMassCombatTarget Next;
            float Best = FMath::Square(Node->TransferRadius);
            if (Transfers < Node->MaxTransfers)
                for (auto T : PureMassCombat::Gather(GetWorld(), GetOwner(), LastPosition, Node->TransferRadius))
                {
                    FVector Q;
                    if (HitTargets.Contains(T) || !PureMassCombat::Resolve(GetWorld(), GetOwner(), T, Q))
                        continue;
                    float D = FVector::DistSquared(LastPosition, Q);
                    if (D < Best && PureMassCombat::Visible(GetWorld(), GetOwner(), LastPosition, T, this))
                    {
                        Best = D;
                        Next = T;
                    }
                }
            if (!Next.IsSet())
            {
                Finish(true);
                return;
            }
            Target = Next;
            HitTargets.Add(Next);
            ++Transfers;
            PureMassCombat::Resolve(GetWorld(), GetOwner(), Target, P);
        }
        LastPosition = P;
        SetActorLocation(P);
    }
    if (Node->Behavior == K::Displace && Node->bSingleTarget)
    {
        FVector P;
        auto* S = GetWorld()->GetSubsystem<UExtensionWorldSubsystem>();
        if (!PureMassCombat::Resolve(GetWorld(), GetOwner(), Target, P) ||
            (!Moves.IsEmpty() && S->WasMoveReplaced(Moves[0])))
        {
            Finish(true);
            return;
        }
        SetActorLocation(P);
        LastPosition = P;
    }
    if (Node->Behavior == K::Delay)
    {
        if (Age >= Node->ArmDelay)
            Finish();
        return;
    }
    if (Node->Behavior == K::Projectile)
    {
        float Step = FMath::Min(Node->Speed * ActiveDelta, Node->Range - Travel);
        FVector Start = GetActorLocation(), End = Start + Direction * Step;
        FHitResult Wall;
        float Time = 1;
        auto T =
            PureMassCombat::Sweep(GetWorld(), GetOwner(), Start, End, Node->CollisionRadius, Time, Wall, nullptr, this);
        if (T.IsSet())
        {
            SetActorLocation(FMath::Lerp(Start, End, Time));
            Hit(T);
            Finish();
            return;
        }
        if (Wall.bBlockingHit)
        {
            SetActorLocation(Wall.ImpactPoint);
            Finish();
            return;
        }
        SetActorLocation(End);
        Travel += Step;
        if (Travel >= Node->Range)
            Finish();
    }
    if (Node->Behavior != K::Attach && Node->Behavior != K::Chain)
    {
        if (bExpired)
            Finish();
        return;
    }
    Clock += ActiveDelta;
    for (int32 I = 0; Clock + KINDA_SMALL_NUMBER >= Node->Period && I < 16 && !bFinished; ++I)
    {
        Clock -= Node->Period;
        if (Node->Behavior == K::Chain)
            ChainStep();
        else if (PureMassCombat::Visible(GetWorld(), GetOwner(), GetOwner()->GetActorLocation(), Target, this))
            Emit(Node->PeriodChild, LastPosition, Target, LastPosition);
    }
    Clock = FMath::Min(Clock, Node->Period);
    if (bExpired)
        Finish();
}
/** 幂等结束；仅正常结束且施法者有效时生成 CompleteChild，销毁地表，更新表现和复制状态，再保留 0.35 秒销毁窗口。 */
void AExtensionRuntime::Finish(bool Cancelled)
{
    if (bFinished)
        return;
    bFinished = true;
    if (!Cancelled && UExtensionWorldSubsystem::CasterAlive(Context))
        Emit(Node->CompleteChild, Node->Behavior == EExtensionKind::Chain ? LastPosition : GetActorLocation(), Target,
             GetActorLocation());
    if (IsValid(Surface))
        Surface->Destroy();
    Surface = nullptr;
    RefreshVisual();
    ForceNetUpdate();
    SetLifeSpan(.35f);
}
/** BeginPlay、Finish 及复制通知共用表现入口：循环粒子或一次结束表现；专用服务器跳过，不执行伤害。 */
void AExtensionRuntime::RefreshVisual()
{
    if (!Node || GetNetMode() == NM_DedicatedServer)
        return;
    if (bFinished)
    {
        Visual->DeactivateImmediate();
        if (!bPresented && Node->ImpactVFX)
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Node->ImpactVFX, GetActorLocation());
        bPresented = true;
        return;
    }
    if (Node->LoopVFX)
    {
        Visual->SetAsset(Node->LoopVFX);
        Visual->Activate(true);
    }
    if (Node->Behavior == EExtensionKind::Attach)
        Visual->SetRelativeLocation(FVector(0, 0, 85));
    if (Node->Behavior == EExtensionKind::Displace)
        Visual->SetRelativeScale3D(FVector(Node->Radius / 100, Node->Radius / 100, .3f));
}
/** 接收命中多播，在非专用服务器按 Node.ImpactVFX 生成一次性粒子。 */
void AExtensionRuntime::Impact_Implementation(FVector P)
{
    if (Node && Node->ImpactVFX && GetNetMode() != NM_DedicatedServer)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Node->ImpactVFX, P);
}
/** 引擎退出/销毁入口：服务器兜底销毁所属地表，再调用父类；组件由 Actor 生命周期回收。 */
void AExtensionRuntime::EndPlay(EEndPlayReason::Type R)
{
    if (HasAuthority() && IsValid(Surface))
        Surface->Destroy();
    Super::EndPlay(R);
}
/** 注册 Node、BeamStart/End 与 bFinished；子图 Context 和预算仅保存在服务器。 */
void AExtensionRuntime::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AExtensionRuntime, Node);
    DOREPLIFETIME(AExtensionRuntime, BeamEnd);
    DOREPLIFETIME(AExtensionRuntime, BeamStart);
    DOREPLIFETIME(AExtensionRuntime, bFinished);
}
