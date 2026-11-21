// 调用链：默认配方导出/行为测试 -> Build；真实节点须先通过旧动作对照后才登记。
#include "AbilitySystem/Unified/Nodes/AbilityLightningFragments.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    const FName HostSlot(TEXT("Lightning.FiniteSteps"));

    /** 帧只授予同根资源索引；跨施法、已完成步骤或停止宿主均无法重新借用。 */
    UAbilityLightningStepsExecution* HostFor(UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input,
                                             const UAbilityLightningStepFrame* Step, FString& Error)
    {
        auto* Host = Step && Step->BelongsTo(Owner.GetExecutionId(), Input.SourceToken)
                         ? Cast<UAbilityLightningStepsExecution>(Owner.FindRuntimeResource(*Step, HostSlot))
                         : nullptr;
        if (!Host)
        {
            Error = TEXT("Lightning fragment requires its current original-root step frame.");
        }
        return Host;
    }

    /** 先完整校验，再允许任何GE；同源新生命和重复条目不能借公开集合扩大已捕获的命中权限。 */
    bool CapturedSubset(const UAbilityLightningTargetsFrame& Frame, const FAbilityNodeInput& Input,
                        TArray<FAbilityLightningSelectedTarget>& Out, FString& Error)
    {
        if (!Input.TargetSet)
        {
            Error = TEXT("Lightning payload/cue requires its public typed target set.");
            return false;
        }
        for (const auto& Target : Input.TargetSet->Targets)
        {
            const auto* Original = Frame.GetCapturedTargets().FindByPredicate(
                [&Target](const FAbilityLightningSelectedTarget& Selection)
                {
                    return AbilityBehaviorPrimitives::SameTargetLife(Selection.Target, Target);
                });
            const bool bDuplicate = Out.ContainsByPredicate(
                [&Target](const FAbilityLightningSelectedTarget& Selection)
                {
                    return AbilityBehaviorPrimitives::SameTargetLife(Selection.Target, Target);
                });
            if (!Original || bDuplicate)
            {
                Error = TEXT("Lightning target set injects an uncaptured or duplicate original life.");
                return false;
            }
            Out.Add(*Original);
        }
        return true;
    }
} // namespace

bool UAbilityLightningControlConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (MaxTargets < 1 || MaxTargets > 128 || !FMath::IsFinite(JumpDelay) ||
        (bSequential && (JumpDelay < .01f || JumpDelay > 1.f)))
    {
        Error = TEXT("Finite lightning supports 1-128 targets and original sequential delay [.01,1].");
    }
    return Error.IsEmpty();
}

bool UAbilityLightningControlConfig::SupportsSustainedRoot(FString& Error) const
{
    return Validate(Error);
}

bool UAbilityLightningQueryConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(JumpRadius) || JumpRadius <= 0.f || JumpRadius > 2000.f)
    {
        Error = TEXT("Lightning query radius must be in (0,2000].");
    }
    return Error.IsEmpty();
}

bool UAbilityLightningQueryConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    const float Product = JumpRadius * Multiplier;
    if (Parameter != EAbilityConfigScalar::Radius || !FMath::IsFinite(Multiplier) || Multiplier <= 0.f ||
        !FMath::IsFinite(Product) || Product <= 0.f || Product > 2000.f)
    {
        return false;
    }
    JumpRadius = Product;
    return true;
}

bool UAbilityLightningDamageConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageFalloff) || DamageFalloff < 0.f || DamageFalloff > 1.f ||
        !FMath::IsFinite(DamageMultiplier) || DamageMultiplier < 0.f)
    {
        Error = TEXT("Lightning payload needs falloff [0,1] and a finite nonnegative main multiplier.");
    }
    return Error.IsEmpty();
}

bool UAbilityLightningDamageConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    const float Product = DamageMultiplier * Multiplier;
    if (Parameter != EAbilityConfigScalar::Damage || !FMath::IsFinite(Multiplier) || Multiplier < 0.f ||
        !FMath::IsFinite(Product))
    {
        return false;
    }
    DamageMultiplier = Product;
    return true;
}

