// 调用链：图策略Start -> 原混合查询/生命令牌 -> DamageService或StatusService -> 原复制表现/状态持有者。
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"

namespace
{
    /** 单次命中结果采用历史位置和原目标令牌，供图后继重新解析。 */
    bool PublishLightningHit(UAbilityGraphInstance& Owner, const FGuid& Id, const FAbilityNodeInput& Input,
                             const FAbilityTargetToken& Target, FVector From, FVector Position,
                             const FAbilityDamageResult& Result)
    {
        FAbilityNodeInput Payload = Input;
        FAbilityImpactSnapshot Impact;
        Impact.Location = Position;
        Impact.Direction = (Position - From).GetSafeNormal();
        Impact.ConfirmedTarget = Target;
        Payload.Impact = Impact;
        Payload.Origin = Position;
        Payload.LiveTargetToken.Reset();
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.bAttempted;
        Payload.bKilled = Result.bKilled;
        Payload.DamageApplied = Result.Applied;
        Payload.ActualDamage = Result.ActualDamage;
        return Owner.Publish(Id, TEXT("DamageResolved"), Payload);
    }

    /** 将同一时刻产生的线段一次复制，逐跳时每次只发送该段。 */
    void PresentLightning(AActor& Caster, FVector Origin, UNiagaraSystem* Beam,
                          TArray<FBatch6ChainSegment>&& Segments, float Lifetime = 1.5f)
    {
        if (Segments.IsEmpty())
        {
            return;
        }
        FActorSpawnParameters Spawn;
        Spawn.Owner = &Caster;
        if (auto* Visual = Caster.GetWorld()->SpawnActor<ABatch6ChainPresentation>(
                Origin, FRotator::ZeroRotator, Spawn))
        {
            Visual->Segments = MoveTemp(Segments);
            Visual->BeamSystem = Beam;
            Visual->SetLifeSpan(Lifetime);
            Visual->Present();
            Visual->ForceNetUpdate();
        }
    }
} // namespace

bool UAbilityForkedChainNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(JumpRadius) || !FMath::IsFinite(DamageFalloff) || DamageFalloff < 0.f ||
        !FMath::IsFinite(FMath::Pow(DamageFalloff, FMath::Clamp(MaxDepth, 0, 16))))
    {
        Error = TEXT("Forked chain requires finite jump radius and finite nonnegative attenuation.");
    }
    return Error.IsEmpty();
}
bool UAbilityAreaPetrifyNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Radius) || Radius < 0.f || !FMath::IsFinite(Duration) || Duration <= 0.f)
    {
        Error = TEXT("Area petrify requires finite nonnegative radius and positive duration.");
    }
    return Error.IsEmpty();
}
bool UAbilityForkedChainNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Forked chain requires an initial live target.");
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityForkedChainNodeConfig>(&Config);
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        Error = TEXT("Forked chain source is invalid.");
        return false;
    }
    struct FPending
    {
        FAbilityTargetToken Target;
        FVector From;
        int32 Depth;
    };
    TArray<FPending> Queue;
    Queue.Add({Input.LiveTargetToken.GetValue(), Input.Origin, 0});
    TSet<FPureMassCombatTarget> Visited;
    Visited.Add(Input.LiveTargetToken->Target);
    TArray<FBatch6ChainSegment> Segments;
    const float Radius = FMath::Clamp(Settings.JumpRadius, 1.f, 2000.f);
    for (int32 Index = 0; Index < Queue.Num() && Index < 128; ++Index)
    {
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
        {
            return true;
        }
        const auto Entry = Queue[Index];
        FVector Position;
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Entry.Target, Position))
        {
            continue;
        }
        FBatch6ChainSegment Segment;
        Segment.Start = Entry.From;
        Segment.End = Position;
        Segments.Add(Segment);
        auto Params = Input.DamageParams;
        Params.DebuffChance = 0.f;
        Params.bIsRadialDamage = false;
        const auto Result = AbilityDamageService::Apply(Input.SourceToken, Entry.Target, Params,
                                                        FMath::Pow(Settings.DamageFalloff, Entry.Depth));
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
        {
            return true;
        }
        if (!PublishLightningHit(Owner, Id, Input, Entry.Target, Entry.From, Position, Result))
        {
            return true;
        }
        // 保持原查询顺序与先伤害后扩展的BFS语义；致死位置仍可继续分叉，但不得复活原目标令牌。
        const auto Next = PureMassCombat::Gather(Input.SourceToken.World.Get(), Caster, Position, Radius);
        int32 Siblings = 0;
        for (const auto& Candidate : Next)
        {
            if (!FBatch6ChainBudget::Allows(Visited.Num(), Entry.Depth + 1, Siblings, Settings.MaxTargets,
                                            Settings.MaxDepth, Settings.BranchesPerTarget))
            {
                break;
            }
            FVector Location;
            FAbilityTargetToken Token;
            FString CaptureError;
            if (Visited.Contains(Candidate) ||
                !PureMassCombat::Resolve(Input.SourceToken.World.Get(), Caster, Candidate, Location) ||
                FVector::DistSquared(Position, Location) > FMath::Square(Radius) ||
                !PureMassCombat::Visible(Input.SourceToken.World.Get(), Caster, Position, Candidate) ||
                !AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Candidate, Token, CaptureError))
            {
                continue;
            }
            Visited.Add(Candidate);
            Queue.Add({Token, Position, Entry.Depth + 1});
            ++Siblings;
        }
    }
    if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return true;
    }
    PresentLightning(*Caster, Input.Origin, Settings.BeamSystem, MoveTemp(Segments));
    return Owner.Publish(Id, TEXT("Completed"), Input);
}
/** 校验目标预算、查询半径和伤害倍率，避免无界分支或非有限数值。 */
bool UAbilityLightningNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (MaxTargets < 1 || MaxTargets > 128 || !FMath::IsFinite(JumpRadius) || JumpRadius <= 0.f ||
        JumpRadius > 2000.f || !FMath::IsFinite(DamageFalloff) || DamageFalloff < 0.f || DamageFalloff > 1.f)
    {
        Error = TEXT("Lightning requires 1-128 targets, radius in (0, 2000], and falloff in [0, 1].");
    }
    return Error.IsEmpty();
}

