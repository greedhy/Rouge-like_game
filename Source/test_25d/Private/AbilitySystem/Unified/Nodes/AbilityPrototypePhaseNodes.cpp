// 调用链：细节点图 -> Phase Start/Advance -> 同一图的步骤屏障 -> Query/Effect 后继 -> Stop 表现清理。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePhaseNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityTargetQueryService.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeSurfaceService.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "GameFramework/Actor.h"

FName AbilityPrototypePhaseNodes::TypeId(EAbilityPrototypePhase Phase)
{
    switch (Phase)
    {
    case EAbilityPrototypePhase::Lifetime:
        return TEXT("Control.PrototypeLifetime");
    case EAbilityPrototypePhase::Field:
        return TEXT("Control.FieldCadence");
    case EAbilityPrototypePhase::Trap:
        return TEXT("Control.PrototypeTrap");
    case EAbilityPrototypePhase::Delay:
        return TEXT("Control.PrototypeDelay");
    case EAbilityPrototypePhase::ExtensionBurst:
        return TEXT("Control.ExtensionBurst");
    case EAbilityPrototypePhase::ExtensionDelay:
        return TEXT("Control.ExtensionDelay");
    case EAbilityPrototypePhase::Surface:
        return TEXT("Control.PrototypeSurfaceClock");
    default:
        return NAME_None;
    }
}

bool UAbilityPrototypePhaseNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (AbilityPrototypePhaseNodes::TypeId(Phase).IsNone())
    {
        Error = TEXT("Prototype phase has no registered finite-time contract.");
    }
    return Error.IsEmpty();
}

bool UAbilityPrototypePhaseNodeConfig::MatchesType(FName Type) const
{
    return Type == AbilityPrototypePhaseNodes::TypeId(Phase);
}

bool UAbilityPrototypePhaseNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Settings = Cast<UAbilityPrototypePhaseNodeConfig>(&Config);
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Settings || !Settings->Validate(Error) || !Frame)
    {
        return false;
    }
    Phase = Settings->Phase;
    const auto& Parameters = Frame->GetParameters();
    const auto Kind = Parameters.Kind;
    const auto* Extension = Cast<UExtensionNode>(&Parameters);
    const bool bValidKind =
        (Phase == EAbilityPrototypePhase::ExtensionBurst && Extension &&
         Extension->Behavior == EExtensionKind::Burst) ||
        (Phase == EAbilityPrototypePhase::ExtensionDelay && Extension &&
         Extension->Behavior == EExtensionKind::Delay) ||
        (Phase == EAbilityPrototypePhase::Field && Kind == EAbilityPrototypeKind::Field) ||
        (Phase == EAbilityPrototypePhase::Trap && Kind == EAbilityPrototypeKind::Trap) ||
        (Phase == EAbilityPrototypePhase::Delay && Kind == EAbilityPrototypeKind::Delay) ||
        (Phase == EAbilityPrototypePhase::Surface && Kind == EAbilityPrototypeKind::Surface && !Extension) ||
        (Phase == EAbilityPrototypePhase::Lifetime &&
         (Kind == EAbilityPrototypeKind::Burst || Kind == EAbilityPrototypeKind::Cone ||
          Kind == EAbilityPrototypeKind::Ring || Kind == EAbilityPrototypeKind::Line));
    const auto* Field = Cast<UBatch6FieldPrototype>(&Parameters);
    const bool bFollowRoot = Field && Field->bFollowCasterToggle && Phase == EAbilityPrototypePhase::Field &&
                             Frame->GetProfile() == TEXT("PrototypeFine.Mixed.v1") && Frame->GetLogicalDepth() == 0;
    if (!bValidKind || bFollowRoot)
    {
        Error = TEXT("Finite prototype phase cannot execute this kind or the separate sustained-field contract.");
        return false;
    }
    Age = Clock = 0.f;
    bCompleting = false;
    {
        FAbilityPrototypePresentationFrame Visual;
        Visual.bReady = true;
        Visual.Transform = FTransform(Input.Direction.Rotation(), Input.Origin);
        const bool bActor = Frame->GetProfile() == TEXT("PrototypeFine.Actor.v1");
        PresentationHost = AbilityPrototypePresentationService::Begin(
            Input.SourceToken, Parameters, Frame->GetStableReference(), Visual,
            Extension ? EAbilityPrototypePresentationProfile::Extension
            : bActor  ? EAbilityPrototypePresentationProfile::Actor
                      : EAbilityPrototypePresentationProfile::Mixed,
            Error);
        if (!PresentationHost)
        {
            return false;
        }
        if (!Owner.RegisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), PresentationHost))
        {
            ReleasePresentation(true);
            Error = TEXT("Prototype phase could not publish its exact presentation resource.");
            return false;
        }
    }
    if (!Owner.RegisterRuntimeResource(*Frame, TEXT("PrototypePhase"), this))
    {
        ReleasePresentation(true);
        Error = TEXT("Prototype phase could not publish its exact payload-completion owner.");
        return false;
    }
    if (bStopped || Owner.IsFinished())
    {
        ReleasePresentation(true);
        return true;
    }
    if (Phase == EAbilityPrototypePhase::Lifetime || Phase == EAbilityPrototypePhase::ExtensionBurst)
    {
        const bool bPublished = PublishStep(TEXT("Action"));
        return bPublished || bStopped;
    }
    return true;
}

