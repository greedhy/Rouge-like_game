// 调用链：图策略Start -> 原混合查询/生命令牌 -> DamageService或StatusService -> 原复制表现/状态持有者。
#include "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityStatusService.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"

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
        FAbilityNodeInput Payload = Input;
        FAbilityImpactSnapshot Impact;
        Impact.Location = Position;
        Impact.Direction = (Position - Entry.From).GetSafeNormal();
        Impact.ConfirmedTarget = Entry.Target;
        Payload.Impact = Impact;
        Payload.Origin = Position;
        Payload.LiveTargetToken.Reset();
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.bAttempted;
        Payload.bKilled = Result.bKilled;
        Payload.DamageApplied = Result.Applied;
        Payload.ActualDamage = Result.ActualDamage;
        if (!Owner.Publish(Id, TEXT("DamageResolved"), Payload))
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
    FActorSpawnParameters Spawn;
    Spawn.Owner = Caster;
    if (auto* Visual =
            Input.SourceToken.World->SpawnActor<ABatch6ChainPresentation>(Input.Origin, FRotator::ZeroRotator, Spawn))
    {
        Visual->Segments = MoveTemp(Segments);
        Visual->BeamSystem = Settings.BeamSystem;
        Visual->Present();
        Visual->ForceNetUpdate();
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
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
    FAbilityNodeDescriptor Petrify;
    Petrify.TypeId = TEXT("Status.PetrifyArea");
    Petrify.ConfigClass = UAbilityAreaPetrifyNodeConfig::StaticClass();
    Petrify.ExecutionClass = UAbilityAreaPetrifyNodeExecution::StaticClass();
    Petrify.InputPayload = EAbilityPortPayload::Origin;
    Petrify.bSupportsActorTargets = Petrify.bSupportsMassTargets = Petrify.bSupportsSnapshotTargets = true;
    Petrify.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
    Petrify.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Chain, Petrify})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Batch6 node registration: %s"), *Error);
        }
    }
}
