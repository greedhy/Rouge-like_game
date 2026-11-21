// 调用链：冻结参数 -> 原profile时钟/共享最近目标查询 -> 同图活目标后继 -> 精确资源清理。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeEmitterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityPrototypeEmitterDetails
{
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    bool ActorProfile(const UAbilityPrototypeParameterFrame& Frame)
    {
        return Frame.GetProfile() == TEXT("PrototypeFine.Actor.v1");
    }

    /** 新投递不能继承祖先的命中/伤害事实；原请求的EventCenter没有被设置，保持零值。 */
    FAbilityNodeInput Request(const FAbilityNodeInput& Input, const TOptional<FAbilityTargetToken>& Target,
                              const FVector& Direction)
    {
        auto Result = Input;
        Result.Direction = Direction;
        Result.LiveTargetToken = Target;
        Result.TargetSet.Reset();
        Result.Impact.Reset();
        Result.EventCenter = FVector::ZeroVector;
        Result.bConfirmedHit = Result.bHasDamageResult = Result.bDamageAttempted = Result.bKilled = false;
        Result.DamageApplied.Reset();
        Result.ActualDamage.Reset();
        return Result;
    }
} // namespace AbilityPrototypeEmitterDetails

bool UAbilityPrototypeEmitterNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Admission != EAbilityPrototypeEmitterAdmission::LegacyGraphHelper)
    {
        Error = TEXT("LogicalScope emitter admission requires a separately registered logical-entry receipt contract");
    }
    return Error.IsEmpty();
}

bool UAbilityPrototypeEmitterNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                  const UAbilityNodeConfig& Configuration,
                                                  const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto* Settings = Cast<UAbilityPrototypeEmitterNodeConfig>(&Configuration);
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Settings || !Settings->Validate(Error) || !Frame ||
        Frame->GetParameters().Kind != EAbilityPrototypeKind::Emitter ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("Emitter requires a supported helper-admission contract and this root's Actor/Mixed frame");
        return false;
    }
    Age = Clock = 0.f;
    SuccessfulRequests = 0;
    bCompleting = false;
    Direction = Input.Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
    Current = Input.LiveTargetToken;
    if (!Current.IsSet() && Input.Impact.IsSet())
    {
        Current = Input.Impact->ConfirmedTarget;
    }
    Visual = {};
    Visual.bReady = true;
    Visual.Transform = FTransform(Direction.Rotation(), Input.Origin);
    Visual.BeamEnd = Input.Origin + Direction * Frame->GetParameters().Range;
    if (Current.IsSet())
    {
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Current.GetValue(), Visual.BeamEnd);
    }
    PresentationHost = AbilityPrototypePresentationService::Begin(
        Input.SourceToken, Frame->GetParameters(), Frame->GetStableReference(), Visual,
        AbilityPrototypeEmitterDetails::ActorProfile(*Frame) ? EAbilityPrototypePresentationProfile::Actor
                                                             : EAbilityPrototypePresentationProfile::Mixed,
        Error);
    if (!PresentationHost)
    {
        return false;
    }
    if (!Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeEmitterDetails::PresentationSlot, PresentationHost))
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, true);
        PresentationHost = nullptr;
        Error = TEXT("Emitter could not bind its exact presentation resource");
        return false;
    }
    return true;
}

void UAbilityPrototypeEmitterNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeEmitterNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeParameterFrame> FrameGuard(Frame.Get());
    const bool bActor = AbilityPrototypeEmitterDetails::ActorProfile(*Frame);
    const auto& Parameters = Frame->GetParameters();
    Age += DeltaSeconds;
    if (!bActor && Age >= Parameters.Duration)
    {
        CompleteEmitter();
        return;
    }
    const float ActiveDelta =
        bActor ? FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - (Age - DeltaSeconds))) : DeltaSeconds;
    AbilityBehaviorPrimitives::AdvanceCadence(
        Clock, ActiveDelta, Parameters.Period, bActor ? MAX_int32 : 8, EAbilityCadenceRemainder::Preserve,
        [this]()
        {
            return EmitScheduled();
        },
        bActor ? SMALL_NUMBER : 0.f, bActor);
    if (!bStopped && !bCompleting && !Owner->IsFinished() && Age >= Parameters.Duration)
    {
        CompleteEmitter();
    }
}

