// 调用链：图调度 -> 原型运动节点 -> 无伤害查询/纯表现；后继效果始终通过同一 GraphInstance 执行。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityPrototypeFlightQueries.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/Actor.h"

namespace AbilityPrototypeFlightDetails
{
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    bool ActorProfile(const UAbilityPrototypeParameterFrame& Frame)
    {
        return Frame.GetProfile() == TEXT("PrototypeFine.Actor.v1");
    }

    FAbilityNodeInput Position(const FAbilityNodeInput& Input, const FAbilityProjectileMotionState& Motion)
    {
        auto Result = Input;
        Result.Origin = Result.EventCenter = Motion.Position;
        Result.Direction = Motion.Direction;
        Result.TargetSet.Reset();
        return Result;
    }
} // namespace AbilityPrototypeFlightDetails

bool UAbilityPrototypeFlightNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                              FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Prototype flight override requires a finite value");
        return false;
    }
    const auto IsIntegerIn = [&](int32 Min, int32 Max)
    {
        return Value >= Min && Value <= Max && Value == FMath::FloorToFloat(Value);
    };
    const auto SetPositive = [&](float& Destination, bool& bEnabled, float Maximum)
    {
        if (Value <= 0.f || Value > Maximum)
        {
            return false;
        }
        Destination = Value;
        bEnabled = true;
        return true;
    };
    bool bAccepted = false;
    switch (Parameter)
    {
    case EAbilityConfigParameter::Motion:
        if (IsIntegerIn(0, int32(EAbilityProjectileMotion::Returning)) &&
            int32(Value) != int32(EAbilityProjectileMotion::NativeHoming))
        {
            Motion = static_cast<EAbilityProjectileMotion>(int32(Value));
            bOverrideMotion = bAccepted = true;
        }
        break;
    case EAbilityConfigParameter::ImpactPolicy:
        if (IsIntegerIn(0, int32(EAbilityProjectileImpactPolicy::ReturnPerPhase)))
        {
            ImpactPolicy = static_cast<EAbilityProjectileImpactPolicy>(int32(Value));
            bOverrideImpactPolicy = bAccepted = true;
        }
        break;
    case EAbilityConfigParameter::ImpactLimit:
        if (IsIntegerIn(1, 64))
        {
            ImpactLimit = int32(Value);
            bOverrideImpactLimit = bAccepted = true;
        }
        break;
    case EAbilityConfigParameter::DamageFalloff:
        if (Value >= 0.f && Value <= 1.f)
        {
            DamageFalloff = Value;
            bOverrideDamageFalloff = bAccepted = true;
        }
        break;
    case EAbilityConfigParameter::Speed:
        bAccepted = SetPositive(Speed, bOverrideSpeed, 10000.f);
        break;
    case EAbilityConfigParameter::Range:
        bAccepted = SetPositive(Range, bOverrideRange, 10000.f);
        break;
    case EAbilityConfigParameter::CollisionRadius:
        bAccepted = SetPositive(CollisionRadius, bOverrideCollisionRadius, 200.f);
        break;
    case EAbilityConfigParameter::HomingTurnRate:
        bAccepted = SetPositive(HomingTurnRate, bOverrideHomingTurnRate, 720.f);
        break;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!bAccepted)
    {
        Error = TEXT("Prototype flight override is out of bounds or requires an unsupported native movement component");
    }
    return bAccepted;
}

bool UAbilityPrototypeFlightNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    const auto Positive = [](float Value, float Maximum)
    {
        return FMath::IsFinite(Value) && Value > 0.f && Value <= Maximum;
    };
    if ((bOverrideMotion && (static_cast<uint8>(Motion) > static_cast<uint8>(EAbilityProjectileMotion::Returning) ||
                             Motion == EAbilityProjectileMotion::NativeHoming)) ||
        (bOverrideImpactPolicy &&
         static_cast<uint8>(ImpactPolicy) > static_cast<uint8>(EAbilityProjectileImpactPolicy::ReturnPerPhase)) ||
        (bOverrideImpactLimit && (ImpactLimit < 1 || ImpactLimit > 64)) ||
        (bOverrideDamageFalloff && (!FMath::IsFinite(DamageFalloff) || DamageFalloff < 0.f || DamageFalloff > 1.f)) ||
        (bOverrideSpeed && !Positive(Speed, 10000.f)) || (bOverrideRange && !Positive(Range, 10000.f)) ||
        (bOverrideCollisionRadius && !Positive(CollisionRadius, 200.f)) ||
        (bOverrideHomingTurnRate && !Positive(HomingTurnRate, 720.f)))
    {
        Error = TEXT("Prototype flight contains an invalid explicit motion/contact override");
    }
    return Error.IsEmpty();
}

bool UAbilityPrototypeFlightNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Configuration,
                                                 const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
                   Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("Prototype flight requires this root's Actor/Mixed parameter frame");
        return false;
    }
    const auto Kind = Frame->GetParameters().Kind;
    if ((Kind != EAbilityPrototypeKind::Projectile && Kind != EAbilityPrototypeKind::Homing &&
         Kind != EAbilityPrototypeKind::Piercing && Kind != EAbilityPrototypeKind::Returning &&
         Kind != EAbilityPrototypeKind::Multishot) ||
        (Kind == EAbilityPrototypeKind::Multishot && !Frame->IsSingleProjectile()))
    {
        Error = TEXT("Flight requires a moving prototype or a single admitted multishot pellet");
        return false;
    }
    const auto* Overrides = Cast<UAbilityPrototypeFlightNodeConfig>(&Configuration);
    if (!Overrides || !Overrides->Validate(Error))
    {
        return false;
    }
    const auto& Parameters = Frame->GetParameters();
    EffectiveFlight.Motion = Kind == EAbilityPrototypeKind::Returning ? EAbilityProjectileMotion::Returning
                             : Kind == EAbilityPrototypeKind::Homing  ? EAbilityProjectileMotion::LegacyTurnHoming
                                                                      : EAbilityProjectileMotion::Linear;
    EffectiveFlight.Speed = Parameters.Speed;
    EffectiveFlight.Range = Parameters.Range;
    EffectiveFlight.CollisionRadius = Parameters.CollisionRadius;
    EffectiveFlight.HomingTurnRate = Parameters.HomingTurnRate;
    EffectiveImpactPolicy = Kind == EAbilityPrototypeKind::Returning  ? EAbilityProjectileImpactPolicy::ReturnPerPhase
                            : Kind == EAbilityPrototypeKind::Piercing ? EAbilityProjectileImpactPolicy::Pierce
                                                                      : EAbilityProjectileImpactPolicy::StopFirst;
    EffectiveImpactLimit = Parameters.Count;
    EffectiveDamageFalloff = Parameters.DamageFalloff;
    // 覆盖在本次Start才与原帧合成；不伪造Kind，也不把更改写回Scope或共享DA。
    if (Overrides->bOverrideMotion)
    {
        EffectiveFlight.Motion = Overrides->Motion;
    }
    if (Overrides->bOverrideImpactPolicy)
    {
        EffectiveImpactPolicy = Overrides->ImpactPolicy;
    }
    if (Overrides->bOverrideImpactLimit)
    {
        EffectiveImpactLimit = Overrides->ImpactLimit;
    }
    if (Overrides->bOverrideDamageFalloff)
    {
        EffectiveDamageFalloff = Overrides->DamageFalloff;
    }
    if (Overrides->bOverrideSpeed)
    {
        EffectiveFlight.Speed = Overrides->Speed;
    }
    if (Overrides->bOverrideRange)
    {
        EffectiveFlight.Range = Overrides->Range;
    }
    if (Overrides->bOverrideCollisionRadius)
    {
        EffectiveFlight.CollisionRadius = Overrides->CollisionRadius;
    }
    if (Overrides->bOverrideHomingTurnRate)
    {
        EffectiveFlight.HomingTurnRate = Overrides->HomingTurnRate;
    }
    if (EffectiveImpactPolicy == EAbilityProjectileImpactPolicy::ReturnPerPhase &&
        EffectiveFlight.Motion != EAbilityProjectileMotion::Returning)
    {
        Error = TEXT("Prototype return-per-phase contacts require the explicit returning motion");
        return false;
    }
    Motion = {};
    Motion.Position = Input.Origin;
    Motion.Direction = Input.Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    InitialTarget = Input.LiveTargetToken;
    if (!InitialTarget.IsSet() && Input.Impact.IsSet())
    {
        InitialTarget = Input.Impact->ConfirmedTarget;
    }
    Age = PeriodClock = 0.f;
    ImpactId = 0;
    bCompleting = false;
    Visited.Reset();
    Visual = {};
    Visual.bReady = true;
    Visual.bSingleProjectile = Frame->IsSingleProjectile();
    Visual.Transform = FTransform(Motion.Direction.Rotation(), Motion.Position);
    if (!Owner.ReservePhysicalSpawnWithLease(InvocationId, PhysicalLease))
    {
        Error = TEXT("Root physical flight admission budget exhausted");
        return false;
    }
    PresentationHost = AbilityPrototypePresentationService::Begin(
        Input.SourceToken, Frame->GetParameters(), Frame->GetStableReference(), Visual,
        AbilityPrototypeFlightDetails::ActorProfile(*Frame) ? EAbilityPrototypePresentationProfile::Actor
                                                            : EAbilityPrototypePresentationProfile::Mixed,
        Error);
    if (!PresentationHost)
    {
        ReleasePhysicalLease();
        return false;
    }
    if (!Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeFlightDetails::PresentationSlot, PresentationHost))
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, true);
        PresentationHost = nullptr;
        ReleasePhysicalLease();
        Error = TEXT("Flight could not bind its exact presentation resource");
        return false;
    }
    return true;
}

void UAbilityPrototypeFlightNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeFlightNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeParameterFrame> FrameGuard(Frame.Get());
    const auto& Parameters = Frame->GetParameters();
    const bool bActor = AbilityPrototypeFlightDetails::ActorProfile(*Frame);
    Age += DeltaSeconds;
    const bool bFinalReturn = bActor && EffectiveFlight.Motion == EAbilityProjectileMotion::Returning;
    if (!bFinalReturn && Age >= Parameters.Duration)
    {
        CompleteFlight();
        return;
    }
    if (!bActor)
    {
        // 原 Mixed 在运动前记录辅助周期；不在这里递归推进孩子的完整寿命。
        AbilityBehaviorPrimitives::AdvanceCadence(
            PeriodClock, DeltaSeconds, Parameters.Period, 8, EAbilityCadenceRemainder::Preserve,
            [&]()
            {
                if (Owner->HasSuccessor(InvocationId, TEXT("Period")))
                {
                    Owner->RequestChild(InvocationId, TEXT("Period"),
                                        AbilityPrototypeFlightDetails::Position(Input, Motion));
                }
                else
                {
                    Owner->Publish(InvocationId, TEXT("Period"),
                                   AbilityPrototypeFlightDetails::Position(Input, Motion));
                }
                return !bStopped && !Owner->IsFinished();
            });
    }
    const float ActiveDelta =
        bFinalReturn ? FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - (Age - DeltaSeconds)))
                     : DeltaSeconds;
    Move(ActiveDelta);
    if (!bStopped && !bCompleting && !Owner->IsFinished() && bFinalReturn && Age >= Parameters.Duration)
    {
        CompleteFlight();
    }
}

void UAbilityPrototypeFlightNodeExecution::RefreshPose(bool bRotate)
{
    Visual.Revision++;
    Visual.Transform.SetLocation(Motion.Position);
    if (bRotate)
    {
        Visual.Transform.SetRotation(Motion.Direction.Rotation().Quaternion());
    }
    Visual.bReturning = Motion.Phase == EAbilityProjectilePhase::Returning;
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
}

bool UAbilityPrototypeFlightNodeExecution::Contact(const FAbilityPrototypeFlightQueryHit& Hit, float Scale)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return false;
    }
    auto Payload = AbilityPrototypeFlightDetails::Position(Input, Motion);
    Payload.LiveTargetToken.Reset();
    Payload.DamageScale *= Scale;
    auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
    Targets->Source = Input.SourceToken;
    if (Hit.Life.IsSet())
    {
        Targets->Targets.Add(Hit.Life.GetValue());
    }
    Payload.TargetSet = MoveTemp(Targets);
    if (!Owner->Publish(InvocationId, TEXT("Contact"), Payload))
    {
        return false;
    }
    Owner->FlushImmediatePayloads(true);
    return !bStopped && !bCompleting && !Owner->IsFinished();
}

