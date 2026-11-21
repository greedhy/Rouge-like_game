// 调用链：冻结帧 -> 原 profile 空间选择 -> 同图批 payload/逐原生命状态 -> 幂等表现清理。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeWaveNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/AbilitySelectedTargetService.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityPrototypeWaveDetails
{
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    bool ActorProfile(const UAbilityPrototypeParameterFrame& Parameters)
    {
        return Parameters.GetProfile() == TEXT("PrototypeFine.Actor.v1");
    }

    FAbilityNodeInput Position(const FAbilityNodeInput& InInput)
    {
        auto Result = InInput;
        Result.TargetSet.Reset();
        Result.LiveTargetToken.Reset();
        Result.EventCenter = Result.Origin;
        return Result;
    }
} // namespace AbilityPrototypeWaveDetails

bool UAbilityPrototypeWaveNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Configuration,
                                               const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || Frame->GetParameters().Kind != EAbilityPrototypeKind::Wave ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("Prototype wave requires this root's Actor/Mixed Wave frame");
        return false;
    }
    Age = Radius = PreviousRadius = PeriodClock = 0.f;
    NextStateTarget = 0;
    bStepPending = bFinishAfterStep = bCompleting = false;
    StateTargets.Reset();
    Admissions = MakeShared<FAbilitySelectionAdmissionLedger>(Owner.GetExecutionId(), Input.SourceToken);
    Visual = {};
    Visual.bReady = true;
    Visual.Transform = FTransform(Input.Direction.Rotation(), Input.Origin);
    PresentationHost = AbilityPrototypePresentationService::Begin(
        Input.SourceToken, Frame->GetParameters(), Frame->GetStableReference(), Visual,
        AbilityPrototypeWaveDetails::ActorProfile(*Frame) ? EAbilityPrototypePresentationProfile::Actor
                                                          : EAbilityPrototypePresentationProfile::Mixed,
        Error);
    if (!PresentationHost)
    {
        return false;
    }
    if (!Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeWaveDetails::PresentationSlot, PresentationHost))
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, true);
        PresentationHost = nullptr;
        Error = TEXT("Wave could not bind its exact presentation resource");
        return false;
    }
    return true;
}

void UAbilityPrototypeWaveNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || bStepPending || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeWaveNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeParameterFrame> FrameGuard(Frame.Get());
    const auto& Parameters = Frame->GetParameters();
    const bool bActor = AbilityPrototypeWaveDetails::ActorProfile(*Frame);
    const bool bFinalBoundary = !bActor && Parameters.IsA<UBatch6FieldPrototype>();
    float ActiveDelta = DeltaSeconds;
    if (bActor)
    {
        // 原Actor先相加Age再减回Delta；改变float表达式顺序会遗漏最后片段的真实目标。
        Age += DeltaSeconds;
        ActiveDelta = FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - (Age - DeltaSeconds)));
    }
    else
    {
        if (bFinalBoundary)
        {
            ActiveDelta = FMath::Min(DeltaSeconds, FMath::Max(0.f, Parameters.Duration - Age));
        }
        Age += ActiveDelta;
        if (!bFinalBoundary && Age >= Parameters.Duration)
        {
            FinishWave();
            return;
        }
        // 原Mixed先登记辅助周期，逻辑子片段由同一drainer延迟到本轮本体接触之后。
        AbilityBehaviorPrimitives::AdvanceCadence(
            PeriodClock, ActiveDelta, Parameters.Period, 8, EAbilityCadenceRemainder::Preserve,
            [&]()
            {
                if (Owner->HasSuccessor(InvocationId, TEXT("Period")))
                {
                    Owner->RequestChild(InvocationId, TEXT("Period"), AbilityPrototypeWaveDetails::Position(Input));
                }
                else
                {
                    Owner->Publish(InvocationId, TEXT("Period"), AbilityPrototypeWaveDetails::Position(Input));
                }
                return !bStopped && !Owner->IsFinished();
            });
    }
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    PreviousRadius = Radius;
    Radius = FMath::Min(Parameters.Radius, Radius + Parameters.Speed * ActiveDelta);
    Visual.Revision++;
    Visual.WaveRadius = Radius;
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
    bFinishAfterStep = Radius >= Parameters.Radius || Age >= Parameters.Duration;
    bStepPending = true;
    if (!SelectContacts())
    {
        if (!bStopped && !Owner->IsFinished())
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
        return;
    }
    ContinueStateTargets();
}