/** 保证每跳有正间隔，并继承通用闪电参数校验。 */
bool UAbilitySequentialChainNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    if (!FMath::IsFinite(JumpDelay) || JumpDelay < 0.01f || JumpDelay > 1.f)
    {
        Error = TEXT("Sequential chain jump delay must be between 0.01 and 1 second.");
    }
    return Error.IsEmpty();
}

/** 先按中心距离排序，再捕获符合光束起点遮挡规则的原生命。 */
TArray<FAbilityTargetToken> UAbilityLightningNodeExecution::FindTargets(AActor& Caster, FVector Center, FVector From,
                                                                       int32 Limit) const
{
    struct FCandidate
    {
        FPureMassCombatTarget Target;
        double DistanceSquared = 0.;
    };
    UWorld* World = Caster.GetWorld();
    TArray<FCandidate> Candidates;
    for (const auto& Target : PureMassCombat::Gather(World, &Caster, Center, Settings->JumpRadius))
    {
        FVector Position;
        if (!Visited.Contains(Target) && PureMassCombat::Resolve(World, &Caster, Target, Position))
        {
            Candidates.Add({Target, FVector::DistSquared(Center, Position)});
        }
    }
    Candidates.StableSort(
        [](const FCandidate& A, const FCandidate& B)
        {
            return A.DistanceSquared < B.DistanceSquared;
        });
    TArray<FAbilityTargetToken> Targets;
    for (const auto& Candidate : Candidates)
    {
        if (Targets.Num() >= Limit)
        {
            break;
        }
        FAbilityTargetToken Token;
        FString Error;
        if (PureMassCombat::Visible(World, &Caster, From, Candidate.Target) &&
            AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Candidate.Target, Token, Error))
        {
            Targets.Add(Token);
        }
    }
    return Targets;
}

/** 重验已捕获的目标；伤害可同步结束施法，之后必须重新检查执行状态。 */
bool UAbilityLightningNodeExecution::Hit(const FAbilityTargetToken& Target, FVector From, float Scale,
                                        TArray<FBatch6ChainSegment>& Segments)
{
    auto* Owner = GraphOwner.Get();
    FVector Position;
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return false;
    }
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Position) ||
        Visited.Contains(Target.Target))
    {
        return true;
    }
    Visited.Add(Target.Target);
    LastImpact = Position;
    auto Params = Input.DamageParams;
    Params.DebuffChance = 0.f;
    Params.bIsRadialDamage = false;
    const auto Result = AbilityDamageService::Apply(Input.SourceToken, Target, Params, Scale);
    AActor* Caster = nullptr;
    if (bStopped || Owner->IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    Segments.Add({From, Position});
    return PublishLightningHit(*Owner, InvocationId, Input, Target, From, Position, Result);
}

