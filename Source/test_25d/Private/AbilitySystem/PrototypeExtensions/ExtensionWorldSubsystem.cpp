/**
 * 持久效果宿主：UWorld 按 Game/PIE 生命周期创建与销毁；Runtime BeginPlay -> AddStack/RequestDisplacement。世界 Tick 管理叠层/Actor 位移，Mass 处理器 -> MoveMass 管理实体位移。子系统自身不复制，叠层显示通过 Presentation Actor 复制，Actor 位移经 CharacterMovement；Deinitialize 清理根运动源及表现。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/PrototypeExtensions/ExtensionWorldSubsystem.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionPresentation.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "Interface/CombatInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** 已移出 UPROPERTY 数组的记录仍须经得起后继/释放回调中的 GC；两类对象各自保活至全部回调结束。 */
    struct FRetiredStack
    {
        FExtensionStackState State;
        EExtensionStackReleaseReason Reason;
        TStrongObjectPtr<UPrototypeCastContext> ContextGuard;
        TStrongObjectPtr<UObject> SourceGuard;

        FRetiredStack(FExtensionStackState&& InState, EExtensionStackReleaseReason InReason)
            : State(MoveTemp(InState)), Reason(InReason), ContextGuard(State.Context.Get()),
              SourceGuard(State.RetainedSource.Get())
        {
        }
    };

    /** Match the engine removal operation on the exact owned source. This also works after source/target life expires. */
    void ReleaseOwnedRootMotion(FExtensionMoveState& Move)
    {
        if (Move.OwnedRootMotion)
        {
            Move.OwnedRootMotion->Status.SetFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);
            Move.OwnedRootMotion.Reset();
        }
    }
} // namespace

bool FExtensionStackSettings::Validate(FString& Error) const
{
    Error.Reset();
    if (EffectId.IsNone() || Threshold < 1 || Threshold > 16 || !FMath::IsFinite(Lifetime) || Lifetime < .1f ||
        Lifetime > 60.f || !FMath::IsFinite(DetonationDelay) || DetonationDelay < 0.f || DetonationDelay > 10.f)
    {
        Error = TEXT("Stack requires an effect identity, 1-16 layers, lifetime in [0.1,60] and delay in [0,10].");
    }
    return Error.IsEmpty();
}

FGuid UExtensionWorldSubsystem::AddStackShared(const FExtensionStackRequest& Request, FString& Error)
{
    Error.Reset();
    auto* Context = Request.Context;
    if (bShuttingDown || !GetWorld() || GetWorld()->GetNetMode() == NM_Client || !CasterAlive(Context) ||
        !Request.Settings.Validate(Error) || Context->DetonatedEffects.Contains(Request.Settings.EffectId) ||
        !Request.TargetLife.Target.IsSet() || Request.TargetLife.World.Get() != GetWorld() ||
        Request.TargetLife.DungeonRunId != Context->GetSourceLife().DungeonRunId || Request.Position.ContainsNaN() ||
        Request.Direction.ContainsNaN() || Request.Depth < 0 ||
        (!Request.TargetLife.Target.IsActor() && !Request.TargetLife.MassEntity.IsSet()))
    {
        if (Error.IsEmpty())
        {
            Error =
                TEXT("Stack requires a current server source, captured target life and an unused effect in this cast.");
        }
        return {};
    }
    FVector Position = Request.Position;
    FVector LivePosition;
    if (AbilityExecutionIdentity::ResolveTarget(Context->GetSourceLife(), Request.TargetLife, LivePosition))
    {
        Position = LivePosition;
    }
    else if (!Request.bConfirmedHit)
    {
        Error = TEXT("Only a confirmed server hit may add a stack after its captured target life has ended.");
        return {};
    }
    // ResolveTarget 失败会清空输出；已确认的致死命中必须保留请求中的历史位置。
    auto* State = Stacks.FindByPredicate(
        [&](const FExtensionStackState& Current)
        {
            return Current.Context && Current.Context->Caster == Context->Caster &&
                   Current.Settings.EffectId == Request.Settings.EffectId && CasterAlive(Current.Context) &&
                   AbilityBehaviorPrimitives::SameTargetLife(Current.TargetLife, Request.TargetLife);
        });
    if (State && State->bPending)
    {
        Error = TEXT("Threshold is already pending for this effect and captured target life.");
        return {};
    }
    if (!State)
    {
        if (Stacks.Num() >= 256)
        {
            Error = TEXT("Extension stack capacity reached.");
            return {};
        }
        State = &Stacks.AddDefaulted_GetRef();
        State->VisualId = NextVisualId++;
    }
    TStrongObjectPtr<UPrototypeCastContext> PreviousContext(State->Context.Get());
    TStrongObjectPtr<UObject> PreviousSource(State->RetainedSource.Get());
    auto PreviousRelease = MoveTemp(State->OnReleased);
    const FGuid Lease = FGuid::NewGuid();
    State->Context = Context;
    State->RetainedSource = Request.RetainedSource;
    State->Settings = Request.Settings;
    State->TargetLife = Request.TargetLife;
    State->Lease = Lease;
    State->Position = Position;
    State->Direction = Request.Direction;
    State->Depth = Request.Depth;
    State->OnThreshold = Request.OnThreshold;
    State->OnReleased = Request.OnReleased;
    State->CancellationVersion = CancellationVersions.FindRef(Context->Caster);
    ++State->Layers;
    State->bPending = State->Layers >= Request.Settings.Threshold;
    State->Remaining = State->bPending ? Request.Settings.DetonationDelay : Request.Settings.Lifetime;
    // 最新数值/Context 已全部提交后才释放旧等待者；回调可重入，返回时不能再次访问数组内指针。
    if (PreviousRelease)
    {
        PreviousRelease(EExtensionStackReleaseReason::Replaced);
    }
    return Lease;
}

