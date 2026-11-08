/**
 * 分叉连锁链：GA ActivateAbility -> 鼠标目标任务 ReceiveTarget -> Commit -> 广度优先 Queue/Visited -> Actor GE 或 Mass 伤害 -> Presentation Actor。不是 Extension 子图递归。预算限制总目标/深度/兄弟数；结果段复制给客户端，表现 Actor 1.5 秒后销毁，本地组件重建时先回收旧组件。
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
    AActor* Caster = GetAvatarActorFromActorInfo();
    if (!Hit || Hit->ImpactPoint.ContainsNaN() || !Caster || !DamageEffectClass)
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
    SetOffensiveAttackTarget(First.Actor.Get());
    if (!First.IsSet() || !CanActivateAbility(CurrentSpecHandle, CurrentActorInfo) ||
        !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
    {
        Abort();
        return;
    }
    struct FPending
    {
        FPureMassCombatTarget Target;
        FVector From;
        int32 Depth;
    };
    TArray<FPending> Queue;
    Queue.Add({First, Origin, 0});
    TSet<FPureMassCombatTarget> Visited;
    Visited.Add(First);
    TArray<FBatch6ChainSegment> Segments;
    for (int32 Index = 0; Index < Queue.Num() && Index < 128; ++Index)
    {
        const auto Entry = Queue[Index];
        FVector P;
        if (!PureMassCombat::Resolve(GetWorld(), Caster, Entry.Target, P))
            continue;
        FBatch6ChainSegment S;
        S.Start = Entry.From;
        S.End = P;
        Segments.Add(S);
        auto Params = MakeDamageEffectParamsFromClassDefaults();
        if (Prototype)
        {
            Params.BaseDamage =
                Prototype->DamageAtLevel(GetAbilityLevel()) * FMath::Pow(Prototype->DamageFalloff, Entry.Depth);
            Params.DamageType = Prototype->DamageType;
        }
        Params.DebuffChance = 0;
        Params.bIsRadialDamage = false;
        if (Entry.Target.IsActor())
        {
            Params.TargetAbilitySystemComponent =
                UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Entry.Target.Actor.Get());
            if (Params.TargetAbilitySystemComponent)
                UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
        }
        else if (auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>())
        {
            FPureMassDamage D;
            D.Source = Caster;
            D.Amount = Params.BaseDamage;
            D.DamageType = Params.DamageType;
            Mass->ApplyDamage(Entry.Target.EntityId, D);
        }
        auto Next = PureMassCombat::Gather(GetWorld(), Caster, P, FMath::Clamp(JumpRadius, 1.f, 2000.f));
        int32 Siblings = 0;
        for (auto T : Next)
        {
            if (!FBatch6ChainBudget::Allows(Visited.Num(), Entry.Depth + 1, Siblings, MaxTargets, MaxDepth,
                                            BranchesPerTarget))
                break;
            FVector Q;
            if (Visited.Contains(T) || !PureMassCombat::Resolve(GetWorld(), Caster, T, Q) ||
                FVector::DistSquared(P, Q) > FMath::Square(FMath::Clamp(JumpRadius, 1.f, 2000.f)) ||
                !PureMassCombat::Visible(GetWorld(), Caster, P, T))
                continue;
            Visited.Add(T);
            Queue.Add({T, P, Entry.Depth + 1});
            ++Siblings;
        }
    }
    FActorSpawnParameters Spawn;
    Spawn.Owner = Caster;
    if (auto* Visual = GetWorld()->SpawnActor<ABatch6ChainPresentation>(Origin, FRotator::ZeroRotator, Spawn))
    {
        Visual->Segments = Segments;
        Visual->BeamSystem = BeamSystem;
        Visual->Present();
        Visual->ForceNetUpdate();
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
