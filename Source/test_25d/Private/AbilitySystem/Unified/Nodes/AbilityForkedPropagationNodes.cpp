// 调用链：运行时配方 -> 本BFS宿主 -> 原真实Delivery -> Hit独立载荷 -> 共享选敌 -> 并发或兼容FIFO投递。
#include "AbilitySystem/Unified/Nodes/AbilityForkedPropagationNodes.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityForkedPropagationService.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** 比较完整原生命，不能把池中同一Actor/数字ID的新生命当作已访问目标。 */
    bool ContainsLife(const TArray<FAbilityTargetToken>& Targets, const FAbilityTargetToken& Target)
    {
        return Targets.ContainsByPredicate(
            [&](const auto& Existing)
            {
                return AbilityBehaviorPrimitives::SameTargetLife(Existing, Target);
            });
    }
} // namespace

/** 新节点默认仅交付接触，后继载荷是唯一伤害/状态所有者。 */
UAbilityForkedPropagationNodeConfig::UAbilityForkedPropagationNodeConfig()
{
    Delivery.bApplyDamage = false;
}

bool UAbilityForkedPropagationNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    if (MaxDepth < 0 || MaxDepth > 16 || BranchesPerTarget < 1 || BranchesPerTarget > 8 ||
        (!bUseNativeLaunchParameters && Delivery.bApplyDamage))
    {
        Error = TEXT("Forked propagation requires depth0..16, branches1..8 and an independent external payload");
        return false;
    }
    return true;
}

bool UAbilityForkedPropagationNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                                FString& Error)
{
    if (Parameter != EAbilityConfigParameter::ForkDepth && Parameter != EAbilityConfigParameter::ForkBranches &&
        Parameter != EAbilityConfigParameter::ForkConcurrent)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!FMath::IsFinite(Value) || Value < 0.f || Value > 16.f ||
        !FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value)) ||
        (Parameter == EAbilityConfigParameter::ForkConcurrent && Value > 1.f))
    {
        Error = TEXT("Fork settings require bounded integer depth/branches or a 0/1 concurrency mode.");
        return false;
    }
    if (Parameter == EAbilityConfigParameter::ForkDepth)
    {
        MaxDepth = FMath::RoundToInt(Value);
    }
    else if (Parameter == EAbilityConfigParameter::ForkBranches)
    {
        BranchesPerTarget = FMath::RoundToInt(Value);
    }
    else
    {
        bConcurrentBranches = Value > 0.f;
    }
    return Validate(Error);
}

bool UAbilityForkedPropagationNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                   const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                   FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityForkedPropagationNodeConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    if (!Settings->Validate(Error))
    {
        return false;
    }
    Pending.Add({Input.LiveTargetToken, Input.Origin, Input.Direction, 0, 0.});
    if (Input.LiveTargetToken.IsSet())
    {
        Reserved.Add(Input.LiveTargetToken.GetValue());
    }
    LastImpact = Input.Origin;
    return LaunchReady(Error);
}

void UAbilityForkedPropagationNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
    {
        return;
    }
    Clock += DeltaSeconds;
    // 回执可同步取消其它兄弟；保活快照并逐颗复核仍属于本节点，避免迭代被回调删除的数组。
    TArray<TStrongObjectPtr<UAbilityDeliveryInstance>> Advancing;
    for (const auto& Delivery : ActiveDeliveries)
    {
        Advancing.Emplace(Delivery.Get());
    }
    for (const auto& Delivery : Advancing)
    {
        if (bStopped || Owner->IsFinished())
        {
            return;
        }
        if (ActiveDeliveries.Contains(Delivery.Get()))
        {
            Delivery->Advance(DeltaSeconds);
        }
    }
    FString Error;
    if (!bStopped && !Owner->IsFinished() && !LaunchReady(Error) && !Owner->IsFinished())
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
    }
}

bool UAbilityForkedPropagationNodeExecution::LaunchReady(FString& Error)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return false;
    }
    AActor* Source = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Source))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return false;
    }
    while (!Pending.IsEmpty())
    {
        if ((!Settings->bConcurrentBranches && (!Flights.IsEmpty() || Clock + UE_SMALL_NUMBER < NextSerialLaunch)) ||
            Clock + UE_SMALL_NUMBER < Pending[0].ReadyAt)
        {
            break;
        }
        const auto Admission = Owner->CheckPhysicalAdmission(InvocationId);
        if (Admission == EAbilityPhysicalAdmission::Wait)
        {
            break;
        }
        if (Admission == EAbilityPhysicalAdmission::Exhausted)
        {
            Error = TEXT("Forked propagation exhausted the original cumulative physical budget.");
            Owner->Cancel(EAbilityGraphEndReason::BudgetExceeded);
            return false;
        }
        const FPending Step = Pending[0];
        Pending.RemoveAt(0, 1, EAllowShrinking::No);
        if (!Launch(Step, Error))
        {
            return false;
        }
        if (bStopped || Owner->IsFinished())
        {
            return true;
        }
    }
    if (Pending.IsEmpty() && Flights.IsEmpty())
    {
        Complete();
    }
    return true;
}