bool UExtensionWorldSubsystem::CancelStack(FGuid Lease)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Lease.IsValid())
    {
        return false;
    }
    const int32 Index = Stacks.IndexOfByPredicate(
        [Lease](const FExtensionStackState& State)
        {
            return State.Lease == Lease;
        });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    FRetiredStack Retired(MoveTemp(Stacks[Index]), EExtensionStackReleaseReason::Cancelled);
    Stacks.RemoveAtSwap(Index);
    if (Retired.State.OnReleased)
    {
        Retired.State.OnReleased(Retired.Reason);
    }
    return true;
}

bool UExtensionWorldSubsystem::IsDisplacementActive(FGuid Token) const
{
    return Token.IsValid() && Moves.ContainsByPredicate(
                                  [Token](const FExtensionMoveState& Move)
                                  {
                                      return Move.Token == Token && Move.Remaining > 0.f;
                                  });
}

bool UExtensionWorldSubsystem::CancelDisplacement(FGuid Token)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Token.IsValid())
    {
        return false;
    }
    const int32 Index = Moves.IndexOfByPredicate(
        [Token](const FExtensionMoveState& Move)
        {
            return Move.Token == Token;
        });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    auto Retired = MoveTemp(Moves[Index]);
    Moves.RemoveAtSwap(Index);
    Replaced.Add(Token, GetWorld()->GetTimeSeconds() + 60.f);
    ReleaseOwnedRootMotion(Retired);
    return true;
}