bool UAbilityPrototypeWaveNodeExecution::SelectContacts()
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    if (!Frame || !Owner || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return false;
    }
    const auto& Parameters = Frame->GetParameters();
    const bool bActor = AbilityPrototypeWaveDetails::ActorProfile(*Frame);
    const auto* Field = Cast<UBatch6FieldPrototype>(&Parameters);
    const bool bFront = bActor || (Field && Field->bWaveFrontOnly);
    TArray<FPureMassCombatTarget> Candidates;
    if (bActor)
    {
        TArray<AActor*> Actors;
        // 保留旧Actor物理broadphase及返回顺序，不能用Mixed解析范围查询替换。
        UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Input.SourceToken.World.Get(), Actors, {Avatar},
                                                            Parameters.Radius, Input.Origin);
        for (auto* Actor : Actors)
        {
            Candidates.Add(FPureMassCombatTarget::FromActor(Actor));
        }
    }
    else
    {
        Candidates = PureMassCombat::Gather(Input.SourceToken.World.Get(), Avatar, Input.Origin, Parameters.Radius);
    }
    auto Contacts = MakeShared<FAbilityTargetSetSnapshot>();
    Contacts->Source = Input.SourceToken;
    auto Receipt = MakeShared<FAbilitySelectionReceipt>();
    Receipt->Profile = bActor   ? EAbilitySelectionReceiptProfile::ActorWave
                       : bFront ? EAbilitySelectionReceiptProfile::MixedFrontWave
                                : EAbilitySelectionReceiptProfile::MixedWave;
    Receipt->Origin = Input.Origin;
    Receipt->MaximumRadius = Parameters.Radius;
    Receipt->PreviousRadius = PreviousRadius;
    Receipt->CurrentRadius = Radius;
    Receipt->IgnoredEffect = PresentationHost;
    Receipt->Step = FGuid::NewGuid();
    Receipt->Admissions = Admissions;
    Contacts->SelectionReceipt = MoveTemp(Receipt);
    StateTargets.Reset();
    NextStateTarget = 0;
    const bool bStateConsumer = Owner->HasSuccessor(InvocationId, TEXT("TargetFound"));
    for (const auto& Candidate : Candidates)
    {
        FAbilityTargetToken Token;
        FVector Location;
        FString Error;
        if (!Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, Error))
        {
            continue;
        }
        // 原Actor在完整半径中心过滤之前观察生命；Mixed的Gather已完成自己的broadphase。
        if ((bActor && FVector::DistSquared(Input.Origin, Location) > FMath::Square(Parameters.Radius)) ||
            Admissions->Contains(Candidate) ||
            (!bActor && !AbilityBehaviorPrimitives::ContainsWavePoint(Input.Origin, Location, PreviousRadius, Radius,
                                                                      EAbilityWaveCoverage::GrowingSphere3D)) ||
            (bFront && !AbilityBehaviorPrimitives::ContainsWavePoint(Input.Origin, Location, PreviousRadius, Radius,
                                                                     EAbilityWaveCoverage::SweptShell3D)) ||
            !(bActor ? AbilityPrototypeTargetServices::ActorLineOfSight(Input.SourceToken.World.Get(), Avatar,
                                                                        Candidate.Actor.Get(), Input.Origin, false,
                                                                        PresentationHost)
                     : PureMassCombat::Visible(Input.SourceToken.World.Get(), Avatar, Input.Origin, Candidate,
                                               PresentationHost)))
        {
            continue;
        }
        if (Contacts->Targets.Num() >= 65536)
        {
            return false;
        }
        Contacts->Targets.Add(Token);
        if (bStateConsumer)
        {
            StateTargets.Add(Token);
        }
    }
    StateSelection = Contacts;
    if (Owner->HasSuccessor(InvocationId, TEXT("Contact")))
    {
        auto Payload = AbilityPrototypeWaveDetails::Position(Input);
        Payload.TargetSet = Contacts;
        if (!Owner->Publish(InvocationId, TEXT("Contact"), Payload))
        {
            return false;
        }
        Owner->FlushImmediatePayloads(true);
    }
    return !bStopped && !Owner->IsFinished();
}