void UAbilityPrototypeFlightNodeExecution::Move(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const bool bActor = AbilityPrototypeFlightDetails::ActorProfile(*Frame);
    const bool bReturn = EffectiveFlight.Motion == EAbilityProjectileMotion::Returning;
    const bool bHoming = EffectiveFlight.Motion == EAbilityProjectileMotion::LegacyTurnHoming;
    auto Resolve =
        [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location, FString& Error)
    {
        return Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, Error);
    };
    float Remaining = DeltaSeconds;
    // 返回剩余时间只允许跨出发和返航两个阶段，没有第二套任务/技能调度器。
    for (int32 Stage = 0; Stage < 2 && !bStopped && !bCompleting && !Owner->IsFinished(); ++Stage)
    {
        AActor* Avatar = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
        {
            Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
            return;
        }
        TOptional<FVector> Aim;
        if (bHoming)
        {
            FVector Position;
            if (!InitialTarget.IsSet() ||
                !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, InitialTarget.GetValue(), Position))
            {
                CompleteFlight();
                return;
            }
            Aim = Position;
        }
        FAbilityFlightSegment Segment;
        if (!AbilityProjectileMotion::ComputeFlightSegment(EffectiveFlight, Motion, Remaining,
                                                           Avatar->GetActorLocation(), Aim, Segment) ||
            // 阶段端点是纯motion给出的事实；tiny余量也可能坐标近似相等，但距离来源仍数百厘米。
            (bReturn && Motion.Phase == EAbilityProjectilePhase::Returning && Segment.bReachesPhaseEnd &&
             Segment.Start.Equals(Segment.End)))
        {
            CompleteFlight();
            return;
        }
        Motion.Direction = Segment.Direction;
        const bool bRotate = !bActor || bHoming || Motion.Phase == EAbilityProjectilePhase::Returning;
        const float Step = FVector::Distance(Segment.Start, Segment.End);
        FString Error;
        if (bActor)
        {
            TArray<FAbilityPrototypeFlightQueryHit> Hits;
            if (!AbilityPrototypeFlightQueries::GatherActorSegment(Input.SourceToken, Segment.Start, Segment.End,
                                                                   EffectiveFlight.CollisionRadius, Visited, Resolve,
                                                                   Hits, Error, PresentationHost))
            {
                Owner->Cancel(EAbilityGraphEndReason::Failed);
                return;
            }
            for (const auto& RawHit : Hits)
            {
                FAbilityPrototypeFlightQueryHit Hit;
                if (!AbilityPrototypeFlightQueries::ClassifyActorContact(Input.SourceToken, RawHit.Hit, Resolve, Hit,
                                                                         PresentationHost))
                {
                    continue;
                }
                Motion.Position = Hit.Hit.Location;
                RefreshPose(bRotate);
                if (Hit.Candidate.IsSet())
                {
                    const auto Admission = AbilityProjectileMotion::AdmitImpact(
                        EffectiveImpactPolicy, Motion.Phase, Visited.Num(), Visited.Contains(Hit.Candidate),
                        EffectiveImpactLimit, EffectiveDamageFalloff);
                    if (!Admission.bAccepted)
                    {
                        continue;
                    }
                    if (!Contact(Hit, Admission.DamageScale))
                    {
                        return;
                    }
                    Visited.Add(Hit.Candidate);
                    if (!Admission.bTerminateAfterImpact)
                    {
                        AbilityPrototypePresentationService::Impact(PresentationHost, ++ImpactId, Motion.Position);
                        continue;
                    }
                }
                CompleteFlight(true);
                return;
            }
        }
        else
        {
            FHitResult Wall;
            for (int32 Index = 0; Index < 64; ++Index)
            {
                FAbilityPrototypeFlightQueryHit Hit;
                if (!AbilityPrototypeFlightQueries::SweepMixedSegment(Input.SourceToken, Segment.Start, Segment.End,
                                                                      EffectiveFlight.CollisionRadius, Visited, Resolve,
                                                                      Hit, Wall, Error, PresentationHost))
                {
                    Owner->Cancel(EAbilityGraphEndReason::Failed);
                    return;
                }
                if (!Hit.Candidate.IsSet())
                {
                    break;
                }
                const auto Admission = AbilityProjectileMotion::AdmitImpact(
                    EffectiveImpactPolicy, Motion.Phase, Visited.Num(), Visited.Contains(Hit.Candidate),
                    EffectiveImpactLimit, EffectiveDamageFalloff);
                if (!Admission.bAccepted)
                {
                    break;
                }
                Motion.Position = Hit.Hit.Location;
                RefreshPose(bRotate);
                if (!Contact(Hit, Admission.DamageScale))
                {
                    return;
                }
                Visited.Add(Hit.Candidate);
                if (Admission.bTerminateAfterImpact)
                {
                    CompleteFlight();
                    return;
                }
            }
            if (Wall.bBlockingHit)
            {
                Motion.Position = Wall.Location;
                RefreshPose(bRotate);
                AbilityPrototypePresentationService::Impact(PresentationHost, ++ImpactId, Wall.ImpactPoint);
                CompleteFlight();
                return;
            }
        }
        Motion.Position = Segment.End;
        Motion.OutboundDistance += Step;
        RefreshPose(bRotate);
        if (bReturn)
        {
            if (Motion.Phase == EAbilityProjectilePhase::Returning && Segment.bReachesPhaseEnd)
            {
                CompleteFlight();
                return;
            }
            if (Motion.Phase == EAbilityProjectilePhase::Outbound && Motion.OutboundDistance >= EffectiveFlight.Range)
            {
                Motion.Phase = EAbilityProjectilePhase::Returning;
                Visited.Reset();
                RefreshPose(bRotate);
                Remaining = Segment.RemainingSeconds;
                if (Remaining > SMALL_NUMBER)
                {
                    continue;
                }
            }
        }
        else if (Motion.OutboundDistance >= EffectiveFlight.Range)
        {
            CompleteFlight();
        }
        return;
    }
}

