// 调用链：传播策略 -> 受管真实投递 -> 命中回执 -> 有界逐跳；不复制完整GA。
#include "AbilitySystem/Unified/Nodes/AbilityPropagationNodes.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "PureMass/PureMassCombatTarget.h"

bool UAbilityPropagationNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    if (Delivery.ImpactPolicy != EAbilityProjectileImpactPolicy::StopFirst ||
        Delivery.Motion == EAbilityProjectileMotion::Returning)
    {
        Error = TEXT(
            "Sequential propagation requires a terminal single-hit delivery; multi-impact visited semantics are not supported.");
        return false;
    }
    if (MaxTargets < 1 || MaxTargets > 64 || !FMath::IsFinite(JumpDelay) || JumpDelay < .01f || JumpDelay > 5.f ||
        !FMath::IsFinite(JumpRadius) || JumpRadius <= 0.f || JumpRadius > 3000.f || !FMath::IsFinite(DamageFalloff) ||
        DamageFalloff < 0.f || DamageFalloff > 1.f ||
        static_cast<uint8>(TargetSelection) > static_cast<uint8>(EAbilityTargetSelectionPolicy::LowestHealthRatio))
    {
        Error = TEXT("Propagation requires 1-64 targets, delay in [0.01,5], radius in (0,3000], and falloff in [0,1].");
    }
    return Error.IsEmpty();
}
bool UAbilityPropagationNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                          FString& Error)
{
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Propagation parameters must be finite.");
        return false;
    }
    const bool bInteger = Parameter == EAbilityConfigParameter::PropagationMaxTargets ||
                          Parameter == EAbilityConfigParameter::TargetSelection;
    if (bInteger && (Value < 0.f || Value > 64.f || !FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value))))
    {
        Error = TEXT("Propagation count and policy must be bounded integers.");
        return false;
    }
    switch (Parameter)
    {
    case EAbilityConfigParameter::PropagationMaxTargets:
        MaxTargets = FMath::RoundToInt(Value);
        break;
    case EAbilityConfigParameter::PropagationDelay:
        JumpDelay = Value;
        break;
    case EAbilityConfigParameter::PropagationRadius:
        JumpRadius = Value;
        break;
    case EAbilityConfigParameter::PropagationFalloff:
        DamageFalloff = Value;
        break;
    case EAbilityConfigParameter::TargetSelection:
        TargetSelection = static_cast<EAbilityTargetSelectionPolicy>(FMath::RoundToInt(Value));
        break;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    return Validate(Error);
}
bool UAbilityPropagationNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityPropagationNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    RootCastId = Owner.GetExecutionId();
    return Launch(Input.Origin, Input.LiveTargetToken, Input.Direction, Error);
}
void UAbilityPropagationNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
    {
        return;
    }
    // 当前投递是否结束由真实命中回执决定，不能用飞行估计时间代替。
    if (Delivery && !Delivery->IsFinished())
    {
        Delivery->Advance(DeltaSeconds);
        return;
    }
    if (!bWaitingForJump)
    {
        return;
    }
    WaitRemaining -= DeltaSeconds;
    if (WaitRemaining > UE_SMALL_NUMBER)
    {
        return;
    }
    if (Settings->TargetSelection != EAbilityTargetSelectionPolicy::Original)
    {
        TArray<FAbilitySelectedCandidate> Selected;
        FString SelectionError;
        if (!AbilityTargetSelectionService::Select(
                Input.SourceToken, LastImpact, Settings->JumpRadius, 1, Settings->TargetSelection,
                [&](const FAbilityTargetToken& Candidate)
                {
                    return Visited.ContainsByPredicate(
                        [&](const FAbilityTargetToken& Prior)
                        {
                            return AbilityBehaviorPrimitives::SameTargetLife(Prior, Candidate);
                        });
                },
                Selected, SelectionError))
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
            return;
        }
        if (Selected.IsEmpty())
        {
            Complete();
            return;
        }
        if (!Launch(LastImpact, Selected[0].Target, (Selected[0].Position - LastImpact).GetSafeNormal(),
                    SelectionError))
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
        return;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    TArray<FAbilityTargetCandidate> Candidates;
    for (const auto& Target :
         PureMassCombat::Gather(Input.SourceToken.World.Get(), Caster, LastImpact, Settings->JumpRadius))
    {
        FVector Location;
        FAbilityTargetToken Token;
        FString Error;
        if (PureMassCombat::Resolve(Input.SourceToken.World.Get(), Caster, Target, Location) &&
            PureMassCombat::Visible(Input.SourceToken.World.Get(), Caster, LastImpact, Target) &&
            AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Target, Token, Error))
        {
            Candidates.Add({Token, Location});
        }
    }
    const auto Next = AbilityBehaviorPrimitives::SelectNearest(Candidates, LastImpact, Settings->JumpRadius, Visited);
    if (!Next.IsSet())
    {
        Complete();
        return;
    }
    FString Error;
    if (!Launch(LastImpact, Next->Target, (Next->Location - LastImpact).GetSafeNormal(), Error))
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
    }
}
void UAbilityPropagationNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    bWaitingForJump = false;
    if (Delivery)
    {
        Delivery->OnFinished.RemoveAll(this);
        Delivery->Cancel();
        Delivery = nullptr;
    }
    Settings = nullptr;
    Visited.Reset();
}
bool UAbilityPropagationNodeExecution::Launch(FVector Origin, const TOptional<FAbilityTargetToken>& Target,
                                              FVector Direction, FString& Error)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || DeliveriesStarted >= Settings->MaxTargets ||
        !Owner->ReservePhysicalSpawn(InvocationId))
    {
        Error = TEXT("Propagation exhausted its original graph delivery budget.");
        return false;
    }
    FAbilityDeliveryRequest Request;
    Request.Source = Input.SourceToken;
    Request.Target = Target;
    Request.Origin = Origin;
    Request.Direction = Direction;
    if (!AbilityDeliveryNodes::ResolveDeliverySettings(*Settings, Input, RootCastId, Request.Settings, Error))
    {
        return false;
    }
    if (Request.Settings.ImpactPolicy != EAbilityProjectileImpactPolicy::StopFirst ||
        Request.Settings.Motion == EAbilityProjectileMotion::Returning)
    {
        Error = TEXT("Sequential propagation requires a prepared terminal single-impact delivery.");
        return false;
    }
    if (Request.Settings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults ||
        Request.Settings.NativeConfigureHoming)
    {
        // 首箭保留Notify点瞄准；后续Target在真实Begin中解析成该原生命的位置，不回到首目标。
        Request.AimPoint = Input.EventCenter;
    }
    Request.DamageParams = Input.DamageParams;
    const float Scale = FMath::Pow(Settings->DamageFalloff, DeliveriesStarted);
    Request.DamageScale = Input.DamageScale * Scale;
    Request.DebuffDamageScale = Input.DebuffDamageScale * Scale;
    Request.RootCastId = RootCastId;
    Request.StepId = FGuid::NewGuid();
    Request.IgnoredTargets = Visited;
    bWaitingForJump = false;
    ++DeliveriesStarted;
    Delivery = NewObject<UAbilityDeliveryInstance>(this);
    Delivery->OnFinished.AddUObject(this, &ThisClass::Delivered);
    return Delivery->Begin(Request, Error);
}
void UAbilityPropagationNodeExecution::Delivered(const FAbilityDeliveryResult& Result)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || Result.RootCastId != RootCastId)
    {
        return;
    }
    if (Result.Outcome != EAbilityDeliveryOutcome::Hit || !Result.Impact.IsSet())
    {
        // 默认连锁只由真实命中推动；撞墙/超时/目标失效不从错误地点凭空继续。
        if (Result.Outcome == EAbilityDeliveryOutcome::Cancelled)
        {
            Owner->FinishInvocation(InvocationId, EAbilityGraphEndReason::Cancelled);
        }
        else
        {
            Complete();
        }
        return;
    }
    Visited.Add(Result.Impact->ConfirmedTarget);
    LastImpact = Result.Impact->Location;
    auto Payload = Input;
    Payload.LiveTargetToken.Reset();
    Payload.Impact = Result.Impact;
    Payload.Origin = Payload.EventCenter = LastImpact;
    Payload.Direction = Result.Direction;
    Payload.bConfirmedHit = true;
    Payload.DamageScale = Result.DamageScale;
    Payload.DebuffDamageScale = Result.DebuffDamageScale;
    Owner->Publish(InvocationId, TEXT("Hit"), Payload);
    if (Result.Impact->ContactDamageProfile != EAbilityContactDamageProfile::Unified && !bStopped &&
        !Owner->IsFinished())
    {
        // 原native接触同步消费外部payload；仍走同一drainer，并在排队下一跳前观察GE/取消/死亡。
        Owner->FlushPublishedEvents(true);
    }
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    if (Result.Damage.bAttempted)
    {
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.Damage.bAttempted;
        Payload.bKilled = Result.Damage.bKilled;
        Payload.DamageApplied = Result.Damage.Applied;
        Payload.ActualDamage = Result.Damage.ActualDamage;
        Owner->Publish(InvocationId, TEXT("DamageResolved"), Payload);
    }
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    if (Visited.Num() >= Settings->MaxTargets)
    {
        Complete();
        return;
    }
    bWaitingForJump = true;
    WaitRemaining = Settings->JumpDelay;
}
void UAbilityPropagationNodeExecution::Complete()
{
    auto* Owner = GraphOwner.Get();
    if (!bStopped && Owner && !Owner->IsFinished())
    {
        auto Payload = Input;
        Payload.Origin = Payload.EventCenter = LastImpact;
        Payload.LiveTargetToken.Reset();
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}
void AbilityPropagationNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Propagation.Sequential")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Propagation.Sequential");
    Descriptor.ConfigClass = UAbilityPropagationNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPropagationNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    // 每一跳先经过精确调用的累计准入；节点持有至真实弹体销毁回执，不会多发并发弹体。
    Descriptor.bRuntimeFiniteSafe = true;
    Descriptor.Ports = {{TEXT("Hit"), EAbilityPortPayload::ConfirmedImpact, false},
                        {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Propagation node registration: %s"), *Error);
}