/** 引擎子系统创建筛选：只支持 Game/PIE，避免编辑器预览世界维护战斗状态。 */
bool UExtensionWorldSubsystem::DoesSupportWorldType(EWorldType::Type T) const
{
    return T == EWorldType::Game || T == EWorldType::PIE;
}
/** 服务器主动清理入口：标记此施法者的持久上下文取消，结束其 Runtime，再 Tick(0) 立即处理无效记录。 */
void UExtensionWorldSubsystem::CancelCasterEffects(AActor* Caster)
{
    if (!Caster || GetWorld()->GetNetMode() == NM_Client)
        return;
    ++CancellationVersions.FindOrAdd(Caster);
    for (auto& S : Stacks)
        if (S.Context && S.Context->Caster == Caster)
            S.Context->bCancelled = true;
    for (auto& M : Moves)
        if (M.Context && M.Context->Caster == Caster)
            M.Context->bCancelled = true;
    for (TActorIterator<AExtensionRuntime> It(GetWorld()); It; ++It)
        if (It->Context && It->Context->Caster == Caster)
        {
            It->Context->bCancelled = true;
            It->Finish(true);
        }
    Tick(0.f);
}
/** 为 Tickable 子系统提供性能统计标识，不推进游戏逻辑。 */
TStatId UExtensionWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(ExtensionWorld, STATGROUP_Tickables);
}
/** Runtime/子系统共用守卫：检查 Context 未取消、弱引用有效、Actor 未销毁且战斗接口未死亡。 */
bool UExtensionWorldSubsystem::CasterAlive(UPrototypeCastContext* C)
{
    FString Error;
    return C && !C->bCancelled && C->CaptureSourceLife(Error);
}
/** 按施法者、EffectId、混合目标查当前层数；没有匹配记录返回零。 */
int32 UExtensionWorldSubsystem::LayersFor(AActor* Caster, FName Id, FPureMassCombatTarget T) const
{
    for (const auto& S : Stacks)
        if (S.Context && S.Context->Caster == Caster && S.Settings.EffectId == Id && S.TargetLife.Target == T)
            return S.Layers;
    return 0;
}
/** Runtime Stack 分支入口：校验图与确认命中快照，按来源/效果/目标合并层数；到阈值进入待引爆期，最多保留 256 条。 */
bool UExtensionWorldSubsystem::AddStack(UPrototypeCastContext* C, UExtensionNode* N, FPureMassCombatTarget T, FVector P,
                                        FVector D, int32 Depth, bool ConfirmedHit,
                                        FLegacyAbilityChildAdmission ChildAdmission, TFunction<void()> ReleasedCallback)
{
    if (GetWorld()->GetNetMode() == NM_Client || !CasterAlive(C) || !N || !N->ValidateGraph() ||
        C->DetonatedEffects.Contains(N->EffectId) || !T.IsSet() || P.ContainsNaN())
        return false;
    FVector LivePosition;
    if (C->ResolveTargetLife(T, LivePosition))
        P = LivePosition;
    else if (!ConfirmedHit)
        return false; // Only a server-confirmed hit can carry a lethal snapshot.
    const auto* TargetLife = C->FindTargetLife(T);
    if (!TargetLife)
    {
        return false;
    }
    FExtensionStackRequest Request;
    Request.Context = C;
    Request.RetainedSource = N;
    Request.Settings.EffectId = N->EffectId;
    Request.Settings.Threshold = N->StackThreshold;
    Request.Settings.Lifetime = N->StackLifetime;
    Request.Settings.DetonationDelay = N->DetonationDelay;
    Request.TargetLife = *TargetLife;
    Request.Position = P;
    Request.Direction = D;
    Request.Depth = Depth;
    Request.bConfirmedHit = ConfirmedHit;
    Request.OnThreshold = [N, Admission = MoveTemp(ChildAdmission)](const FExtensionStackTrigger& Trigger)
    {
        if (Admission)
        {
            FLegacyAbilityChildRequest Child;
            Child.Event = TEXT("OnExpire");
            Child.Config = N->CompleteChild;
            Child.Context = Trigger.Context;
            Child.Position = Trigger.Position;
            Child.Direction = Trigger.Direction;
            Child.EventCenter = Trigger.Position;
            Child.Depth = Trigger.Depth + 1;
            Admission(Child);
        }
        else
        {
            AExtensionRuntime::Spawn(Trigger.Context, N->CompleteChild, Trigger.Position, Trigger.Direction, {},
                                     Trigger.Depth + 1, Trigger.Position);
        }
    };
    Request.OnReleased = [Release = MoveTemp(ReleasedCallback)](EExtensionStackReleaseReason)
    {
        if (Release)
        {
            Release();
        }
    };
    FString Error;
    return AddStackShared(Request, Error).IsValid();
}
/** 服务器逐帧过期清理；先移出待引爆记录再生成子节点以避免重入失效；清理根运动/替换令牌，并更新复制表现快照。 */
void UExtensionWorldSubsystem::Tick(float Delta)
{
    if (bShuttingDown || GetWorld()->GetNetMode() == NM_Client || !FMath::IsFinite(Delta) || Delta < 0.f)
        return;
    TArray<FRetiredStack> RetiredStacks;
    for (int32 I = Stacks.Num() - 1; I >= 0; --I)
    {
        auto& S = Stacks[I];
        if (!CasterAlive(S.Context))
        {
            const auto Reason = S.Context && S.Context->bCancelled ? EExtensionStackReleaseReason::Cancelled
                                                                   : EExtensionStackReleaseReason::SourceInvalid;
            RetiredStacks.Emplace(MoveTemp(S), Reason);
            Stacks.RemoveAtSwap(I);
            continue;
        }
        FVector P;
        const bool Alive = AbilityExecutionIdentity::ResolveTarget(S.Context->GetSourceLife(), S.TargetLife, P);
        if (Alive)
            S.Position = P;
        S.Remaining -= Delta;
        if (S.Remaining <= 0 || !Alive)
        {
            const auto Reason = S.bPending ? EExtensionStackReleaseReason::Triggered
                                : Alive    ? EExtensionStackReleaseReason::Expired
                                           : EExtensionStackReleaseReason::TargetLost;
            RetiredStacks.Emplace(MoveTemp(S), Reason);
            Stacks.RemoveAtSwap(I);
        }
    }
    // 所有记录先离开共享数组，并各自强保活，再发布阈值；子图可以重入叠层，也可以触发 GC。
    for (auto& Retired : RetiredStacks)
    {
        auto& S = Retired.State;
        if (Retired.Reason != EExtensionStackReleaseReason::Triggered)
        {
            continue;
        }
        if (bShuttingDown || !GetWorld() || GetWorld()->bIsTearingDown)
        {
            Retired.Reason = EExtensionStackReleaseReason::WorldEnded;
            continue;
        }
        if (S.Context->bCancelled || S.CancellationVersion != CancellationVersions.FindRef(S.Context->Caster))
        {
            Retired.Reason = EExtensionStackReleaseReason::Cancelled;
            continue;
        }
        if (!CasterAlive(S.Context))
        {
            Retired.Reason = EExtensionStackReleaseReason::SourceInvalid;
            continue;
        }
        S.Context->DetonatedEffects.Add(S.Settings.EffectId);
        if (S.OnThreshold)
        {
            FExtensionStackTrigger Trigger;
            Trigger.Lease = S.Lease;
            Trigger.Source = S.Context->GetSourceLife();
            Trigger.TargetLife = S.TargetLife;
            Trigger.Position = S.Position;
            Trigger.Direction = S.Direction;
            Trigger.Context = S.Context;
            Trigger.RetainedSource = S.RetainedSource;
            Trigger.Depth = S.Depth;
            S.OnThreshold(Trigger);
        }
    }
    // 保留旧流程“全部阈值先于等待者释放”的顺序；取消的记录不会伪装为正常阈值。
    for (auto& Retired : RetiredStacks)
    {
        if (Retired.State.OnReleased)
        {
            Retired.State.OnReleased(Retired.Reason);
        }
    }
    for (int32 I = Moves.Num() - 1; I >= 0; --I)
    {
        auto& M = Moves[I];
        FVector P;
        bool Valid = CasterAlive(M.Context) && M.Context->ResolveTargetLife(M.Target, P);
        if (M.Target.IsActor())
            M.Remaining -= Delta;
        if (!Valid || M.Remaining <= 0)
        {
            if (!Valid)
                Replaced.Add(M.Token, GetWorld()->GetTimeSeconds() + 60);
            ReleaseOwnedRootMotion(M);
            Moves.RemoveAtSwap(I);
        }
    }
    for (auto It = Replaced.CreateIterator(); It; ++It)
        if (It.Value() < GetWorld()->GetTimeSeconds())
            It.RemoveCurrent();
    for (auto It = CancellationVersions.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    if (!Stacks.IsEmpty() && !IsValid(Presentation))
        Presentation = GetWorld()->SpawnActor<AExtensionPresentation>();
    if (Presentation)
    {
        Presentation->Marks.Reset();
        for (const auto& S : Stacks)
        {
            auto& V = Presentation->Marks.AddDefaulted_GetRef();
            V.Id = S.VisualId;
            V.Position = S.Position + FVector(0, 0, 95);
            V.Layers = S.Layers;
            V.bPending = S.bPending;
        }
        Presentation->Refresh();
    }
}
/** Actor/Mass 位移共用几何约束：胶囊扫掠、落地坡度/高度和可用导航投影；失败保留起点，不穿墙或越悬崖。 */
bool UExtensionWorldSubsystem::ConstrainMove(UWorld* W, AActor* Ignore, FVector Start, FVector Desired, float Radius,
                                             float HalfHeight, FVector& Result)
{
    Result = Start;
    if (!W || Start.ContainsNaN() || Desired.ContainsNaN())
        return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ExtensionDisplacement), false, Ignore);
    FHitResult Wall;
    FVector End = Desired;
    // Keep the full horizontal radius while allowing a 1 cm floor-contact tolerance.
    // Ground support and height are independently checked below using the full height.
    if (W->SweepSingleByObjectType(Wall, Start, End, FQuat::Identity, Objects,
                                   FCollisionShape::MakeCapsule(Radius, FMath::Max(Radius, HalfHeight - 1.f)), Params))
    {
        if (Wall.bStartPenetrating)
            return false;
        End = FMath::Lerp(Start, End, FMath::Max(0.f, Wall.Time - .01f));
    }
    FHitResult Floor;
    FVector Foot = End - FVector(0, 0, HalfHeight);
    if (!W->LineTraceSingleByObjectType(Floor, Foot + FVector(0, 0, 60), Foot - FVector(0, 0, 80), Objects, Params) ||
        Floor.ImpactNormal.Z < .65f || FMath::Abs(Floor.ImpactPoint.Z - (Start.Z - HalfHeight)) > 60)
        return false;
    End.Z = Floor.ImpactPoint.Z + HalfHeight + 1;
    if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
        Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
    {
        FNavLocation NavPoint;
        if (!Nav->ProjectPointToNavigation(Floor.ImpactPoint, NavPoint, FVector(40, 40, 70)) ||
            FVector::DistSquared2D(NavPoint.Location, Floor.ImpactPoint) > FMath::Square(40.f))
            return false;
    }
    Result = End;
    return true;
}
/** Runtime Displace 分支入口：服务器校验免疫和位移比例、约束终点；替换旧令牌。Actor 添加 RootMotionSource，Mass 保存目标供处理器推进。 */
FGuid UExtensionWorldSubsystem::RequestDisplacement(UPrototypeCastContext* C, UExtensionNode* N,
                                                    FPureMassCombatTarget T, FVector Center, FVector Direction)
{
    FVector P;
    if (bShuttingDown || GetWorld()->GetNetMode() == NM_Client || !CasterAlive(C) || !N || !N->ValidateGraph() ||
        !C->ResolveTargetLife(T, P))
        return {};
    float Radius = 20, HalfHeight = 40, Scale = 1;
    ACharacter* Character = nullptr;
    if (T.IsActor())
    {
        Character = Cast<ACharacter>(T.Actor.Get());
        if (!Character || Character->ActorHasTag(TEXT("Player")))
            return {};
        for (auto Cls : N->ImmuneActorClasses)
            if (Cls && Character->IsA(Cls))
                return {};
        for (auto Tag : N->ImmuneActorTags)
            if (Character->ActorHasTag(Tag))
                return {};
        for (auto& Pair : N->ActorTagMovementScale)
            if (Character->ActorHasTag(Pair.Key))
                Scale = FMath::Min(Scale, Pair.Value);
        Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
        HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    else
    {
        FPureMassView V;
        if (!GetWorld()->GetSubsystem<UPureMassSubsystem>()->Resolve(T.EntityId, V) || !V.Type ||
            N->ImmuneMassTypes.Contains(V.Type))
            return {};
        Radius = V.Type->Radius;
        HalfHeight = V.Type->HalfHeight;
    }
    FVector Offset = (P - Center);
    Offset.Z = 0;
    float Distance = N->MoveDistance * Scale;
    if (N->bPull)
        Distance = FMath::Min(Distance, FMath::Max(0.f, Offset.Size() - N->SafeDistance));
    FVector Dir = Offset.GetSafeNormal(SMALL_NUMBER, Direction.GetSafeNormal2D(SMALL_NUMBER, FVector::ForwardVector));
    if (N->bPull)
        Dir = -Dir;
    FVector End;
    if (Distance <= 0 || !ConstrainMove(GetWorld(), Character, P, P + Dir * Distance, Radius, HalfHeight, End))
        return {};
    for (int32 I = Moves.Num() - 1; I >= 0; --I)
        if (Moves[I].Target == T)
        {
            Replaced.Add(Moves[I].Token, GetWorld()->GetTimeSeconds() + 60);
            ReleaseOwnedRootMotion(Moves[I]);
            Moves.RemoveAtSwap(I);
        }
    if (Moves.Num() >= 256)
    {
        UE_LOG(LogTemp, Warning, TEXT("Extension displacement capacity reached"));
        return {};
    }
    auto& M = Moves.AddDefaulted_GetRef();
    M.Context = C;
    M.Node = N;
    M.Target = T;
    M.Token = FGuid::NewGuid();
    M.Destination = End;
    M.Remaining = N->Duration;
    if (Character)
    {
        // Anim montage root motion outranks root-motion sources in CharacterMovement.
        // Forced displacement interrupts it through GAS so simulated clients stop it too.
        if (Character->IsPlayingRootMotion())
        {
            auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Character);
            if (ASC && ASC->GetCurrentMontage())
                ASC->CurrentMontageStop(0.f);
            else
                Character->StopAnimMontage();
        }
        auto Move = MakeShared<FRootMotionSource_MoveToForce>();
        Move->InstanceName = TEXT("ExtensionDisplace");
        Move->Priority = 1000;
        Move->AccumulateMode = ERootMotionAccumulateMode::Override;
        Move->StartLocation = P;
        Move->TargetLocation = End;
        Move->Duration = N->Duration;
        Move->bRestrictSpeedToExpected = true;
        Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
        Move->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
        M.RootMotionId = Character->GetCharacterMovement()->ApplyRootMotionSource(Move);
        M.OwnedRootMotion = Move;
    }
    return M.Token;
}
/** Mass 处理器逐实体调用：插值到终点并再次约束碰撞，更新 Remaining；清空旧路径并短暂占用移动，避免同帧导航覆盖。 */
void UExtensionWorldSubsystem::MoveMass(FPureMassFragment& F, float Delta)
{
    auto* M = Moves.FindByPredicate(
        [&](const auto& V)
        {
            return !V.Target.IsActor() && V.Target.EntityId == F.Id;
        });
    if (!M)
        return;
    FVector CurrentTargetPosition;
    if (!CasterAlive(M->Context) || !M->Context->ResolveTargetLife(M->Target, CurrentTargetPosition))
    {
        // Processor callbacks can run before subsystem Tick. Close the old command without touching its fragment.
        M->Remaining = 0.f;
        Replaced.Add(M->Token, GetWorld()->GetTimeSeconds() + 60.f);
        return;
    }
    if (M->Remaining <= 0 || F.Health <= 0 || !F.Type.IsValid())
        return;
    FVector End;
    const FVector Desired = FMath::Lerp(F.Location, M->Destination, FMath::Clamp(Delta / M->Remaining, 0.f, 1.f));
    if (ConstrainMove(GetWorld(), nullptr, F.Location, Desired, F.Type->Radius, F.Type->HalfHeight, End))
        F.Location = End;
    else
        M->Remaining = 0;
    M->Remaining -= Delta;
    F.Path.Reset();
    F.PathPoint = 0;
    F.NextPathTime = 0;
    // Existing navigation observes this for this frame; preserve longer genuine roots.
    F.RootRemaining = FMath::Max(F.RootRemaining, Delta + .001f);
}
/** 世界退出：移除所有记录的 Actor RootMotionSource，清空叠层/移动/令牌并销毁 Presentation，最后调用父类。 */
void UExtensionWorldSubsystem::Deinitialize()
{
    bShuttingDown = true;
    TArray<FRetiredStack> RetiredStacks;
    for (auto& Stack : Stacks)
    {
        if (Stack.Context)
        {
            Stack.Context->bCancelled = true;
        }
        RetiredStacks.Emplace(MoveTemp(Stack), EExtensionStackReleaseReason::WorldEnded);
    }
    Stacks.Reset();
    for (auto& Stack : RetiredStacks)
    {
        if (Stack.State.OnReleased)
        {
            Stack.State.OnReleased(Stack.Reason);
        }
    }
    for (auto& M : Moves)
        ReleaseOwnedRootMotion(M);
    Moves.Reset();
    Stacks.Reset();
    Replaced.Reset();
    CancellationVersions.Reset();
    if (IsValid(Presentation))
        Presentation->Destroy();
    Presentation = nullptr;
    Super::Deinitialize();
}
