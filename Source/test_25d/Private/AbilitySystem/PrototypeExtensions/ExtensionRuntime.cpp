/** Extension GA / graph strategy -> Spawn or SpawnDriven -> extension driver. Graph owns scheduling; host owns replicated visuals. Stack continuations stay in the world subsystem and release the graph invocation explicitly. */
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/Unified/LegacyExtensionAbilityDriver.h"
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
    Driver = MakeShared<FExtensionAbilityDriver>(this);
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
AExtensionRuntime* AExtensionRuntime::Spawn(UPrototypeCastContext* C, UExtensionNode* N, FVector P, FVector D,
                                            FPureMassCombatTarget T, int32 InDepth, FVector EventCenter,
                                            bool ConfirmedHit)
{
    return SpawnInternal(C, N, P, D, T, InDepth, EventCenter, ConfirmedHit, {}, false, nullptr);
}

AExtensionRuntime* AExtensionRuntime::SpawnDriven(UPrototypeCastContext* C, UExtensionNode* N, FVector P, FVector D,
                                                  FPureMassCombatTarget T, int32 InDepth, FVector EventCenter,
                                                  bool ConfirmedHit, FLegacyAbilityChildAdmission Admission,
                                                  UExtensionNode* PresentationConfig)
{
    return SpawnInternal(C, N, P, D, T, InDepth, EventCenter, ConfirmedHit, MoveTemp(Admission), true,
                         PresentationConfig);
}

AExtensionRuntime* AExtensionRuntime::SpawnInternal(UPrototypeCastContext* C, UExtensionNode* N, FVector P, FVector D,
                                                    FPureMassCombatTarget T, int32 InDepth, FVector EventCenter,
                                                    bool ConfirmedHit, FLegacyAbilityChildAdmission Admission,
                                                    bool bDriven, UExtensionNode* PresentationConfig)
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
    FString SourceError;
    if (!C->CaptureSourceLife(SourceError))
    {
        return nullptr;
    }
    FVector CapturedTargetPosition;
    C->ResolveTargetLife(T, CapturedTargetPosition);
    if (!C->ReserveSpawn(InDepth))
        return nullptr;
    const FTransform Transform(D.Rotation(), P);
    auto* R = C->Caster->GetWorld()->SpawnActorDeferred<AExtensionRuntime>(
        StaticClass(), Transform, C->Caster.Get(), Cast<APawn>(C->Caster.Get()),
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!R)
        return nullptr;
    R->Context = C;
    R->ExecutionNode = N;
    R->Node = PresentationConfig ? PresentationConfig : N;
    R->Driver->Target = T;
    R->Driver->Depth = InDepth;
    R->Driver->Center = EventCenter;
    R->Driver->bConfirmedHit = ConfirmedHit;
    R->Driver->Direction = D.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    R->Driver->LastPosition = P;
    R->BeamEnd = P;
    R->BeamStart = P;
    R->bExternallyDriven = bDriven;
    R->Driver->ChildAdmission = MoveTemp(Admission);
    R->FinishSpawning(Transform);
    return R;
}
/** 父行为事件连接器：沿用 Context、方向及确认命中快照，以 Depth+1 调用 Spawn；不重新 Commit GA。 */
bool AExtensionRuntime::Emit(UExtensionNode* Child, FVector P, FPureMassCombatTarget T, FVector EventCenter,
                             bool ConfirmedHit)
{
    return Driver->Emit(TEXT("OnHit"), Child, P, T, EventCenter, ConfirmedHit);
}
/** 初始化本地表现；服务器按 Behavior 执行 Burst、转交 Stack、发起 Chain、解析 Attach、请求 Displace 或创建 Surface。 */
void AExtensionRuntime::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisual();
    Driver->Start();
}
/** 校验存活目标后分流 Actor/Mass 伤害，再多播表现并执行 HitChild；命中前保存的位置用于致死后子节点快照。 */
bool AExtensionRuntime::Hit(FPureMassCombatTarget T, float Scale)
{
    return Driver->Hit(T, Scale);
}
/** 初次 BeginPlay/后续 Tick 调用：结算一跳、记录去重目标和衰减，复制两端位置，再选可见的最近未命中目标。 */
void AExtensionRuntime::ChainStep()
{
    Driver->ChainStep();
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
    if (!bExternallyDriven)
    {
        Driver->Advance(Delta);
    }
}
/** 幂等结束；仅正常结束且施法者有效时生成 CompleteChild，销毁地表，更新表现和复制状态，再保留 0.35 秒销毁窗口。 */
void AExtensionRuntime::Finish(bool Cancelled)
{
    Driver->Finish(Cancelled);
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
    // World teardown or external destruction closes the invocation without emitting gameplay children.
    bFinished = true;
    if (HasAuthority() && IsValid(Surface))
        Surface->Destroy();
    Surface = nullptr;
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

/** Exposes the current invocation target without promoting it to a live target capability. */
FPureMassCombatTarget AExtensionRuntime::GetTarget() const
{
    return Driver->Target;
}