bool UAbilityPrototypePhaseNodeExecution::PublishStep(FName Port)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return false;
    }
    FAbilityNodeInput Payload = Input;
    if (Phase != EAbilityPrototypePhase::Surface)
    {
        Payload.LiveTargetToken.Reset();
    }
    Payload.TargetSet.Reset();
    Payload.EventCenter = Phase == EAbilityPrototypePhase::Surface ? FVector::ZeroVector : Payload.Origin;
    if (Owner->HasSuccessor(InvocationId, Port))
    {
        if (Port == TEXT("Action"))
        {
            if (!Owner->Publish(InvocationId, Port, Payload))
            {
                return false;
            }
        }
        else
        {
            // 原Emit的可选孩子预准入失败不结束父时钟；本体Action仍是普通硬预算发布。
            Owner->RequestChild(InvocationId, Port, Payload);
        }
    }
    if (Port == TEXT("Action"))
    {
        // 原Driver先完成本次Pulse（包括中心表现），返回后才执行排队的OnHit孩子。
        // 仍使用同一drainer：先结算本体辅助节点，把logical entry暂存到本体动作之后。
        Owner->FlushImmediatePayloads(true);
    }
    else
    {
        Owner->FlushPublishedEvents(true);
    }
    return !bStopped && !Owner->IsFinished();
}

bool UAbilityPrototypePhaseNodeExecution::HasTrapTarget(FString& Error)
{
    auto* Owner = GraphOwner.Get();
    if (!Owner || !Frame)
    {
        return false;
    }
    const auto& Parameters = Frame->GetParameters();
    FAbilityQueryRequest Request;
    Request.Origin = Input.Origin;
    Request.Radius = Parameters.Radius;
    Request.MaxTargets = 65536;
    Request.bIncludeMass = Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1");
    Request.Profile =
        Request.bIncludeMass ? EAbilityShapeQueryProfile::LegacyMixed : EAbilityShapeQueryProfile::LegacyActor;
    // 原 Actor Trap 只需范围内活候选；Mixed 的 Nearest 需要可见性。
    Request.bRequireVisibility = Request.bIncludeMass;
    TArray<FAbilityTargetToken> Targets;
    auto Resolve = [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Token, FVector& Location,
                       FString& CaptureError)
    {
        return Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, CaptureError);
    };
    return AbilityTargetQueryService::GatherBatch(Input.SourceToken, Request, Resolve, Targets, Error) &&
           !Targets.IsEmpty();
}

void UAbilityPrototypePhaseNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner || !Frame || Owner->IsFinished())
    {
        return;
    }
    const auto& Parameters = Frame->GetParameters();
    if (Phase == EAbilityPrototypePhase::ExtensionBurst)
    {
        return;
    }
    const bool bActor = Frame->GetProfile() == TEXT("PrototypeFine.Actor.v1");
    const bool bFinalField =
        Phase == EAbilityPrototypePhase::Field && (bActor || Parameters.IsA<UBatch6FieldPrototype>());
    // 保留原 Actor 的 Age+=Delta 后再计算剩余部分的 float 顺序。
    if (bFinalField && !bActor)
    {
        DeltaSeconds = FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - Age));
    }
    Age += DeltaSeconds;
    if (Phase == EAbilityPrototypePhase::Delay || Phase == EAbilityPrototypePhase::ExtensionDelay)
    {
        if (Age >= Parameters.ArmDelay)
        {
            CompletePhase();
        }
        return;
    }
    if (!bFinalField && Age >= Parameters.Duration)
    {
        CompletePhase();
        return;
    }
    if (Phase == EAbilityPrototypePhase::Trap)
    {
        FString Error;
        if (Age >= Parameters.ArmDelay && HasTrapTarget(Error))
        {
            if (PublishStep(TEXT("Action")))
            {
                CompletePhase();
            }
        }
        else if (!Error.IsEmpty())
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
        return;
    }
    if (Phase == EAbilityPrototypePhase::Surface)
    {
        AbilityPrototypeSurfaceService::AdvanceClock(Clock, DeltaSeconds, Parameters.Period,
                                                     bActor ? EAbilityPrototypeSurfaceProfile::Actor
                                                            : EAbilityPrototypeSurfaceProfile::Mixed,
                                                     [&]()
                                                     {
                                                         return PublishStep(TEXT("Period"));
                                                     });
        return;
    }
    if (Phase == EAbilityPrototypePhase::Field)
    {
        const float ActiveDelta =
            bActor ? FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - (Age - DeltaSeconds)))
                   : DeltaSeconds;
        AbilityBehaviorPrimitives::AdvanceCadence(
            Clock, ActiveDelta, Parameters.Period, bActor ? MAX_int32 : 8, EAbilityCadenceRemainder::Preserve,
            [&]()
            {
                return PublishStep(TEXT("Action")) && PublishStep(TEXT("Period"));
            },
            bActor ? KINDA_SMALL_NUMBER : 0.f);
        if (bFinalField && Age >= Parameters.Duration && !bStopped && !Owner->IsFinished())
        {
            CompletePhase();
        }
        return;
    }
    // Burst 不发辅助周期；原 Actor Cone/Ring/Line 也不发，Mixed 的三个形状保留8次追赶。
    if (!bActor && Parameters.Kind != EAbilityPrototypeKind::Burst)
    {
        AbilityBehaviorPrimitives::AdvanceCadence(Clock, DeltaSeconds, Parameters.Period, 8,
                                                  EAbilityCadenceRemainder::Preserve,
                                                  [&]()
                                                  {
                                                      return PublishStep(TEXT("Period"));
                                                  });
    }
}