bool UAbilityLightningCueConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Lifetime) || Lifetime <= 0.f)
    {
        Error = TEXT("Lightning beam lifetime must be finite and positive.");
    }
    return Error.IsEmpty();
}

const UAbilityLightningStepFrame* UAbilityLightningTargetsFrame::GetStep() const
{
    return Step;
}

const TArray<FAbilityLightningSelectedTarget>& UAbilityLightningTargetsFrame::GetCapturedTargets() const
{
    return CapturedFrame ? CapturedFrame->GetCapturedTargets() : Targets;
}

bool UAbilityLightningStepsExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    const TStrongObjectPtr<UAbilityLightningStepsExecution> Guard(this);
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = CastChecked<UAbilityLightningControlConfig>(const_cast<UAbilityNodeConfig*>(&Config));
    FVector FirstLocation;
    if (!Settings->Validate(Error) || !Input.LiveTargetToken.IsSet() ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), FirstLocation))
    {
        Error = TEXT("Lightning control requires its original initial live target.");
        return false;
    }
    return EmitStep(Error);
}

bool UAbilityLightningStepsExecution::EmitStep(FString& Error)
{
    auto* Owner = GraphOwner.Get();
    AActor* Caster = nullptr;
    if (bStopped || !Owner || Owner->IsFinished() || CurrentFrame ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        Error = TEXT("Lightning step cannot start after source loss or while another step is active.");
        return false;
    }
    CurrentFrame = NewObject<UAbilityLightningStepFrame>(this);
    CurrentFrame->BindExecution(Owner->GetExecutionId(), Input.SourceToken);
    CurrentFrame->First = Input.LiveTargetToken.GetValue();
    CurrentFrame->Visited = Visited;
    CurrentFrame->From = Sequence == 0 ? Caster->GetActorLocation() : LastImpact;
    CurrentFrame->Sequence = Sequence;
    CurrentFrame->MaxTargets = Settings->MaxTargets;
    CurrentFrame->bSequential = Settings->bSequential;
    bSelectionAccepted = false;
    bEmptySelection = true;
    bWaiting = false;
    PendingResults.Reset();
    if (!Owner->RegisterRuntimeResource(*CurrentFrame, HostSlot, this))
    {
        CurrentFrame = nullptr;
        Error = TEXT("Lightning step resource registration failed.");
        return false;
    }
    auto Payload = Input;
    Payload.Origin = CurrentFrame->From;
    Payload.LiveTargetToken.Reset();
    Payload.ParameterFrame = CurrentFrame;
    const TWeakObjectPtr<UAbilityLightningStepsExecution> Weak(this);
    if (!Owner->PublishWithContinuation(InvocationId, TEXT("Step"), Payload,
                                        [Weak]()
                                        {
                                            if (auto* Host = Weak.Get())
                                            {
                                                Host->StepFinished();
                                            }
                                        }))
    {
        return false;
    }
    // 同一drainer同步本体；128个独立观测结果可以按原64事件帧额度继续排空，不放大帧预算。
    Owner->FlushPublishedEvents(false);
    return true;
}

bool UAbilityLightningStepsExecution::AcceptSelection(const UAbilityLightningStepFrame& Frame,
                                                      const TArray<FAbilityLightningSelectedTarget>& Targets)
{
    if (bStopped || CurrentFrame != &Frame || bSelectionAccepted)
    {
        return false;
    }
    bSelectionAccepted = true;
    bEmptySelection = Targets.IsEmpty();
    for (const auto& Selection : Targets)
    {
        Visited.Add(Selection.Target.Target);
        LastImpact = Selection.Position;
    }
    return true;
}

void UAbilityLightningStepsExecution::RecordDamage(const UAbilityLightningStepFrame& Frame, FAbilityNodeInput Payload)
{
    if (!bStopped && CurrentFrame == &Frame)
    {
        LastImpact = Payload.Origin;
        Payload.ParameterFrame = CurrentFrame;
        PendingResults.Add(MoveTemp(Payload));
    }
}

