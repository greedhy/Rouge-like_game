// 调用链：图调度 -> 连锁/光束节点 -> 共用目标服务和同图 payload 步骤；不直接调用旧 Driver。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassAbility.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityPrototypeTargetDetails
{
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    /** 只承认实际实现的旧 profile，不把 Extension 或任意字符串当成 Mixed。 */
    bool IsActor(const UAbilityPrototypeParameterFrame& Frame)
    {
        return Frame.GetProfile() == TEXT("PrototypeFine.Actor.v1");
    }

    EAbilityPrototypeTargetProfile Profile(const UAbilityPrototypeParameterFrame& Frame)
    {
        return IsActor(Frame) ? EAbilityPrototypeTargetProfile::Actor : EAbilityPrototypeTargetProfile::Mixed;
    }

    TOptional<FAbilityTargetToken> InitialTarget(const FAbilityNodeInput& Input)
    {
        if (Input.LiveTargetToken.IsSet())
        {
            return Input.LiveTargetToken;
        }
        return Input.Impact.IsSet() ? TOptional<FAbilityTargetToken>(Input.Impact->ConfirmedTarget)
                                    : TOptional<FAbilityTargetToken>();
    }

    /** 不复制 TargetSet 的永久使用权限；位置端口可保留目标的原生命身份供后继显式复核。 */
    FAbilityNodeInput Position(const FAbilityNodeInput& Input, FVector Origin,
                               const TOptional<FAbilityTargetToken>& Target)
    {
        FAbilityNodeInput Result = Input;
        Result.Origin = Result.EventCenter = Origin;
        Result.TargetSet.Reset();
        Result.LiveTargetToken = Target;
        return Result;
    }

    FAbilityNodeInput ContactInput(const FAbilityNodeInput& Input, const FAbilityTargetToken& Target, FVector Origin,
                                   FVector EndPoint)
    {
        FAbilityNodeInput Result = Position(Input, Origin, {});
        Result.EventCenter = EndPoint;
        auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
        Targets->Source = Input.SourceToken;
        Targets->Targets.Add(Target);
        Result.TargetSet = MoveTemp(Targets);
        return Result;
    }

    /** Start/Stop 都用精确帧与宿主解绑，不按来源批量销毁其它片段的表现。 */
    void ClosePresentation(UAbilityGraphInstance* Owner, UAbilityPrototypeParameterFrame* Frame, AActor* Host,
                           bool bCancelled)
    {
        AbilityPrototypePresentationService::FinishOriginal(Host, bCancelled);
        if (Owner && Frame && Host)
        {
            Owner->UnregisterRuntimeResource(*Frame, PresentationSlot, Host);
        }
    }

    bool CreatePresentation(UAbilityGraphInstance& Owner, UAbilityPrototypeParameterFrame& Frame,
                            const FAbilityNodeInput& Input, const FAbilityPrototypePresentationFrame& Visual,
                            TObjectPtr<AActor>& Host, FString& Error)
    {
        Host = AbilityPrototypePresentationService::Begin(
            Input.SourceToken, Frame.GetParameters(), Frame.GetStableReference(), Visual,
            IsActor(Frame) ? EAbilityPrototypePresentationProfile::Actor : EAbilityPrototypePresentationProfile::Mixed,
            Error);
        if (!Host)
        {
            return false;
        }
        if (!Owner.RegisterRuntimeResource(Frame, PresentationSlot, Host))
        {
            AbilityPrototypePresentationService::FinishOriginal(Host, true);
            Host = nullptr;
            Error = TEXT("Target node could not bind its exact presentation resource to the prototype frame");
            return false;
        }
        return true;
    }
} // namespace AbilityPrototypeTargetDetails