void UAbilityPrototypeWaveNodeExecution::ContinueStateTargets()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !bStepPending || !Owner || Owner->IsFinished())
    {
        return;
    }
    while (NextStateTarget < StateTargets.Num())
    {
        auto Payload = AbilityPrototypeWaveDetails::Position(Input);
        const auto Token = StateTargets[NextStateTarget++];
        // Contact可能致死/复用目标；状态端口只允许原生命，绝不重新捕获同地址新租约。
        if (!StateSelection || !AbilitySelectedTargetService::ResolveSelectedTarget(
                                   *Owner, Input.SourceToken, *StateSelection, Token, Payload.EventCenter))
        {
            continue;
        }
        Payload.LiveTargetToken = Token;
        const TWeakObjectPtr<UAbilityPrototypeWaveNodeExecution> WeakThis(this);
        if (!Owner->PublishWithContinuation(InvocationId, TEXT("TargetFound"), Payload,
                                            [WeakThis]()
                                            {
                                                if (auto* Self = WeakThis.Get())
                                                {
                                                    Self->ContinueStateTargets();
                                                }
                                            }))
        {
            Owner->Cancel(EAbilityGraphEndReason::Failed);
        }
        return;
    }
    FinishStep();
}

void UAbilityPrototypeWaveNodeExecution::FinishStep()
{
    StateTargets.Reset();
    StateSelection.Reset();
    NextStateTarget = 0;
    bStepPending = false;
    if (bFinishAfterStep)
    {
        FinishWave();
    }
}

void UAbilityPrototypeWaveNodeExecution::FinishWave()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner || Owner->IsFinished() || !Frame)
    {
        return;
    }
    bCompleting = true;
    auto Payload = AbilityPrototypeWaveDetails::Position(Input);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Expired"), Payload);
    }
    Owner->FlushImmediatePayloads(true);
    if (!bStopped && !Owner->IsFinished())
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, false,
                                                            !AbilityPrototypeWaveDetails::ActorProfile(*Frame));
        Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeWaveDetails::PresentationSlot, PresentationHost);
        PresentationHost = nullptr;
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityPrototypeWaveNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost,
                                                            Reason != EAbilityGraphEndReason::Completed);
        if (auto* Owner = GraphOwner.Get(); Owner && Frame && PresentationHost)
        {
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeWaveDetails::PresentationSlot, PresentationHost);
        }
        PresentationHost = nullptr;
        Frame = nullptr;
        StateTargets.Reset();
        StateSelection.Reset();
        Admissions.Reset();
        bStepPending = false;
    }
    Super::Stop(Reason);
}

void AbilityPrototypeWaveNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Spatial.PrototypeWave");
    Descriptor.ConfigClass = UAbilityPrototypeWaveNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPrototypeWaveNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Descriptor.bIsAsync = true;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bPrototypeCompatibilitySafe = true;
    Descriptor.Ports = {{TEXT("Contact"), EAbilityPortPayload::TargetSet, false},
                        {TEXT("TargetFound"), EAbilityPortPayload::LiveTarget, false},
                        {TEXT("Period"), EAbilityPortPayload::Origin, false},
                        {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype wave registration: %s"), *Error);
    }
}