bool UAbilityForkedPropagationNodeExecution::Launch(const FPending& Step, FString& Error)
{
    auto* Owner = GraphOwner.Get();
    FAbilityDeliveryRequest Request;
    if (!Owner ||
        !AbilityDeliveryNodes::ResolveDeliverySettings(*Settings, Input, Owner->GetExecutionId(), Request.Settings,
                                                       Error) ||
        Request.Settings.bApplyDamage || Request.Settings.ImpactPolicy != EAbilityProjectileImpactPolicy::StopFirst ||
        Request.Settings.Motion == EAbilityProjectileMotion::Returning)
    {
        Error = TEXT("Forked propagation requires one terminal contact and an external payload owner.");
        return false;
    }
    FGuid Lease;
    if (!Owner->ReservePhysicalSpawnWithLease(InvocationId, Lease))
    {
        if (!Owner->IsFinished())
        {
            Owner->Cancel(EAbilityGraphEndReason::BudgetExceeded);
        }
        return false;
    }
    Request.Source = Input.SourceToken;
    Request.Target = Step.Target;
    Request.Origin = Step.Origin;
    Request.Direction = Step.Direction;
    Request.DamageParams = Input.DamageParams;
    const float Scale = FMath::Pow(Settings->DamageFalloff, Step.Depth);
    Request.DamageScale = Input.DamageScale * Scale;
    Request.DebuffDamageScale = Input.DebuffDamageScale * Scale;
    Request.RootCastId = Owner->GetExecutionId();
    Request.StepId = FGuid::NewGuid();
    Request.IgnoredTargets = Confirmed;
    if (Request.Settings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults ||
        Request.Settings.NativeConfigureHoming)
    {
        Request.AimPoint = Input.EventCenter;
    }
    auto* Delivery = NewObject<UAbilityDeliveryInstance>(this);
    const TStrongObjectPtr<UAbilityDeliveryInstance> Starting(Delivery);
    // 必须在Begin前登记：无目标/阻挡等终态也可能在Begin内部同步回调。
    Flights.Add(Request.StepId, {Step, Delivery, Lease});
    ActiveDeliveries.Add(Delivery);
    Delivery->OnFinished.AddUObject(this, &ThisClass::Delivered);
    ++DeliveriesStarted;
    if (!Delivery->Begin(Request, Error))
    {
        Delivery->OnFinished.RemoveAll(this);
        if (Flights.Remove(Request.StepId) > 0)
        {
            Owner->ReleasePhysicalSpawnLease(Lease);
        }
        ActiveDeliveries.Remove(Delivery);
        Delivery->Cancel();
        return false;
    }
    return true;
}