bool UAbilityTargetSequenceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Configuration,
                                                const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || Frame->GetParameters().Kind != EAbilityPrototypeKind::Chain ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("TargetSequence requires this root's Actor/Mixed Chain parameter frame");
        return false;
    }
    Current = AbilityPrototypeTargetDetails::InitialTarget(Input);
    LastContact.Reset();
    Visited.Reset();
    ChainOrigin = Input.Origin;
    Age = Clock = 0.f;
    bCompleting = false;
    VisualFrame = {};
    VisualFrame.bReady = true;
    VisualFrame.Transform = FTransform(Input.Direction.Rotation(), ChainOrigin);
    VisualFrame.BeamStart = ChainOrigin;
    VisualFrame.BeamEnd = ChainOrigin + Input.Direction * Frame->GetParameters().Range;
    if (Current.IsSet())
    {
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Current.GetValue(), VisualFrame.BeamEnd);
    }
    return AbilityPrototypeTargetDetails::CreatePresentation(Owner, *Frame, Input, VisualFrame, PresentationHost,
                                                             Error);
}

void UAbilityTargetSequenceNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityTargetSequenceNodeExecution> SelfGuard(this);
    Age += DeltaSeconds;
    if (Age >= Frame->GetParameters().Duration)
    {
        CompleteSequence();
        return;
    }
    AbilityPrototypeTargetServices::AdvanceChainClock(Clock, DeltaSeconds, Frame->GetParameters().Period,
                                                      Visited.IsEmpty(), AbilityPrototypeTargetDetails::Profile(*Frame),
                                                      [this]()
                                                      {
                                                          return Contact();
                                                      });
}

bool UAbilityTargetSequenceNodeExecution::Contact()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return false;
    }
    const auto& Parameters = Frame->GetParameters();
    const auto Profile = AbilityPrototypeTargetDetails::Profile(*Frame);
    FVector TargetPoint;
    const float Reach = Visited.IsEmpty() ? Parameters.Range : Parameters.Radius;
    if (Visited.Num() >= Parameters.Count || !Current.IsSet() || Visited.Contains(Current->Target) ||
        !AbilityPrototypeTargetServices::ResolveChainContact(Input.SourceToken, Current.GetValue(), ChainOrigin, Reach,
                                                             Profile, TargetPoint))
    {
        CompleteSequence();
        return false;
    }
    const auto ContactTarget = Current.GetValue();
    auto Payload = AbilityPrototypeTargetDetails::ContactInput(Input, ContactTarget, ChainOrigin, TargetPoint);
    Payload.DamageScale *= FMath::Pow(Parameters.DamageFalloff, Visited.Num());
    VisualFrame.Revision++;
    VisualFrame.Transform.SetLocation(ChainOrigin);
    VisualFrame.BeamStart = ChainOrigin;
    VisualFrame.BeamEnd = TargetPoint;
    AbilityPrototypePresentationService::Update(PresentationHost, VisualFrame);
    if (!Owner->Publish(InvocationId, TEXT("Action"), Payload))
    {
        return false;
    }
    // Advance is outside the drainer. Flush settles ready payload actions, not asynchronous OnHit lifetimes.
    Owner->FlushPublishedEvents(true);
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return false;
    }
    Visited.Add(ContactTarget.Target);
    LastContact = ContactTarget;
    ChainOrigin = TargetPoint;
    FAbilityTargetToken Next;
    FVector NextPoint;
    FString Error;
    auto Resolve = [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location,
                       FString& CaptureError)
    {
        if (Profile == EAbilityPrototypeTargetProfile::Actor &&
            !APrototypeRuntime::IsEnemy(Input.SourceToken.Avatar.Get(), Candidate.Actor.Get()))
        {
            return false;
        }
        return Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, CaptureError);
    };
    if (!AbilityPrototypeTargetServices::SelectChainNext(Input.SourceToken, ChainOrigin, Frame->GetParameters().Radius,
                                                         Profile, Visited, Resolve, Next, NextPoint, Error))
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
        return false;
    }
    Current = Next.Target.IsSet() ? TOptional<FAbilityTargetToken>(Next) : TOptional<FAbilityTargetToken>();
    if (Profile == EAbilityPrototypeTargetProfile::Mixed &&
        (!Current.IsSet() || Visited.Num() >= Frame->GetParameters().Count))
    {
        CompleteSequence();
        return false;
    }
    return !bStopped && !bCompleting && !Owner->IsFinished();
}

