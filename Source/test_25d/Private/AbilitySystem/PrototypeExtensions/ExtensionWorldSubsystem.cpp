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

namespace
{
    /** Compare captured identities without requiring a live target: a lethal threshold hit still belongs to its old life. */
    bool SameCapturedTargetLife(const UPrototypeCastContext& Previous, const UPrototypeCastContext& Current,
                                const FPureMassCombatTarget& Target)
    {
        const auto* PreviousLife = Previous.FindTargetLife(Target);
        const auto* CurrentLife = Current.FindTargetLife(Target);
        return PreviousLife && CurrentLife && PreviousLife->World == CurrentLife->World &&
               PreviousLife->Target == CurrentLife->Target && PreviousLife->ActorLease == CurrentLife->ActorLease &&
               PreviousLife->MassEntity == CurrentLife->MassEntity &&
               PreviousLife->DungeonRunId == CurrentLife->DungeonRunId;
    }

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
        if (S.Context->Caster == Caster && S.Node->EffectId == Id && S.Target == T)
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
    FExtensionStackState* S = Stacks.FindByPredicate(
        [&](const auto& V)
        {
            return V.Context && V.Node && V.Context->Caster == C->Caster && V.Node->EffectId == N->EffectId &&
                   V.Target == T && CasterAlive(V.Context) && SameCapturedTargetLife(*V.Context, *C, T);
        });
    if (S && S->bPending)
        return false;
    if (!S)
    {
        if (Stacks.Num() >= 256)
        {
            UE_LOG(LogTemp, Warning, TEXT("Extension stack capacity reached"));
            return false;
        }
        S = &Stacks.AddDefaulted_GetRef();
        S->Target = T;
        S->VisualId = NextVisualId++;
    }
    // 下一次叠层继承数值，执行所有权转给新施法。先移动旧回调，避免同步回调触及正在填写的数组元素。
    TFunction<void()> PreviousRelease = MoveTemp(S->ReleasedCallback);
    S->ChildAdmission = MoveTemp(ChildAdmission);
    S->ReleasedCallback = MoveTemp(ReleasedCallback);
    S->Context = C;
    S->Node = N;
    S->Position = P;
    S->Direction = D;
    S->Depth = Depth;
    ++S->Layers;
    S->bPending = S->Layers >= N->StackThreshold;
    S->Remaining = S->bPending ? N->DetonationDelay : N->StackLifetime;
    if (PreviousRelease)
    {
        PreviousRelease();
    }
    return true;
}
/** 服务器逐帧过期清理；先移出待引爆记录再生成子节点以避免重入失效；清理根运动/替换令牌，并更新复制表现快照。 */
void UExtensionWorldSubsystem::Tick(float Delta)
{
    if (GetWorld()->GetNetMode() == NM_Client)
        return;
    TArray<FExtensionStackState> Detonations;
    TArray<TFunction<void()>> Releases;
    for (int32 I = Stacks.Num() - 1; I >= 0; --I)
    {
        auto& S = Stacks[I];
        if (!CasterAlive(S.Context))
        {
            if (S.ReleasedCallback)
            {
                Releases.Add(MoveTemp(S.ReleasedCallback));
            }
            Stacks.RemoveAtSwap(I);
            continue;
        }
        FVector P;
        bool Alive = S.Context->ResolveTargetLife(S.Target, P);
        if (Alive)
            S.Position = P;
        S.Remaining -= Delta;
        if (S.Remaining <= 0 || !Alive)
        {
            if (S.bPending)
                Detonations.Add(S);
            else if (S.ReleasedCallback)
                Releases.Add(MoveTemp(S.ReleasedCallback));
            Stacks.RemoveAtSwap(I);
        }
    }
    // Remove records before callbacks; children may add new records.
    for (auto& S : Detonations)
    {
        S.Context->DetonatedEffects.Add(S.Node->EffectId);
        if (S.ChildAdmission)
        {
            FLegacyAbilityChildRequest Request;
            Request.Event = TEXT("OnExpire");
            Request.Config = S.Node->CompleteChild;
            Request.Context = S.Context;
            Request.Position = S.Position;
            Request.Direction = S.Direction;
            Request.EventCenter = S.Position;
            Request.Depth = S.Depth + 1;
            S.ChildAdmission(Request);
        }
        else
        {
            AExtensionRuntime::Spawn(S.Context, S.Node->CompleteChild, S.Position, S.Direction, {}, S.Depth + 1,
                                     S.Position);
        }
        if (S.ReleasedCallback)
        {
            Releases.Add(MoveTemp(S.ReleasedCallback));
        }
    }
    for (auto& Release : Releases)
    {
        Release();
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
    if (GetWorld()->GetNetMode() == NM_Client || !CasterAlive(C) || !N || !N->ValidateGraph() ||
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
    auto RetiredStacks = MoveTemp(Stacks);
    for (auto& Stack : RetiredStacks)
    {
        if (Stack.Context)
        {
            Stack.Context->bCancelled = true;
        }
        if (Stack.ReleasedCallback)
        {
            Stack.ReleasedCallback();
        }
    }
    for (auto& M : Moves)
        ReleaseOwnedRootMotion(M);
    Moves.Reset();
    Stacks.Reset();
    Replaced.Reset();
    if (IsValid(Presentation))
        Presentation->Destroy();
    Presentation = nullptr;
    Super::Deinitialize();
}
