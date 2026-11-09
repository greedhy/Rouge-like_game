/**
 * 分叉连锁链：GA ActivateAbility -> 鼠标目标任务 ReceiveTarget -> Commit -> Delivery.ForkedChain图策略的广度优先 Queue/Visited -> Actor GE 或 Mass 伤害 -> Presentation Actor。不是 Extension 子图递归。预算限制总目标/深度/兄弟数；结果段复制给客户端，表现 Actor 1.5 秒后销毁，本地组件重建时先回收旧组件。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "PureMass/PureMassAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

/** 构造始终相关的复制 Actor 并加载圆柱兜底网格，避免 Niagara 参数不兼容时无连线。 */
ABatch6ChainPresentation::ABatch6ChainPresentation()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    FallbackMesh = Mesh.Object;
}
/** 生成/复制后本地 Present；服务器设置 1.5 秒寿命，表现不延长 GA 生命周期。 */
void ABatch6ChainPresentation::BeginPlay()
{
    Super::BeginPlay();
    Present();
    if (HasAuthority())
        SetLifeSpan(1.5f);
}
/** 复制 Segments 与 BeamSystem，二者的 RepNotify 均可触发 Present 重建。 */
void ABatch6ChainPresentation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ABatch6ChainPresentation, Segments);
    DOREPLIFETIME(ABatch6ChainPresentation, BeamSystem);
}
/** 专用服务器跳过；销毁上次动态组件，按段生成无碰撞圆柱和 Niagara 并写入端点，不造成伤害。 */
void ABatch6ChainPresentation::Present()
{
    if (GetNetMode() == NM_DedicatedServer)
        return;
    for (const auto& Component : PresentationComponents)
        if (IsValid(Component.Get()))
            Component->DestroyComponent();
    PresentationComponents.Reset();
    for (const auto& S : Segments)
    {
        // Real render geometry remains visible in packaged builds and if the selected Niagara has no beam parameters.
        if (FallbackMesh)
        {
            auto* Cylinder = NewObject<UStaticMeshComponent>(this);
            Cylinder->SetStaticMesh(FallbackMesh);
            Cylinder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Cylinder->SetupAttachment(GetRootComponent());
            Cylinder->RegisterComponent();
            Cylinder->SetWorldLocation((S.Start + S.End) * .5f);
            Cylinder->SetWorldRotation(FRotationMatrix::MakeFromZ(S.End - S.Start).Rotator());
            Cylinder->SetWorldScale3D(FVector(.035f, .035f, FVector::Distance(S.Start, S.End) / 100.f));
            PresentationComponents.Add(Cylinder);
        }
        if (BeamSystem)
            if (auto* V = UNiagaraFunctionLibrary::SpawnSystemAttached(BeamSystem, GetRootComponent(), NAME_None,
                                                                       S.Start, FRotator::ZeroRotator,
                                                                       EAttachLocation::KeepWorldPosition, true))
            {
                V->SetVariableVec3(TEXT("User.Start"), S.Start);
                V->SetVariableVec3(TEXT("User.End"), S.End);
                V->SetVariableVec3(TEXT("User.BeamEnd"), S.End);
                PresentationComponents.Add(V);
            }
    }
}
/** 构造每 Actor 实例化、本地预测输入 GA，默认关闭基础 Debuff/击退概率。 */
UBatch6ChainAbility::UBatch6ChainAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    DebuffChance = 0;
    KnockbackChance = 0;
}
/** 清目标去重标记，创建鼠标目标任务并设置三秒超时；实际攻击在服务器 ReceiveTarget 中完成。 */
void UBatch6ChainAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                          FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    bResolved = false;
    ActivationSource = {};
    if (Info && Info->IsNetAuthority())
    {
        FString Error;
        if (!AbilityExecutionIdentity::CaptureSource(Info->AvatarActor.Get(), ActivationSource, Error))
        {
            Abort();
            return;
        }
    }
    GetWorld()->GetTimerManager().SetTimer(Timeout, this, &ThisClass::Abort, 3.f);
    auto* Task = UTargetDataUnderMouse::CreateTargetDataUnderMouse(this);
    Task->ValidData.AddDynamic(this, &ThisClass::ReceiveTarget);
    Task->ReadyForActivation();
}
/** 目标无效或超时统一取消入口，仅结束仍激活的技能。 */
void UBatch6ChainAbility::Abort()
{
    if (IsActive())
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
/** 服务器 Commit 回调，使用 UPureMassAbility 的原生扣蓝 GE。 */
void UBatch6ChainAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                    FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority() && Prototype)
        UPureMassAbility::SpendMana(Info->AvatarActor.Get(), Prototype->ManaCost);
}
/** UI 生成当前等级伤害、总目标、深度、分叉数和费用说明。 */
FString UBatch6ChainAbility::GetDescription(int32 Level)
{
    if (!Prototype)
        return Super::GetDescription(Level);
    return FString::Printf(
        TEXT(
            "<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>%.0f damage; %d branches, %d hops, up to %d foes.</>"),
        *Prototype->DisplayName.ToString(), Level, Prototype->ManaCost, Prototype->Cooldown,
        Prototype->DamageAtLevel(Level), BranchesPerTarget, MaxDepth, MaxTargets);
}
/** 无论成功取消都清除本技能目标超时，再交父类清理任务。 */
void UBatch6ChainAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                     FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    CloseExecutionSession(Cancelled);
    if (Batch6Execution)
    {
        auto* Execution = Batch6Execution.Get();
        Batch6Execution = nullptr;
        if (!Execution->IsFinished())
        {
            Execution->Cancel();
        }
    }
    ActivationSource = {};
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timeout);
    Super::EndAbility(Handle, Info, Activation, Replicate, Cancelled);
}
/** 服务器消费一次目标请求，查询瞄准点附近首目标并校验距离遮挡；Commit 后广度优先结算、生成复制连线宿主并结束。 */
void UBatch6ChainAbility::ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bResolved || !CurrentActorInfo->IsNetAuthority())
        return;
    bResolved = true;
    const auto* Hit = Data.Num() == 1 && Data.Get(0) ? Data.Get(0)->GetHitResult() : nullptr;
    AActor* Caster = nullptr;
    if (!Hit || Hit->ImpactPoint.ContainsNaN() || !AbilityExecutionIdentity::ResolveSource(ActivationSource, Caster) ||
        !DamageEffectClass)
    {
        Abort();
        return;
    }
    FVector Origin = Caster->GetActorLocation();
    auto Candidates = PureMassCombat::Gather(GetWorld(), Caster, Hit->ImpactPoint, 150.f);
    Candidates.Sort(
        [&](const FPureMassCombatTarget& A, const FPureMassCombatTarget& B)
        {
            FVector X, Y;
            PureMassCombat::Resolve(GetWorld(), Caster, A, X);
            PureMassCombat::Resolve(GetWorld(), Caster, B, Y);
            return FVector::DistSquared(X, Hit->ImpactPoint) < FVector::DistSquared(Y, Hit->ImpactPoint);
        });
    FPureMassCombatTarget First;
    for (auto T : Candidates)
    {
        FVector P;
        if (PureMassCombat::Resolve(GetWorld(), Caster, T, P) &&
            FVector::DistSquared(Origin, P) <= FMath::Square(FMath::Clamp(CastRange, 1.f, 5000.f)) &&
            PureMassCombat::Visible(GetWorld(), Caster, Origin, T))
        {
            First = T;
            break;
        }
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(this);
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    auto* Config = NewObject<UAbilityForkedChainNodeConfig>(Graph);
    Config->MaxTargets = MaxTargets;
    Config->MaxDepth = MaxDepth;
    Config->BranchesPerTarget = BranchesPerTarget;
    Config->JumpRadius = JumpRadius;
    Config->DamageFalloff = Prototype ? Prototype->DamageFalloff : 1.f;
    Config->BeamSystem = BeamSystem;
    FAbilityGraphNode Node;
    Node.NodeId = FGuid::NewGuid();
    Node.TypeId = TEXT("Delivery.ForkedChain");
    Node.Config = Config;
    Graph->EntryNodeId = Node.NodeId;
    Graph->Nodes.Add(Node);
    FAbilityNodeInput Input;
    Input.SourceToken = ActivationSource;
    Input.Origin = Origin;
    Input.Level = GetAbilityLevel();
    Input.DamageParams = MakeDamageEffectParamsFromClassDefaults();
    if (Prototype)
    {
        Input.DamageParams.BaseDamage = Prototype->DamageAtLevel(GetAbilityLevel());
        Input.DamageParams.DamageType = Prototype->DamageType;
    }
    FAbilityTargetToken FirstToken;
    FString Error;
    TArray<FString> Errors;
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    SetOffensiveAttackTarget(First.Actor.Get());
    if (!First.IsSet() || !AbilityExecutionIdentity::CaptureTarget(ActivationSource, First, FirstToken, Error) ||
        !Graph->Validate(Registry, Errors) || !CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) ||
        !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !IsActive())
    {
        Abort();
        return;
    }
    Input.LiveTargetToken = FirstToken;
    Batch6Execution = NewObject<UAbilityGraphInstance>(this);
    auto* Execution = Batch6Execution.Get();
    const bool bStarted = Execution->StartWithInput(Graph, Input, {}, Registry, Error);
    // 单节点已同步结算，最多129条结果/完成事件；分批清空通知，不推进额外游戏时间或再次伤害。
    for (int32 Batch = 0; bStarted && !Execution->IsFinished() && Batch < 3; ++Batch)
    {
        Execution->Advance(0.f);
    }
    const bool bSucceeded =
        bStarted && Execution->IsFinished() && Execution->GetEndReason() == EAbilityGraphEndReason::Completed;
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bSucceeded);
    }
}