void UAbilityTargetSequenceNodeExecution::CompleteSequence()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    const auto Target = AbilityPrototypeTargetDetails::IsActor(*Frame) && LastContact.IsSet() ? LastContact : Current;
    const auto Payload = AbilityPrototypeTargetDetails::Position(Input, ChainOrigin, Target);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Expired"), Payload);
    }
    Owner->FlushPublishedEvents(true);
    if (!bStopped && !Owner->IsFinished())
    {
        AbilityPrototypeTargetDetails::ClosePresentation(Owner, Frame, PresentationHost, false);
        PresentationHost = nullptr;
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityTargetSequenceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        AbilityPrototypeTargetDetails::ClosePresentation(GraphOwner.Get(), Frame, PresentationHost,
                                                         Reason != EAbilityGraphEndReason::Completed);
        PresentationHost = nullptr;
        Frame = nullptr;
        Current.Reset();
        LastContact.Reset();
        Visited.Reset();
    }
    Super::Stop(Reason);
}

bool UAbilityTargetChannelNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Configuration,
                                               const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || Frame->GetParameters().Kind != EAbilityPrototypeKind::Beam ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("TargetChannel requires this root's Actor/Mixed Beam parameter frame");
        return false;
    }
    Current = AbilityPrototypeTargetDetails::InitialTarget(Input);
    Age = Clock = ManaClock = 0.f;
    bCompleting = false;
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Error = TEXT("TargetChannel lost its original source before presentation creation");
        return false;
    }
    VisualFrame = {};
    VisualFrame.bReady = true;
    // 原 Actor 的请求起点在首次有效跟随前保留；Mixed 工厂一开始就强制贴近施法者。
    VisualFrame.BeamStart =
        AbilityPrototypeTargetDetails::IsActor(*Frame) ? Input.Origin : Avatar->GetActorLocation() + FVector(0, 0, 30);
    VisualFrame.Transform = FTransform(Input.Direction.Rotation(), VisualFrame.BeamStart);
    VisualFrame.BeamEnd = VisualFrame.BeamStart + Input.Direction * Frame->GetParameters().Range;
    if (Current.IsSet())
    {
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Current.GetValue(), VisualFrame.BeamEnd);
    }
    return AbilityPrototypeTargetDetails::CreatePresentation(Owner, *Frame, Input, VisualFrame, PresentationHost,
                                                             Error);
}

void UAbilityTargetChannelNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityTargetChannelNodeExecution> SelfGuard(this);
    const auto Profile = AbilityPrototypeTargetDetails::Profile(*Frame);
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    Age += DeltaSeconds;
    const bool bChargeThisFrame = Profile == EAbilityPrototypeTargetProfile::Mixed || ManaClock + DeltaSeconds >= 1.f;
    const float Cost =
        AbilityPrototypeTargetServices::ManaDue(ManaClock, DeltaSeconds, Frame->GetParameters().ManaPerSecond, Profile);
    // Use the original GE-backed spend actions. Their synchronous observers can cancel this exact invocation.
    const bool bSpent = !bChargeThisFrame ||
                        (Profile == EAbilityPrototypeTargetProfile::Actor ? APrototypeRuntime::SpendMana(Avatar, Cost)
                                                                          : UPureMassAbility::SpendMana(Avatar, Cost));
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return;
    }
    if (!bSpent)
    {
        Owner->Cancel(EAbilityGraphEndReason::Cancelled);
        return;
    }
    if (Age >= Frame->GetParameters().Duration)
    {
        CompleteChannel();
        return;
    }
    FVector SourcePoint;
    FVector TargetPoint;
    if (!Current.IsSet() ||
        !AbilityPrototypeTargetServices::ResolveBeam(Input.SourceToken, Current.GetValue(),
                                                     Frame->GetParameters().Range, Profile, SourcePoint, TargetPoint))
    {
        if (Profile == EAbilityPrototypeTargetProfile::Actor)
        {
            Owner->Cancel(EAbilityGraphEndReason::Cancelled);
        }
        else
        {
            CompleteChannel();
        }
        return;
    }
    VisualFrame.Revision++;
    VisualFrame.Transform.SetLocation(SourcePoint);
    VisualFrame.BeamStart = SourcePoint;
    VisualFrame.BeamEnd = TargetPoint;
    AbilityPrototypePresentationService::Update(PresentationHost, VisualFrame);
    AbilityPrototypeTargetServices::AdvanceBeamClock(Clock, DeltaSeconds, Frame->GetParameters().Period, Profile,
                                                     [this]()
                                                     {
                                                         return Pulse();
                                                     });
}