void UAbilityLightningStepsExecution::StepFinished()
{
    const TStrongObjectPtr<UAbilityLightningStepsExecution> Guard(this);
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !CurrentFrame)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityLightningStepFrame> FrameGuard(CurrentFrame.Get());
    Owner->UnregisterRuntimeResource(*CurrentFrame, HostSlot, this);
    CurrentFrame = nullptr;
    auto Results = MoveTemp(PendingResults);
    // 宿主不属于Step continuation；附加的延迟OnHit具有自身寿命，不能改变原闪电节拍。
    for (const auto& Result : Results)
    {
        if (bStopped || Owner->IsFinished() || !Owner->Publish(InvocationId, TEXT("DamageResolved"), Result))
        {
            return;
        }
    }
    if (!bSelectionAccepted)
    {
        Owner->FinishInvocation(InvocationId, EAbilityGraphEndReason::TargetLost);
        return;
    }
    if (!Settings->bSequential || bEmptySelection || Visited.Num() >= Settings->MaxTargets)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
        return;
    }
    ++Sequence;
    TimeUntilJump = Settings->JumpDelay;
    bWaiting = true;
}

void UAbilityLightningStepsExecution::Advance(float DeltaSeconds)
{
    const TStrongObjectPtr<UAbilityLightningStepsExecution> Guard(this);
    if (bStopped || !bWaiting || DeltaSeconds <= 0.f)
    {
        return;
    }
    TimeUntilJump -= DeltaSeconds;
    if (TimeUntilJump <= UE_SMALL_NUMBER)
    {
        FString Error;
        if (!EmitStep(Error))
        {
            if (auto* Owner = GraphOwner.Get(); Owner && !Owner->IsFinished())
            {
                Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
            }
        }
    }
}

void UAbilityLightningStepsExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (CurrentFrame)
    {
        if (auto* Owner = GraphOwner.Get())
        {
            Owner->UnregisterRuntimeResource(*CurrentFrame, HostSlot, this);
        }
    }
    CurrentFrame = nullptr;
    PendingResults.Reset();
    Visited.Reset();
    bWaiting = false;
    Super::Stop(Reason);
}

bool UAbilityLightningTargetsExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    auto* Step = Cast<UAbilityLightningStepFrame>(Input.ParameterFrame.Get());
    auto* Host = HostFor(Owner, Input, Step, Error);
    if (!Host)
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityLightningQueryConfig>(&Config);
    auto* Frame = NewObject<UAbilityLightningTargetsFrame>(this);
    Frame->BindExecution(Owner.GetExecutionId(), Input.SourceToken);
    Frame->Step = Step;
    if (Step->Sequence == 0)
    {
        FVector Position;
        if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Step->First, Position))
        {
            Frame->Targets.Add({Step->First, Position});
            if (!Step->bSequential && Step->MaxTargets > 1)
            {
                auto Excluded = Step->Visited;
                Excluded.Add(Step->First.Target);
                const auto Others = AbilityLightningActions::FindTargets(
                    Input.SourceToken, Position, Step->From, Settings.JumpRadius, Excluded, Step->MaxTargets - 1);
                for (const auto& Target : Others)
                {
                    if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Position))
                    {
                        Frame->Targets.Add({Target, Position});
                    }
                }
            }
        }
    }
    else
    {
        const auto Next = AbilityLightningActions::FindTargets(Input.SourceToken, Step->From, Step->From,
                                                               Settings.JumpRadius, Step->Visited, 1);
        for (const auto& Target : Next)
        {
            FVector Position;
            if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, Position))
            {
                Frame->Targets.Add({Target, Position});
            }
        }
    }
    if (!Host->AcceptSelection(*Step, Frame->Targets))
    {
        Error = TEXT("Lightning step selection has already been accepted or retired.");
        return false;
    }
    auto Set = MakeShared<FAbilityTargetSetSnapshot>();
    Set->Source = Input.SourceToken;
    for (const auto& Selection : Frame->Targets)
    {
        Set->Targets.Add(Selection.Target);
    }
    auto Payload = Input;
    Payload.TargetSet = Set;
    Payload.ParameterFrame = Frame;
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}