bool UAbilityPrototypeEmitterNodeExecution::EmitScheduled()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return false;
    }
    const bool bActor = AbilityPrototypeEmitterDetails::ActorProfile(*Frame);
    const auto& Parameters = Frame->GetParameters();
    auto Resolve = [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location,
                       FString& CaptureError)
    {
        // 原Actor GatherTargets在nearest的float距离之前有独立double中心半径筛选。
        return Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, CaptureError) &&
               (!bActor || FVector::DistSquared(Input.Origin, Location) <= FMath::Square(Parameters.Radius));
    };
    FAbilityTargetToken Selected;
    FVector Location;
    FString Error;
    const bool bQueried = AbilityPrototypeTargetServices::SelectChainNext(
        Input.SourceToken, Input.Origin, Parameters.Radius,
        bActor ? EAbilityPrototypeTargetProfile::Actor : EAbilityPrototypeTargetProfile::Mixed, {}, Resolve, Selected,
        Location, Error);
    if (!bQueried)
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return false;
    }
    if (!Selected.Target.IsSet())
    {
        Clock = bActor ? FMath::Fmod(Clock, Parameters.Period) : 0.f;
        return false;
    }
    Current = Selected;
    Direction = (Location - Input.Origin).GetSafeNormal(SMALL_NUMBER, bActor ? FVector::ForwardVector : Direction);
    Visual.Revision++;
    Visual.BeamEnd = Location;
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
    const auto Payload = AbilityPrototypeEmitterDetails::Request(Input, Current, Direction);
    if (Owner->RequestChild(InvocationId, TEXT("Launch"), Payload))
    {
        // 显式LegacyGraphHelper合同：只认图的helper预留，与Standalone实际Spawn返回值不同。
        // 子Scope后续是否拒绝以及如何拒绝，由其独立LogicalRejectionPolicy管理，不在此硬编码取消。
        if (++SuccessfulRequests >= Parameters.Count)
        {
            CompleteEmitter();
            return false;
        }
        return !bStopped && !Owner->IsFinished();
    }
    if (bActor)
    {
        Clock = FMath::Fmod(Clock, Parameters.Period);
        return false;
    }
    return !bStopped && !Owner->IsFinished();
}

void UAbilityPrototypeEmitterNodeExecution::CompleteEmitter()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    auto Live = Current;
    FVector Location;
    if (Live.IsSet() && !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Live.GetValue(), Location))
    {
        Live.Reset();
    }
    const auto Payload = AbilityPrototypeEmitterDetails::Request(Input, Live, Direction);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    if (!bStopped && !Owner->IsFinished())
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, false,
                                                            !AbilityPrototypeEmitterDetails::ActorProfile(*Frame));
        Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeEmitterDetails::PresentationSlot, PresentationHost);
        PresentationHost = nullptr;
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityPrototypeEmitterNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost,
                                                            Reason != EAbilityGraphEndReason::Completed);
        if (auto* Owner = GraphOwner.Get(); Owner && Frame && PresentationHost)
        {
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeEmitterDetails::PresentationSlot,
                                             PresentationHost);
        }
        PresentationHost = nullptr;
        Frame = nullptr;
        Current.Reset();
    }
    Super::Stop(Reason);
}

void AbilityPrototypeEmitterNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Control.PrototypeEmitter");
    Descriptor.ConfigClass = UAbilityPrototypeEmitterNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPrototypeEmitterNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
    Descriptor.Ports = {{TEXT("Launch"), EAbilityPortPayload::LiveTarget, false},
                        {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype emitter registration: %s"), *Error);
    }
}