void UAbilityPrototypePhaseNodeExecution::CompletePhase()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    if (Phase == EAbilityPrototypePhase::Surface)
    {
        // 原Driver先关资源/表现，再由迭代队列执行已经接纳的到期孩子。
        ReleasePresentation(false);
    }
    if (!PublishStep(TEXT("Expired")))
    {
        return;
    }
    ReleasePresentation(false);
    // 没有业务完成端口也必须结束时钟；图会等其余已接纳的异步分支各自完成。
    if (Owner->HasSuccessor(InvocationId, TEXT("Completed")))
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
    else
    {
        Owner->FinishInvocation(InvocationId);
    }
}

void UAbilityPrototypePhaseNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        if (auto* Owner = GraphOwner.Get(); Owner && Frame)
        {
            Owner->UnregisterRuntimeResource(*Frame, TEXT("PrototypePhase"), this);
        }
        ReleasePresentation(Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired);
        Frame = nullptr;
    }
    Super::Stop(Reason);
}

void UAbilityPrototypePhaseNodeExecution::ReleasePresentation(bool bCancelled)
{
    if (auto* Owner = GraphOwner.Get(); Owner && Frame && PresentationHost)
    {
        Owner->UnregisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), PresentationHost);
    }
    const bool bExtension = Frame && Frame->GetProfile() == TEXT("PrototypeFine.Extension.v1");
    AbilityPrototypePresentationService::FinishOriginal(PresentationHost, bCancelled, bExtension);
    PresentationHost = nullptr;
}

void UAbilityPrototypePhaseNodeExecution::CompleteFromPayload(const UAbilityPrototypeParameterFrame& CompletedFrame)
{
    if (Frame == &CompletedFrame && Phase == EAbilityPrototypePhase::ExtensionBurst)
    {
        CompletePhase();
    }
}

bool UAbilityPrototypeEndNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    if (!Frame)
    {
        return false;
    }
    if (auto* PhaseOwner =
            Cast<UAbilityPrototypePhaseNodeExecution>(Owner.FindRuntimeResource(*Frame, TEXT("PrototypePhase"))))
    {
        PhaseOwner->CompleteFromPayload(*Frame);
    }
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityPrototypePhaseNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (!Registry.Find(TEXT("Control.PrototypeEnd")))
    {
        FAbilityNodeDescriptor End;
        End.TypeId = TEXT("Control.PrototypeEnd");
        End.ConfigClass = UAbilityPrototypeEndNodeConfig::StaticClass();
        End.ExecutionClass = UAbilityPrototypeEndNodeExecution::StaticClass();
        End.InputPayload = EAbilityPortPayload::Origin;
        End.bSupportsActorTargets = End.bSupportsMassTargets = End.bSupportsSnapshotTargets = true;
        End.bPrototypeCompatibilitySafe = true;
        End.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        ensureAlwaysMsgf(Registry.Register(End, Error), TEXT("Prototype end registration: %s"), *Error);
    }
    for (auto Phase : {EAbilityPrototypePhase::Lifetime, EAbilityPrototypePhase::Field, EAbilityPrototypePhase::Trap,
                       EAbilityPrototypePhase::Delay, EAbilityPrototypePhase::ExtensionBurst,
                       EAbilityPrototypePhase::ExtensionDelay, EAbilityPrototypePhase::Surface})
    {
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = TypeId(Phase);
        Descriptor.ConfigClass = UAbilityPrototypePhaseNodeConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityPrototypePhaseNodeExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
        Descriptor.Ports = {{TEXT("Action"), EAbilityPortPayload::Origin, false},
                            {TEXT("Period"), EAbilityPortPayload::Origin, false},
                            {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype phase registration: %s"), *Error);
        }
    }
}