void UAbilityForkedPropagationNodeExecution::Delivered(const FAbilityDeliveryResult& Result)
{
    const TStrongObjectPtr<UAbilityForkedPropagationNodeExecution> SelfGuard(this);
    auto* Owner = GraphOwner.Get();
    const auto* Found = Flights.Find(Result.StepId);
    if (bStopped || !Owner || Owner->IsFinished() || !Found || !Found->Delivery.IsValid() ||
        Result.RootCastId != Owner->GetExecutionId() || Result.DeliveryId != Found->Delivery->GetResult().DeliveryId)
    {
        return;
    }
    const FFlight Flight = *Found;
    const TStrongObjectPtr<UAbilityDeliveryInstance> Finished(Flight.Delivery.Get());
    // 先封准确步骤，再做可能重入的外部GE和取消；旧回执不能释放另一颗弹体的租约。
    Flights.Remove(Result.StepId);
    ActiveDeliveries.Remove(Finished.Get());
    Finished->OnFinished.RemoveAll(this);
    Owner->ReleasePhysicalSpawnLease(Flight.Lease);
    NextSerialLaunch = Clock + Settings->JumpDelay;
    if (Result.Outcome == EAbilityDeliveryOutcome::Cancelled)
    {
        Owner->FinishInvocation(InvocationId, EAbilityGraphEndReason::Cancelled);
        return;
    }
    if (Result.Outcome == EAbilityDeliveryOutcome::Hit && Result.Impact.IsSet() &&
        !ContainsLife(Confirmed, Result.Impact->ConfirmedTarget))
    {
        LastImpact = Result.Impact->Location;
        const auto Target = Result.Impact->ConfirmedTarget;
        Confirmed.Add(Target);
        if (!ContainsLife(Reserved, Target))
        {
            Reserved.Add(Target);
        }
        auto Payload = Input;
        Payload.LiveTargetToken.Reset();
        Payload.Impact = Result.Impact;
        Payload.Origin = Payload.EventCenter = LastImpact;
        Payload.Direction = Result.Direction;
        Payload.DamageScale = Result.DamageScale;
        Payload.DebuffDamageScale = Result.DebuffDamageScale;
        Payload.bConfirmedHit = true;
        if (!Owner->Publish(InvocationId, TEXT("Hit"), Payload))
        {
            return;
        }
        Owner->FlushPublishedEvents(true);
        if (bStopped || Owner->IsFinished())
        {
            return;
        }
        const int32 Used = DeliveriesStarted + Pending.Num();
        if (Flight.Step.Depth < Settings->MaxDepth && Used < Settings->MaxTargets)
        {
            if (Settings->TargetSelection == EAbilityTargetSelectionPolicy::Original)
            {
                // 原默认候选次序保持兼容；并发/串行只控制真实投递调度，不改变旧选敌合同。
                FAbilityForkedSelectionSettings Selection;
                Selection.MaxTargets = Settings->MaxTargets;
                Selection.MaxDepth = Settings->MaxDepth;
                Selection.BranchesPerTarget = Settings->BranchesPerTarget;
                Selection.Radius = Settings->JumpRadius;
                const auto Children = AbilityForkedPropagationService::SelectChildren(
                    Input.SourceToken, LastImpact, Flight.Step.Depth + 1, Used, Selection,
                    [](const auto&)
                    {
                        return false;
                    },
                    [&](const auto& Candidate)
                    {
                        return ContainsLife(Reserved, Candidate);
                    });
                for (const auto& Child : Children)
                {
                    Reserved.Add(Child.Target);
                    Pending.Add({Child.Target, LastImpact, (Child.Position - LastImpact).GetSafeNormal(),
                                 Flight.Step.Depth + 1, Clock + Settings->JumpDelay});
                }
            }
            else
            {
                TArray<FAbilitySelectedCandidate> Children;
                FString Error;
                if (!AbilityTargetSelectionService::Select(
                        Input.SourceToken, LastImpact, Settings->JumpRadius,
                        FMath::Min(Settings->BranchesPerTarget, Settings->MaxTargets - Used), Settings->TargetSelection,
                        [&](const auto& Candidate)
                        {
                            return ContainsLife(Reserved, Candidate);
                        },
                        Children, Error))
                {
                    Owner->Cancel(EAbilityGraphEndReason::Failed);
                    return;
                }
                for (const auto& Child : Children)
                {
                    Reserved.Add(Child.Target);
                    Pending.Add({Child.Target, LastImpact, (Child.Position - LastImpact).GetSafeNormal(),
                                 Flight.Step.Depth + 1, Clock + Settings->JumpDelay});
                }
            }
        }
    }
    if (Pending.IsEmpty() && Flights.IsEmpty())
    {
        Complete();
    }
}

void UAbilityForkedPropagationNodeExecution::Complete()
{
    auto* Owner = GraphOwner.Get();
    if (!bStopped && Owner && !Owner->IsFinished())
    {
        auto Payload = Input;
        Payload.LiveTargetToken.Reset();
        Payload.Origin = Payload.EventCenter = LastImpact;
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityForkedPropagationNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped)
    {
        return;
    }
    Super::Stop(Reason);
    // 所有身份先封口；Cancel回调或GC不能重新扩展已终止节点。
    const auto Stopping = Flights;
    Flights.Reset();
    Pending.Reset();
    for (const auto& Pair : Stopping)
    {
        if (auto* Delivery = Pair.Value.Delivery.Get())
        {
            Delivery->OnFinished.RemoveAll(this);
            if (Owner)
            {
                Owner->ReleasePhysicalSpawnLease(Pair.Value.Lease);
            }
            Delivery->Cancel();
        }
        else if (Owner)
        {
            Owner->ReleasePhysicalSpawnLease(Pair.Value.Lease);
        }
    }
    ActiveDeliveries.Reset();
    Reserved.Reset();
    Confirmed.Reset();
    Settings = nullptr;
}

void AbilityForkedPropagationNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Propagation.Forked")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Propagation.Forked");
    Descriptor.ConfigClass = UAbilityForkedPropagationNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityForkedPropagationNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    // 每次真实投递均经当前InvocationId预留；回执/取消释放精确lease，不能重置累计生成额度。
    Descriptor.bRuntimeFiniteSafe = true;
    Descriptor.Ports = {{TEXT("Hit"), EAbilityPortPayload::ConfirmedImpact, false},
                        {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Forked propagation registration: %s"), *Error);
}