bool UAbilityLightningNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityLightningNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    AActor* Caster = nullptr;
    FVector FirstPosition;
    if (!Input.LiveTargetToken.IsSet() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster) ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), FirstPosition))
    {
        Error = TEXT("Lightning requires a live source and initial target.");
        return false;
    }
    const auto First = Input.LiveTargetToken.GetValue();
    const FVector From = Caster->GetActorLocation();
    const auto* Sequential = Cast<UAbilitySequentialChainNodeConfig>(Settings);
    TArray<FAbilityTargetToken> Others;
    if (!Sequential && Settings->MaxTargets > 1)
    {
        // 固定全部次目标再结算首击，避免击杀/复用回调改变叉状闪电的选取范围。
        Visited.Add(First.Target);
        Others = FindTargets(*Caster, FirstPosition, From, Settings->MaxTargets - 1);
        Visited.Reset();
    }
    TArray<FBatch6ChainSegment> Segments;
    if (!Hit(First, From, 1.f, Segments))
    {
        return true;
    }
    for (const auto& Target : Others)
    {
        if (!Hit(Target, From, Settings->DamageFalloff, Segments))
        {
            return true;
        }
    }
    PresentLightning(*Caster, From, Settings->BeamSystem, MoveTemp(Segments), Sequential ? 0.35f : 1.5f);
    if (Sequential && Visited.Num() < Settings->MaxTargets)
    {
        TimeUntilJump = Sequential->JumpDelay;
        return true;
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void UAbilityLightningNodeExecution::Advance(float DeltaSeconds)
{
    const auto* Sequential = Cast<UAbilitySequentialChainNodeConfig>(Settings);
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Sequential || !Owner || Owner->IsFinished() || DeltaSeconds <= 0.f)
    {
        return;
    }
    TimeUntilJump -= DeltaSeconds;
    if (TimeUntilJump > UE_SMALL_NUMBER)
    {
        return;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    const FVector From = LastImpact;
    const auto Targets = FindTargets(*Caster, From, From, 1);
    if (Targets.IsEmpty())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
        return;
    }
    TArray<FBatch6ChainSegment> Segments;
    if (!Hit(Targets[0], From, FMath::Pow(Settings->DamageFalloff, Visited.Num()), Segments))
    {
        return;
    }
    PresentLightning(*Caster, From, Settings->BeamSystem, MoveTemp(Segments), 0.35f);
    if (Visited.Num() >= Settings->MaxTargets)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
        return;
    }
    // 不补发跨过的时间，长帧结束后仍要等待完整一跳间隔。
    TimeUntilJump = Sequential->JumpDelay;
}

bool UAbilityAreaPetrifyNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityAreaPetrifyNodeConfig>(&Config);
    AActor* Source = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
    {
        Error = TEXT("Area petrify source is invalid.");
        return false;
    }
    const auto Targets = PureMassCombat::Gather(Input.SourceToken.World.Get(), Source, Input.Origin, Settings.Radius);
    TArray<FAbilityTargetToken> CapturedTargets;
    for (const auto& Target : Targets)
    {
        if ((Target.IsActor() && !Cast<AEnemyCharacter>(Target.Actor.Get())) ||
            !PureMassCombat::Visible(Input.SourceToken.World.Get(), Source, Input.Origin, Target))
        {
            continue;
        }
        FAbilityTargetToken Token;
        FString CaptureError;
        if (AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Target, Token, CaptureError))
        {
            CapturedTargets.Add(Token);
        }
    }
    // 首个状态可同步取消其它GA或触发生命变化；先固定整次查询的身份，逐目标重验后才施加。
    for (const auto& Target : CapturedTargets)
    {
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
        {
            return true;
        }
        FVector Location;
        if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Location))
        {
            AbilityStatusService::ApplyPetrify(Source, Target.Target, Settings.Duration, Settings.StoneMaterial);
        }
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}
void AbilityBatch6Nodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Chain;
    Chain.TypeId = TEXT("Delivery.ForkedChain");
    Chain.ConfigClass = UAbilityForkedChainNodeConfig::StaticClass();
    Chain.ExecutionClass = UAbilityForkedChainNodeExecution::StaticClass();
    Chain.InputPayload = EAbilityPortPayload::LiveTarget;
    Chain.bSupportsActorTargets = Chain.bSupportsMassTargets = true;
    Chain.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                   {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Volley = Chain;
    Volley.TypeId = TEXT("Delivery.ForkedVolley");
    Volley.ConfigClass = UAbilityForkedVolleyNodeConfig::StaticClass();
    Volley.ExecutionClass = UAbilityLightningNodeExecution::StaticClass();
    FAbilityNodeDescriptor Sequential = Volley;
    Sequential.TypeId = TEXT("Delivery.SequentialChain");
    Sequential.ConfigClass = UAbilitySequentialChainNodeConfig::StaticClass();
    FAbilityNodeDescriptor Petrify;
    Petrify.TypeId = TEXT("Status.PetrifyArea");
    Petrify.ConfigClass = UAbilityAreaPetrifyNodeConfig::StaticClass();
    Petrify.ExecutionClass = UAbilityAreaPetrifyNodeExecution::StaticClass();
    Petrify.InputPayload = EAbilityPortPayload::Origin;
    Petrify.bSupportsActorTargets = Petrify.bSupportsMassTargets = Petrify.bSupportsSnapshotTargets = true;
    Petrify.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Petrify.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Chain, Volley, Sequential, Petrify})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Batch6 node registration: %s"), *Error);
        }
    }
}