bool UAbilityTargetChannelNodeExecution::Pulse()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Current.IsSet() || !Owner || Owner->IsFinished())
    {
        return false;
    }
    // Range/LOS was checked once for this frame; the generic effect still validates original life per pulse.
    const auto Payload = AbilityPrototypeTargetDetails::ContactInput(Input, Current.GetValue(), VisualFrame.BeamStart,
                                                                     VisualFrame.BeamEnd);
    if (!Owner->Publish(InvocationId, TEXT("Action"), Payload))
    {
        return false;
    }
    Owner->FlushPublishedEvents(true);
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return false;
    }
    const auto Period = AbilityPrototypeTargetDetails::Position(Input, VisualFrame.BeamStart, Current);
    if (Owner->HasSuccessor(InvocationId, TEXT("Period")))
    {
        Owner->RequestChild(InvocationId, TEXT("Period"), Period);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Period"), Period);
    }
    Owner->FlushPublishedEvents(true);
    return !bStopped && !Owner->IsFinished();
}

void UAbilityTargetChannelNodeExecution::CompleteChannel()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    const auto Payload = AbilityPrototypeTargetDetails::Position(Input, VisualFrame.BeamStart, Current);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Expired"), Payload);
    }
    Owner->FlushPublishedEvents(true);
    if (!bStopped && !Owner->IsFinished())
    {
        AbilityPrototypeTargetDetails::ClosePresentation(Owner, Frame, PresentationHost, false);
        PresentationHost = nullptr;
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityTargetChannelNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        AbilityPrototypeTargetDetails::ClosePresentation(GraphOwner.Get(), Frame, PresentationHost,
                                                         Reason != EAbilityGraphEndReason::Completed);
        PresentationHost = nullptr;
        Frame = nullptr;
        Current.Reset();
    }
    Super::Stop(Reason);
}

void AbilityPrototypeTargetNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const bool bChain : {true, false})
    {
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = bChain ? TEXT("Control.TargetSequence") : TEXT("Control.TargetChannel");
        Descriptor.ConfigClass =
            bChain ? UAbilityTargetSequenceNodeConfig::StaticClass() : UAbilityTargetChannelNodeConfig::StaticClass();
        Descriptor.ExecutionClass = bChain ? UAbilityTargetSequenceNodeExecution::StaticClass()
                                           : UAbilityTargetChannelNodeExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
        // Beam只拥有一个准确帧下的固定表现宿主；有限事件/调用预算约束脉冲，Stop终止并归还该宿主。
        // Sequence尚未申请此合同，不因共享注册循环而隐式取得runtime-finite权限。
        Descriptor.bRuntimeFiniteSafe = !bChain;
        Descriptor.Ports = {{TEXT("Action"), EAbilityPortPayload::TargetSet, false},
                            {TEXT("Period"), EAbilityPortPayload::Origin, false},
                            {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype target registration: %s"), *Error);
        }
    }
}