bool UAbilityLightningDamageExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    const TStrongObjectPtr<UAbilityLightningDamageExecution> Guard(this);
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = Cast<UAbilityLightningTargetsFrame>(Input.ParameterFrame.Get());
    const auto* Step = Frame ? Frame->GetStep() : nullptr;
    auto* Host = HostFor(Owner, Input, Step, Error);
    if (!Host || !Frame || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken))
    {
        return false;
    }
    TArray<FAbilityLightningSelectedTarget> Selected;
    if (!CapturedSubset(*Frame, Input, Selected, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityLightningDamageConfig>(&Config);
    auto* ResultFrame = NewObject<UAbilityLightningTargetsFrame>(this);
    const TStrongObjectPtr<UAbilityLightningTargetsFrame> ResultGuard(ResultFrame);
    ResultFrame->BindExecution(Owner.GetExecutionId(), Input.SourceToken);
    ResultFrame->Step = Frame->Step;
    ResultFrame->CapturedFrame = const_cast<UAbilityLightningTargetsFrame*>(Frame);
    ResultFrame->bDamageReceipt = true;
    for (const auto& Selection : Selected)
    {
        FVector Position;
        AActor* Caster = nullptr;
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
        {
            return true;
        }
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Selection.Target, Position))
        {
            continue;
        }
        const bool bPrimary = AbilityBehaviorPrimitives::SameTargetLife(Selection.Target, Step->First);
        const float Exponent = Step->bSequential ? static_cast<float>(Step->Sequence) : (bPrimary ? 0.f : 1.f);
        const float Scale =
            Input.DamageScale * Settings.DamageMultiplier * FMath::Pow(Settings.DamageFalloff, Exponent);
        if (!FMath::IsFinite(Scale) || !FMath::IsFinite(Input.DamageParams.BaseDamage * Scale))
        {
            Error = TEXT("Lightning cumulative main damage is nonfinite.");
            return false;
        }
        const auto Result =
            AbilityLightningActions::ApplyHit(Input.SourceToken, Selection.Target, Input.DamageParams, Scale);
        if (bStopped || Owner.IsFinished() || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
        {
            return true;
        }
        ResultFrame->Targets.Add({Selection.Target, Position});
        auto ResultInput = Input;
        ResultInput.DamageScale = Scale;
        Host->RecordDamage(
            *Step, AbilityLightningActions::DamagePayload(ResultInput, Selection.Target, Step->From, Position, Result));
    }
    auto Payload = Input;
    Payload.ParameterFrame = ResultFrame;
    return Owner.Publish(Id, TEXT("Completed"), Payload);
}

