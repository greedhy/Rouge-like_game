/** 调用链：GA 选点与 Commit -> RunGraph Task -> 叉状或逐跳策略 -> 共享伤害与复制光束。
 *  源技能结束时 GAS 销毁任务并取消剩余跳跃，已出现的光束由其表现 Actor 到期清理。
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
#include "AbilitySystem/Unified/AbilityTask_RunGraph.h"

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
        {
            if (auto* V = UNiagaraFunctionLibrary::SpawnSystemAttached(BeamSystem, GetRootComponent(), NAME_None,
                                                                       S.Start, FRotator::ZeroRotator,
                                                                       EAttachLocation::KeepWorldPosition, true, false))
            {
                // 先写入世界坐标，再激活 Niagara，避免首帧使用素材内置端点。
                V->SetVariableVec3(TEXT("User.Start"), S.Start);
                V->SetVariableVec3(TEXT("User.End"), S.End);
                V->SetVariableVec3(TEXT("User.BeamEnd"), S.End);
                V->SetVariableVec3(TEXT("BeamEnd"), S.End);
                V->SetVariableVec3(TEXT("Beam Start"), S.Start);
                V->SetVariableVec3(TEXT("Beam End"), S.End);
                PresentationComponents.Add(V);
                V->Activate(true);
            }
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
/** 展示两种传播方式，沿用原技能的伤害、费用及冷却参数。 */
FString UBatch6ChainAbility::GetDescription(int32 Level)
{
    if (!Prototype)
    {
        return Super::GetDescription(Level);
    }
    const FString Behavior = bSequentialChain ? FString::Printf(TEXT("每 %.2f 秒传导一次"), JumpDelay)
                                              : FString(TEXT("同时击中主目标及附近敌人"));
    return FString::Printf(
        TEXT("<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>%.0f 伤害；%s，最多 %d 个目标。</>"),
        *Prototype->DisplayName.ToString(), Level, Prototype->ManaCost, Prototype->Cooldown,
        Prototype->DamageAtLevel(Level), *Behavior, MaxTargets);
}
/** 无论成功取消都清除本技能目标超时，再交父类清理任务。 */
void UBatch6ChainAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                     FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled)
{
    CloseExecutionSession(Cancelled);
    ActivationSource = {};
    if (GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(Timeout);
    Super::EndAbility(Handle, Info, Activation, Replicate, Cancelled);
}
/** 鼠标目标回调在服务器选出首个有效敌人，验证图并提交一次费用。 */
void UBatch6ChainAbility::ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bResolved || !CurrentActorInfo)
    {
        return;
    }
    bResolved = true;
    GetWorld()->GetTimerManager().ClearTimer(Timeout);
    if (!CurrentActorInfo->IsNetAuthority())
    {
        // 本地选点已提交，GA 的结束由权威端复制，不能用选点超时中止正在传导的图。
        return;
    }
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
    auto* Graph = GetRuntimeRecipeSnapshot().Graph.Get();
    if (!Graph)
    {
        // 正式零修正入口保留原兼容策略；预览/动态修改必须使用激活前已冻结的同一配方。
        Graph = NewObject<UAbilityGraphDefinition>(this);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        UAbilityLightningNodeConfig* Config = nullptr;
        if (bSequentialChain)
        {
            auto* Sequential = NewObject<UAbilitySequentialChainNodeConfig>(Graph);
            Sequential->JumpDelay = JumpDelay;
            Config = Sequential;
        }
        else
        {
            Config = NewObject<UAbilityForkedVolleyNodeConfig>(Graph);
        }
        Config->MaxTargets = MaxTargets;
        Config->JumpRadius = JumpRadius;
        Config->DamageFalloff = Prototype ? Prototype->DamageFalloff : 1.f;
        Config->BeamSystem = BeamSystem;
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = bSequentialChain ? TEXT("Delivery.SequentialChain") : TEXT("Delivery.ForkedVolley");
        Node.Config = Config;
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Add(Node);
    }
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
    FAbilityExecutionOptions Options;
    Options.CompatibilityProfile = Graph->CompatibilityProfile;
    Options.BudgetPolicy = Graph->BudgetPolicy;
    // 不论运行时选择哪种控制，均从本次任务启动时计时，不能把施法前整帧Delta灌入新步骤。
    auto* Task = UAbilityTask_RunGraph::RunAbilityGraphWithInput(this, Graph, Input, Options, true);
    Task->OnFinished.AddDynamic(this, &ThisClass::GraphFinished);
    Task->ReadyForActivation();
}

/** 图正常完成或失效时结束这次施法，显式取消仍由 GAS 销毁任务。 */
void UBatch6ChainAbility::GraphFinished(bool bCancelled)
{
    if (IsActive())
    {
        EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
    }
}