void UAbilityPrototypeFlightNodeExecution::CompleteFlight(bool bCollisionImpact)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    const auto Payload = AbilityPrototypeFlightDetails::Position(Input, Motion);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Expired"), Payload);
    }
    Owner->FlushImmediatePayloads(true);
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    const bool bImpact = !AbilityPrototypeFlightDetails::ActorProfile(*Frame) || bCollisionImpact;
    AbilityPrototypePresentationService::FinishOriginal(PresentationHost, false, bImpact);
    Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeFlightDetails::PresentationSlot, PresentationHost);
    PresentationHost = nullptr;
    ReleasePhysicalLease();
    Owner->Publish(InvocationId, TEXT("Completed"), Payload);
}

void UAbilityPrototypeFlightNodeExecution::ReleasePhysicalLease()
{
    if (PhysicalLease.IsValid())
    {
        if (auto* Owner = GraphOwner.Get())
        {
            Owner->ReleasePhysicalSpawnLease(PhysicalLease);
        }
        PhysicalLease.Invalidate();
    }
}

void UAbilityPrototypeFlightNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost,
                                                            Reason != EAbilityGraphEndReason::Completed);
        if (auto* Owner = GraphOwner.Get(); Owner && Frame && PresentationHost)
        {
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeFlightDetails::PresentationSlot, PresentationHost);
        }
        PresentationHost = nullptr;
        Frame = nullptr;
        InitialTarget.Reset();
        Visited.Reset();
        ReleasePhysicalLease();
    }
    Super::Stop(Reason);
}

void AbilityPrototypeFlightNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Delivery.PrototypeFlight");
    Descriptor.ConfigClass = UAbilityPrototypeFlightNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPrototypeFlightNodeExecution::StaticClass();
    // 每次真实宿主生成都在Start通过当前InvocationId持有唯一物理lease，Stop归还并发资源。
    Descriptor.bRuntimeFiniteSafe = true;
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
    Descriptor.Ports = {{TEXT("Contact"), EAbilityPortPayload::TargetSet, false},
                        {TEXT("Period"), EAbilityPortPayload::Origin, false},
                        {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype flight registration: %s"), *Error);
    }
}