bool UAbilityLightningCueExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                          const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                          FString& Error)
{
    const TStrongObjectPtr<UAbilityLightningCueExecution> Guard(this);
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = Cast<UAbilityLightningTargetsFrame>(Input.ParameterFrame.Get());
    const auto* Step = Frame ? Frame->GetStep() : nullptr;
    if (!Frame || !HostFor(Owner, Input, Step, Error) || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken))
    {
        return false;
    }
    TArray<FAbilityLightningSelectedTarget> Selected;
    if (!CapturedSubset(*Frame, Input, Selected, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    TArray<FBatch6ChainSegment> Segments;
    for (const auto& Selection : Selected)
    {
        FVector Position = Selection.Position;
        const auto* Actual = Frame->Targets.FindByPredicate(
            [&Selection](const FAbilityLightningSelectedTarget& Hit)
            {
                return AbilityBehaviorPrimitives::SameTargetLife(Hit.Target, Selection.Target);
            });
        if (Frame->bDamageReceipt && Actual)
        {
            Segments.Add({Step->From, Actual->Position});
        }
        else if (!Frame->bDamageReceipt &&
                 AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Selection.Target, Position))
        {
            Segments.Add({Step->From, Position});
        }
    }
    const auto& Settings = *CastChecked<UAbilityLightningCueConfig>(&Config);
    AbilityLightningActions::Present(*Caster, Step->From, Settings.BeamSystem, MoveTemp(Segments), Settings.Lifetime);
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

UAbilityGraphDefinition* AbilityLightningFragments::Build(UObject* Outer, const UAbilityLightningNodeConfig& Settings)
{
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = TEXT("UnifiedV2");
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::RegisteredSustained;
    auto* Control = NewObject<UAbilityLightningControlConfig>(Graph);
    const auto* Sequential = Cast<UAbilitySequentialChainNodeConfig>(&Settings);
    Control->bSequential = Sequential != nullptr;
    Control->MaxTargets = Settings.MaxTargets;
    Control->JumpDelay = Sequential ? Sequential->JumpDelay : .1f;
    auto* Query = NewObject<UAbilityLightningQueryConfig>(Graph);
    Query->JumpRadius = Settings.JumpRadius;
    auto* Damage = NewObject<UAbilityLightningDamageConfig>(Graph);
    Damage->DamageFalloff = Settings.DamageFalloff;
    auto* Cue = NewObject<UAbilityLightningCueConfig>(Graph);
    Cue->BeamSystem = Settings.BeamSystem;
    Cue->Lifetime = Sequential ? .35f : 1.5f;
    const FGuid A = FGuid::NewGuid();
    const FGuid B = FGuid::NewGuid();
    const FGuid C = FGuid::NewGuid();
    const FGuid D = FGuid::NewGuid();
    Graph->EntryNodeId = A;
    Graph->Nodes = {{A, TEXT("Control.LightningSteps"), Control},
                    {B, TEXT("Query.LightningTargets"), Query},
                    {C, TEXT("Effect.LightningDamageBatch"), Damage},
                    {D, TEXT("Presentation.LightningBeams"), Cue}};
    Graph->Edges = {{A, B, TEXT("Step")}, {B, C, TEXT("Completed")}, {C, D, TEXT("Completed")}};
    return Graph;
}

void AbilityLightningFragments::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Control;
    Control.TypeId = TEXT("Control.LightningSteps");
    Control.ConfigClass = UAbilityLightningControlConfig::StaticClass();
    Control.ExecutionClass = UAbilityLightningStepsExecution::StaticClass();
    Control.InputPayload = EAbilityPortPayload::LiveTarget;
    Control.bSupportsActorTargets = Control.bSupportsMassTargets = Control.bSupportsSnapshotTargets = true;
    Control.bOwnsTargetTracking = Control.bSustainedRoot = Control.bSustainedSafe = Control.bIsAsync = true;
    Control.Ports = {{TEXT("Step"), EAbilityPortPayload::Origin, false},
                     {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                     {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Query = Control;
    Query.TypeId = TEXT("Query.LightningTargets");
    Query.ConfigClass = UAbilityLightningQueryConfig::StaticClass();
    Query.ExecutionClass = UAbilityLightningTargetsExecution::StaticClass();
    Query.InputPayload = EAbilityPortPayload::Origin;
    Query.bOwnsTargetTracking = Query.bSustainedRoot = Query.bIsAsync = false;
    Query.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
    FAbilityNodeDescriptor Damage = Query;
    Damage.TypeId = TEXT("Effect.LightningDamageBatch");
    Damage.ConfigClass = UAbilityLightningDamageConfig::StaticClass();
    Damage.ExecutionClass = UAbilityLightningDamageExecution::StaticClass();
    Damage.InputPayload = EAbilityPortPayload::TargetSet;
    Damage.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Cue = Damage;
    Cue.TypeId = TEXT("Presentation.LightningBeams");
    Cue.ConfigClass = UAbilityLightningCueConfig::StaticClass();
    Cue.ExecutionClass = UAbilityLightningCueExecution::StaticClass();
    Cue.InputPayload = EAbilityPortPayload::Origin;
    for (const auto& Descriptor : {Control, Query, Damage, Cue})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Lightning fragment registration: %s"), *Error);
        }
    }
}
