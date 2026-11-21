// 调用链：注册图策略 -> 原生命锚点或 ExtensionWorld 的准确状态租约 -> 类型化后继事件。
#include "AbilitySystem/Unified/Nodes/AbilityStateCompositionNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityPrototypePresentationService.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace
{
    /** 共享旧 Context 时保留其预算/去重；新 Context 只捕获同一个原来源，不能跟随之后的 Avatar。 */
    UPrototypeCastContext* MakeContext(UObject* Outer, UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input,
                                       FString& Error)
    {
        auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get());
        if (!Context)
        {
            Context = NewObject<UPrototypeCastContext>(Outer);
            Context->Caster = Input.SourceToken.Avatar;
            Context->CastId = Owner.GetExecutionId();
            Context->Level = FMath::Clamp(FMath::RoundToInt(Input.Level), 1, 100);
            Context->DamageParams = Input.DamageParams;
        }
        if (!Context->CaptureSourceLife(Error))
        {
            return nullptr;
        }
        const auto& Source = Context->GetSourceLife();
        if (Source.ASC != Input.SourceToken.ASC || Source.Avatar != Input.SourceToken.Avatar ||
            Source.World != Input.SourceToken.World || Source.DungeonRunId != Input.SourceToken.DungeonRunId ||
            Source.EnemyLease != Input.SourceToken.EnemyLease)
        {
            Error = TEXT("State context must retain the exact graph source life.");
            return nullptr;
        }
        return Context;
    }

    /** 历史位置输出不会把旧目标重新升级为可施加状态的 LiveTarget。 */
    FAbilityNodeInput AtPosition(const FAbilityNodeInput& Input, FVector Position)
    {
        auto Output = Input;
        Output.Origin = Output.EventCenter = Position;
        Output.LiveTargetToken.Reset();
        return Output;
    }

    /** 原正常终结先尝试可选Emit；即使逻辑孩子被预拒，状态本身也必须结束。 */
    void CompleteState(UAbilityGraphInstance& Owner, FGuid Invocation, const FAbilityNodeInput& Output, bool bPrototype)
    {
        if (!bPrototype)
        {
            Owner.Publish(Invocation, TEXT("Completed"), Output);
            return;
        }
        if (!Owner.HasSuccessor(Invocation, TEXT("Completed")) ||
            !Owner.RequestChild(Invocation, TEXT("Completed"), Output))
        {
            Owner.FinishInvocation(Invocation);
        }
    }

    /** 弱索引只发布准确宿主；调用节点仍须通过UPROPERTY持有，并在Stop关闭。 */
    AActor* BeginFrameVisual(UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input, FVector Position,
                             FString& Error)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        if (!Frame || Frame->GetProfile() != TEXT("PrototypeFine.Extension.v1"))
        {
            return nullptr;
        }
        FAbilityPrototypePresentationFrame View;
        View.bReady = true;
        View.Transform = FTransform(Input.Direction.Rotation(), Position);
        auto* Host = AbilityPrototypePresentationService::Begin(Input.SourceToken, Frame->GetParameters(),
                                                                Frame->GetStableReference(), View,
                                                                EAbilityPrototypePresentationProfile::Extension, Error);
        if (Host && !Owner.IsFinished() && Owner.RegisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), Host))
        {
            return Host;
        }
        AbilityPrototypePresentationService::Finish(Host, true);
        return nullptr;
    }

    void CloseFrameVisual(UAbilityGraphInstance* Owner, const FAbilityNodeInput& Input, AActor* Host,
                          EAbilityGraphEndReason Reason)
    {
        if (!Host)
        {
            return;
        }
        if (Owner)
        {
            if (const auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Input.ParameterFrame.Get()))
            {
                Owner->UnregisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), Host);
            }
        }
        AbilityPrototypePresentationService::Finish(
            Host, Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired, true);
    }
} // namespace

bool UAbilityAttachNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Attach typed configuration requires a finite value");
        return false;
    }
    const TStrongObjectPtr<UAbilityAttachNodeConfig> Candidate(
        DuplicateObject<UAbilityAttachNodeConfig>(this, GetTransientPackage()));
    switch (Parameter)
    {
    case EAbilityConfigParameter::Duration:
        Candidate->Duration = Value;
        break;
    case EAbilityConfigParameter::Period:
        Candidate->Period = Value;
        break;
    case EAbilityConfigParameter::TransferRadius:
        Candidate->TransferRadius = Value;
        break;
    case EAbilityConfigParameter::MaxTransfers:
        if (Value < 0.f || Value > 16.f || Value != FMath::FloorToFloat(Value))
        {
            Error = TEXT("Attach transfer count must be an integer from 0 to 16");
            return false;
        }
        Candidate->MaxTransfers = int32(Value);
        break;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!Candidate->Validate(Error))
    {
        return false;
    }
    // 候选完整校验后才提交；失败不修改当前配方，运行中的执行继续持有它原来的配置副本。
    Duration = Candidate->Duration;
    Period = Candidate->Period;
    TransferRadius = Candidate->TransferRadius;
    MaxTransfers = Candidate->MaxTransfers;
    return true;
}

bool UAbilityStackNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value, FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Stack typed configuration requires a finite value");
        return false;
    }
    auto Candidate = Stack;
    switch (Parameter)
    {
    case EAbilityConfigParameter::StackThreshold:
        if (Value < 1.f || Value > 16.f || Value != FMath::FloorToFloat(Value))
        {
            Error = TEXT("Stack threshold must be an integer from 1 to 16");
            return false;
        }
        Candidate.Threshold = int32(Value);
        break;
    case EAbilityConfigParameter::StackLifetime:
        Candidate.Lifetime = Value;
        break;
    case EAbilityConfigParameter::DetonationDelay:
        Candidate.DetonationDelay = Value;
        break;
    default:
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!Candidate.Validate(Error))
    {
        return false;
    }
    // 世界叠层只读取已经启动时传入的值；这里不触碰任何现有lease、层数或触发回调。
    Stack = Candidate;
    return true;
}

bool UAbilityTargetDisplaceNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                             FString& Error)
{
    Error.Reset();
    if (!FMath::IsFinite(Value))
    {
        Error = TEXT("Displace typed configuration requires a finite value");
        return false;
    }
    if (Parameter != EAbilityConfigParameter::MoveDistance && Parameter != EAbilityConfigParameter::SafeDistance &&
        Parameter != EAbilityConfigParameter::Duration)
    {
        return Super::TrySetRecipeParameter(Parameter, Value, Error);
    }
    if (!Validate(Error))
    {
        return false;
    }
    const TStrongObjectPtr<UAbilityTargetDisplaceNodeConfig> Candidate(
        DuplicateObject<UAbilityTargetDisplaceNodeConfig>(this, GetTransientPackage()));
    Candidate->Settings = DuplicateObject<UExtensionNode>(Settings, Candidate.Get());
    switch (Parameter)
    {
    case EAbilityConfigParameter::MoveDistance:
        Candidate->Settings->MoveDistance = Value;
        break;
    case EAbilityConfigParameter::SafeDistance:
        Candidate->Settings->SafeDistance = Value;
        break;
    case EAbilityConfigParameter::Duration:
        Candidate->Settings->Duration = Value;
        break;
    default:
        return false;
    }
    if (!Candidate->Validate(Error))
    {
        return false;
    }
    // Settings可能原本指向共享DA。提交一份属于当前配置的私有副本，不能在另一施法/玩家上写回调参。
    Settings = DuplicateObject<UExtensionNode>(Candidate->Settings, this);
    return true;
}

bool UAbilityAttachNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Duration) || Duration < .1f || Duration > 60.f || !FMath::IsFinite(Period) || Period < .1f ||
        Period > 60.f || MaxTransfers < 0 || MaxTransfers > 16 || !FMath::IsFinite(TransferRadius) ||
        TransferRadius < 0.f)
    {
        Error =
            TEXT("Attach requires duration [0.1,60], period [0.1,60], 0-16 transfers and a finite nonnegative radius.");
    }
    return Error.IsEmpty();
}

bool UAbilityAttachNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Attach requires a captured live anchor.");
        return false;
    }
    bUsePrototypeFrame = Config.IsA<UAbilityPrototypeAttachNodeConfig>();
    if (bUsePrototypeFrame)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        const auto* Extension = Frame ? Cast<UExtensionNode>(&Frame->GetParameters()) : nullptr;
        if (!Extension || Extension->Behavior != EExtensionKind::Attach)
        {
            Error = TEXT("Prototype attachment requires its own extension frame.");
            return false;
        }
        // 时钟仍使用同一执行类；这些只读运行值只从唯一帧初始化，不是第二套可编辑配置。
        Settings = NewObject<UAbilityAttachNodeConfig>(this);
        Settings->Duration = Extension->Duration;
        Settings->Period = Extension->Period;
        Settings->MaxTransfers = Extension->MaxTransfers;
        Settings->TransferRadius = Extension->TransferRadius;
    }
    else
    {
        Settings = DuplicateObject(CastChecked<UAbilityAttachNodeConfig>(&Config), this);
    }
    Anchor = Input.LiveTargetToken.GetValue();
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Anchor, LastPosition))
    {
        Error = TEXT("Attach anchor is no longer its captured life.");
        return false;
    }
    Visited.Add(Anchor.Target);
    if (bUsePrototypeFrame)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        FAbilityPrototypePresentationFrame View;
        View.bReady = true;
        View.Transform = FTransform(Input.Direction.Rotation(), LastPosition);
        AActor* Host = Frame ? AbilityPrototypePresentationService::Begin(
                                   Input.SourceToken, Frame->GetParameters(), Frame->GetStableReference(), View,
                                   EAbilityPrototypePresentationProfile::Extension, Error)
                             : nullptr;
        if (bStopped || Owner.IsFinished())
        {
            AbilityPrototypePresentationService::Finish(Host, true);
            return true;
        }
        if (!Host || !Owner.RegisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), Host))
        {
            AbilityPrototypePresentationService::Finish(Host, true);
            return false;
        }
        PrototypeVisual = Host;
        VisualRevision = 1;
    }
    return true;
}

void UAbilityAttachNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings || !FMath::IsFinite(Delta) || Delta < 0.f)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityAttachNodeExecution> SelfGuard(this);
    const float ActiveDelta = FMath::Min(Delta, FMath::Max(0.f, Settings->Duration - Age - 2 * KINDA_SMALL_NUMBER));
    Age += Delta;
    FVector Position;
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Anchor, Position))
    {
        AActor* Caster = Input.SourceToken.Avatar.Get();
        TArray<FAbilityTargetCandidate> Candidates;
        if (Caster && Transfers < Settings->MaxTransfers)
        {
            for (const auto& Target :
                 PureMassCombat::Gather(Input.SourceToken.World.Get(), Caster, LastPosition, Settings->TransferRadius))
            {
                FAbilityTargetToken Token;
                FVector Point;
                FString Error;
                const bool bResolved =
                    !Visited.Contains(Target) &&
                    (bUsePrototypeFrame
                         ? Owner->ResolveOrCaptureOriginalTarget(Target, Token, Point, Error)
                         : AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, Target, Token, Error));
                if (bResolved && AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Token, Point) &&
                    PureMassCombat::Visible(Input.SourceToken.World.Get(), Caster, LastPosition, Target))
                {
                    Candidates.Add({Token, Point});
                }
            }
        }
        const auto Next = AbilityBehaviorPrimitives::SelectNearest(Candidates, LastPosition, Settings->TransferRadius,
                                                                   {}, EAbilityCandidateTieBreak::First, false);
        if (!Next.IsSet())
        {
            Owner->Publish(InvocationId, TEXT("TargetLost"), AtPosition(Input, LastPosition),
                           EAbilityGraphEndReason::TargetLost);
            return;
        }
        Anchor = Next->Target;
        Position = Next->Location;
        Visited.Add(Anchor.Target);
        ++Transfers;
        auto Output = AtPosition(Input, Position);
        Output.LiveTargetToken = Anchor;
        Owner->Publish(InvocationId, TEXT("Transferred"), Output);
    }
    LastPosition = Position;
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    if (PrototypeVisual)
    {
        FAbilityPrototypePresentationFrame View;
        View.bReady = true;
        View.Revision = ++VisualRevision;
        View.Transform = FTransform(Input.Direction.Rotation(), LastPosition);
        AbilityPrototypePresentationService::Update(PrototypeVisual, View);
    }
    // 延用 Extension 的最多 16 次追赶、积压 Clamp 及末帧截断；本节点从不结算固定伤害。
    AbilityBehaviorPrimitives::AdvanceCadence(
        Clock, ActiveDelta, Settings->Period, 16, EAbilityCadenceRemainder::ClampToPeriod,
        [&]()
        {
            auto* Caster = Input.SourceToken.Avatar.Get();
            FVector Current;
            if (!bStopped && !Owner->IsFinished() && Caster &&
                AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Anchor, Current) &&
                PureMassCombat::Visible(Input.SourceToken.World.Get(), Caster, Caster->GetActorLocation(),
                                        Anchor.Target))
            {
                LastPosition = Current;
                auto Output = AtPosition(Input, Current);
                Output.LiveTargetToken = Anchor;
                if (bUsePrototypeFrame)
                {
                    Owner->RequestChild(InvocationId, TEXT("Period"), Output);
                }
                else
                {
                    Owner->Publish(InvocationId, TEXT("Period"), Output);
                }
            }
            return !bStopped && !Owner->IsFinished();
        },
        KINDA_SMALL_NUMBER);
    if (!bStopped && !Owner->IsFinished() && Age + KINDA_SMALL_NUMBER >= Settings->Duration)
    {
        CompleteState(*Owner, InvocationId, AtPosition(Input, LastPosition), bUsePrototypeFrame);
    }
}

void UAbilityAttachNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (PrototypeVisual)
    {
        if (auto* Owner = GraphOwner.Get())
        {
            if (const auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Input.ParameterFrame.Get()))
            {
                Owner->UnregisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), PrototypeVisual);
            }
        }
        // Extension 原结束特效与业务 CompleteChild 分离；取消只走显式兼容表现，不补发子图。
        AbilityPrototypePresentationService::Finish(PrototypeVisual, Reason != EAbilityGraphEndReason::Completed, true);
        PrototypeVisual = nullptr;
    }
    Super::Stop(Reason);
    Settings = nullptr;
    Anchor = {};
    Visited.Reset();
}

bool UAbilityStackNodeConfig::Validate(FString& Error) const
{
    return Stack.Validate(Error);
}

bool UAbilityStackNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    bool bConfirmed = false;
    if (Input.Impact.IsSet() && (Input.bConfirmedHit || Input.bHasDamageResult))
    {
        TargetLife = Input.Impact->ConfirmedTarget;
        LastPosition = Input.Impact->Location;
        bConfirmed = true;
    }
    else if (Input.LiveTargetToken.IsSet())
    {
        TargetLife = Input.LiveTargetToken.GetValue();
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, TargetLife, LastPosition))
        {
            Error = TEXT("Stack live input has ended; a confirmed impact is required for historical application.");
            return false;
        }
    }
    else
    {
        Error = TEXT("Stack needs a live target or confirmed historical target life.");
        return false;
    }
    Context = MakeContext(this, Owner, Input, Error);
    WorldState =
        Input.SourceToken.World.IsValid() ? Input.SourceToken.World->GetSubsystem<UExtensionWorldSubsystem>() : nullptr;
    if (!Context || !WorldState.IsValid())
    {
        return false;
    }
    FExtensionStackRequest Request;
    Request.Context = Context;
    Request.RetainedSource = const_cast<UAbilityNodeConfig*>(&Config);
    bUsePrototypeFrame = Config.IsA<UAbilityPrototypeStackNodeConfig>();
    if (bUsePrototypeFrame)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        const auto* Extension = Frame ? Cast<UExtensionNode>(&Frame->GetParameters()) : nullptr;
        if (!Extension || Extension->Behavior != EExtensionKind::Stack)
        {
            Error = TEXT("Prototype stacking requires its own extension frame.");
            return false;
        }
        Request.Settings.EffectId = Extension->EffectId;
        Request.Settings.Threshold = Extension->StackThreshold;
        Request.Settings.Lifetime = Extension->StackLifetime;
        Request.Settings.DetonationDelay = Extension->DetonationDelay;
        Request.Depth = Frame->GetLogicalDepth();
    }
    else
    {
        Request.Settings = CastChecked<UAbilityStackNodeConfig>(&Config)->Stack;
    }
    Request.TargetLife = TargetLife;
    Request.Position = LastPosition;
    Request.Direction = Input.Direction;
    Request.bConfirmedHit = bConfirmed;
    const TWeakObjectPtr<UAbilityStackNodeExecution> WeakThis(this);
    Request.OnThreshold = [WeakThis](const FExtensionStackTrigger& Trigger)
    {
        if (auto* Execution = WeakThis.Get())
        {
            Execution->Threshold(Trigger);
        }
    };
    Request.OnReleased = [WeakThis](EExtensionStackReleaseReason Reason)
    {
        if (auto* Execution = WeakThis.Get())
        {
            Execution->Released(Reason);
        }
    };
    const auto StateOwner = WorldState;
    const auto Lease = StateOwner->AddStackShared(Request, Error);
    if (bStopped || Owner.IsFinished())
    {
        if (StateOwner.IsValid())
        {
            StateOwner->CancelStack(Lease);
        }
        return true;
    }
    StackLease = Lease;
    if (StackLease.IsValid() && Config.IsA<UAbilityPrototypeStackNodeConfig>())
    {
        PrototypeVisual = BeginFrameVisual(Owner, Input, Input.Origin, Error);
        if (!PrototypeVisual)
        {
            return Owner.IsFinished();
        }
    }
    return StackLease.IsValid();
}

void UAbilityStackNodeExecution::ClosePrototypeVisual(bool bCancelled)
{
    if (PrototypeVisual)
    {
        if (auto* Owner = GraphOwner.Get())
        {
            if (const auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Input.ParameterFrame.Get()))
            {
                Owner->UnregisterRuntimeResource(*Frame, TEXT("PrototypePresentation"), PrototypeVisual);
            }
        }
        AbilityPrototypePresentationService::Finish(PrototypeVisual, bCancelled, false);
        PrototypeVisual = nullptr;
    }
}

void UAbilityStackNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    ClosePrototypeVisual(Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired);
    const auto Lease = StackLease;
    StackLease.Invalidate();
    if (WorldState.IsValid())
    {
        WorldState->CancelStack(Lease);
    }
    WorldState.Reset();
    Context = nullptr;
}

void UAbilityStackNodeExecution::Threshold(const FExtensionStackTrigger& Trigger)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityStackNodeExecution> SelfGuard(this);
    LastPosition = Trigger.Position;
    auto Output = AtPosition(Input, Trigger.Position);
    Output.Direction = Trigger.Direction;
    Output.Context = Trigger.Context;
    if (bUsePrototypeFrame)
    {
        Owner->RequestChild(InvocationId, TEXT("Threshold"), Output);
    }
    else
    {
        Owner->Publish(InvocationId, TEXT("Threshold"), Output);
    }
}

void UAbilityStackNodeExecution::Released(EExtensionStackReleaseReason Reason)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityStackNodeExecution> SelfGuard(this);
    ClosePrototypeVisual(
        Reason == EExtensionStackReleaseReason::Cancelled || Reason == EExtensionStackReleaseReason::WorldEnded ||
        Reason == EExtensionStackReleaseReason::SourceInvalid || Reason == EExtensionStackReleaseReason::TargetLost);
    StackLease.Invalidate();
    if (Reason == EExtensionStackReleaseReason::Cancelled || Reason == EExtensionStackReleaseReason::WorldEnded ||
        Reason == EExtensionStackReleaseReason::SourceInvalid || Reason == EExtensionStackReleaseReason::TargetLost)
    {
        const auto End = Reason == EExtensionStackReleaseReason::SourceInvalid ? EAbilityGraphEndReason::SourceInvalid
                         : Reason == EExtensionStackReleaseReason::TargetLost  ? EAbilityGraphEndReason::TargetLost
                                                                               : EAbilityGraphEndReason::Cancelled;
        Owner->FinishInvocation(InvocationId, End);
        return;
    }
    FVector Position;
    if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, TargetLife, Position))
    {
        LastPosition = Position;
    }
    Owner->Publish(InvocationId, TEXT("Released"), AtPosition(Input, LastPosition),
                   Reason == EExtensionStackReleaseReason::Expired ? EAbilityGraphEndReason::Expired
                                                                   : EAbilityGraphEndReason::Completed);
}

bool UAbilityTargetDisplaceNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!Settings || Settings->Behavior != EExtensionKind::Displace || Settings->HitChild || Settings->PeriodChild ||
        Settings->CompleteChild || !Settings->ValidateGraph())
    {
        Error = TEXT("Target displacement needs one valid Displace configuration without embedded payload children.");
    }
    return Error.IsEmpty();
}

bool UAbilityTargetDisplaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error) || !Input.LiveTargetToken.IsSet())
    {
        Error = TEXT("Target displacement requires a live target input.");
        return false;
    }
    TargetLife = Input.LiveTargetToken.GetValue();
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, TargetLife, LastPosition))
    {
        Error = TEXT("Target displacement cannot capture a replacement target life.");
        return false;
    }
    Context = MakeContext(this, Owner, Input, Error);
    bUsePrototypeFrame = Config.IsA<UAbilityPrototypeDisplaceNodeConfig>();
    if (bUsePrototypeFrame)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        const auto* Extension = Frame ? Cast<UExtensionNode>(&Frame->GetParameters()) : nullptr;
        if (!Extension || Extension->Behavior != EExtensionKind::Displace || !Extension->bSingleTarget)
        {
            Error = TEXT("Prototype single displacement requires its own registered target frame.");
            return false;
        }
        Settings = DuplicateObject(Extension, this);
        // 子调用由图边拥有；世界位移只消费动作本身，保留全部免疫、碰撞、RootMotion 数值。
        Settings->HitChild = nullptr;
        Settings->PeriodChild = nullptr;
        Settings->CompleteChild = nullptr;
    }
    else
    {
        Settings = DuplicateObject(CastChecked<UAbilityTargetDisplaceNodeConfig>(&Config)->Settings.Get(), this);
    }
    WorldState =
        Input.SourceToken.World.IsValid() ? Input.SourceToken.World->GetSubsystem<UExtensionWorldSubsystem>() : nullptr;
    if (!Context || !WorldState.IsValid())
    {
        return false;
    }
    if (bUsePrototypeFrame)
    {
        PrototypeVisual = BeginFrameVisual(Owner, Input, Input.Origin, Error);
        if (!PrototypeVisual)
        {
            return Owner.IsFinished();
        }
    }
    const auto StateOwner = WorldState;
    const FVector Center = Config.IsA<UAbilityPrototypeDisplaceNodeConfig>() ? Input.EventCenter : Input.Origin;
    const auto Lease = StateOwner->RequestDisplacement(Context, Settings, TargetLife.Target, Center, Input.Direction);
    if (bStopped || Owner.IsFinished())
    {
        if (StateOwner.IsValid())
        {
            StateOwner->CancelDisplacement(Lease);
        }
        return true;
    }
    MovementLease = Lease;
    if (!MovementLease.IsValid())
    {
        if (bUsePrototypeFrame)
        {
            // 原Extension宿主的Moves.empty只Finish(true)本分支；不取消其它命中目标/共享Context。
            CloseFrameVisual(&Owner, Input, PrototypeVisual, EAbilityGraphEndReason::Cancelled);
            PrototypeVisual = nullptr;
            return Owner.Publish(Id, TEXT("Rejected"), Input);
        }
        Error = TEXT("Original displacement policy rejected geometry, immunity or movement capacity.");
        return false;
    }
    return true;
}

void UAbilityTargetDisplaceNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !WorldState.IsValid() || !FMath::IsFinite(Delta) || Delta < 0.f)
    {
        return;
    }
    FVector CurrentPosition;
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, TargetLife, CurrentPosition))
    {
        Owner->Publish(InvocationId, TEXT("TargetLost"), AtPosition(Input, LastPosition),
                       EAbilityGraphEndReason::TargetLost);
    }
    else if (WorldState->WasMoveReplaced(MovementLease))
    {
        LastPosition = CurrentPosition;
        Owner->Publish(InvocationId, TEXT("Replaced"), AtPosition(Input, LastPosition),
                       EAbilityGraphEndReason::Cancelled);
    }
    else if (!WorldState->IsDisplacementActive(MovementLease))
    {
        LastPosition = CurrentPosition;
        if (PrototypeVisual)
        {
            FAbilityPrototypePresentationFrame View;
            View.bReady = true;
            View.Revision = ++VisualRevision;
            View.Transform = FTransform(Input.Direction.Rotation(), CurrentPosition);
            AbilityPrototypePresentationService::Update(PrototypeVisual, View);
        }
        // World/CharacterMovement/Mass 处理器是唯一移动时钟；这里只观察真实终态，不用第二个估计计时器。
        CompleteState(*Owner, InvocationId, AtPosition(Input, LastPosition), bUsePrototypeFrame);
    }
    else
    {
        LastPosition = CurrentPosition;
        if (PrototypeVisual)
        {
            FAbilityPrototypePresentationFrame View;
            View.bReady = true;
            View.Revision = ++VisualRevision;
            View.Transform = FTransform(Input.Direction.Rotation(), CurrentPosition);
            AbilityPrototypePresentationService::Update(PrototypeVisual, View);
        }
    }
}

void UAbilityTargetDisplaceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    CloseFrameVisual(GraphOwner.Get(), Input, PrototypeVisual, Reason);
    PrototypeVisual = nullptr;
    Super::Stop(Reason);
    if (Reason != EAbilityGraphEndReason::Completed && WorldState.IsValid())
    {
        WorldState->CancelDisplacement(MovementLease);
    }
    MovementLease.Invalidate();
    Context = nullptr;
    Settings = nullptr;
    WorldState.Reset();
}

bool UAbilityPrototypeAreaDisplaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                       const UAbilityNodeConfig& Config,
                                                       const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    const auto* Extension = Frame ? Cast<UExtensionNode>(&Frame->GetParameters()) : nullptr;
    if (!Extension || Extension->Behavior != EExtensionKind::Displace || Extension->bSingleTarget)
    {
        Error = TEXT("Area displacement requires its own extension region frame.");
        return false;
    }
    Context = MakeContext(this, Owner, Input, Error);
    WorldState =
        Input.SourceToken.World.IsValid() ? Input.SourceToken.World->GetSubsystem<UExtensionWorldSubsystem>() : nullptr;
    if (!Context || !WorldState.IsValid())
    {
        return false;
    }
    Settings = DuplicateObject(Extension, this);
    Settings->HitChild = Settings->PeriodChild = Settings->CompleteChild = nullptr;
    Age = 0.f;
    // 复用原完整混合查询/可见性和同根原生命；每个具体动作只进入原World一次。
    for (const auto& Candidate :
         PureMassCombat::Gather(Input.SourceToken.World.Get(), Context->Caster.Get(), Input.Origin, Extension->Radius))
    {
        if (bStopped || Owner.IsFinished())
        {
            return true;
        }
        FAbilityTargetToken Token;
        FVector Position;
        if (!PureMassCombat::Visible(Input.SourceToken.World.Get(), Context->Caster.Get(),
                                     Input.Origin + FVector(0, 0, 2), Candidate) ||
            !Owner.ResolveOrCaptureOriginalTarget(Candidate, Token, Position, Error))
        {
            continue;
        }
        const auto StateOwner = WorldState;
        const FGuid Lease =
            StateOwner->RequestDisplacement(Context, Settings, Candidate, Input.Origin, Input.Direction);
        if (bStopped || Owner.IsFinished())
        {
            if (StateOwner.IsValid())
            {
                StateOwner->CancelDisplacement(Lease);
            }
            return true;
        }
        if (Lease.IsValid())
        {
            MoveLeases.Add(Lease);
        }
    }
    // 原区域即使没有可移动目标也保留原时长/结束出口；不要把空集合当成即时完成。
    PrototypeVisual = BeginFrameVisual(Owner, Input, Input.Origin, Error);
    return PrototypeVisual || Owner.IsFinished();
}

void UAbilityPrototypeAreaDisplaceNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings || !FMath::IsFinite(Delta) || Delta < 0.f)
    {
        return;
    }
    Age += Delta;
    if (Age + KINDA_SMALL_NUMBER >= Settings->Duration)
    {
        CompleteState(*Owner, InvocationId, AtPosition(Input, Input.Origin), true);
    }
}

void UAbilityPrototypeAreaDisplaceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    CloseFrameVisual(GraphOwner.Get(), Input, PrototypeVisual, Reason);
    PrototypeVisual = nullptr;
    Super::Stop(Reason);
    if (Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired &&
        WorldState.IsValid())
    {
        for (FGuid Lease : MoveLeases)
        {
            WorldState->CancelDisplacement(Lease);
        }
    }
    MoveLeases.Reset();
    WorldState.Reset();
    Context = nullptr;
    Settings = nullptr;
}

void AbilityStateCompositionNodes::Register(FAbilityNodeRegistry& Registry)
{
    const auto Add = [&](FAbilityNodeDescriptor Descriptor)
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("State composition registration: %s"), *Error);
        }
    };
    FAbilityNodeDescriptor Attach;
    Attach.TypeId = TEXT("State.Attach");
    Attach.ConfigClass = UAbilityAttachNodeConfig::StaticClass();
    Attach.ExecutionClass = UAbilityAttachNodeExecution::StaticClass();
    Attach.InputPayload = EAbilityPortPayload::LiveTarget;
    Attach.bSupportsActorTargets = Attach.bSupportsMassTargets = true;
    Attach.bOwnsTargetTracking = true;
    Attach.bIsAsync = true;
    // 通用附着不生成Actor；每个周期后继仍走同一个根的调用/事件额度与原生命校验。
    Attach.bRuntimeFiniteSafe = true;
    Attach.Ports = {{TEXT("Period"), EAbilityPortPayload::LiveTarget, false},
                    {TEXT("Transferred"), EAbilityPortPayload::LiveTarget, false},
                    {TEXT("TargetLost"), EAbilityPortPayload::Origin, true},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Attach);
    for (const auto& Pair :
         {TPair<FName, EAbilityPortPayload>(TEXT("State.Stack"), EAbilityPortPayload::LiveTarget),
          TPair<FName, EAbilityPortPayload>(TEXT("State.StackImpact"), EAbilityPortPayload::ConfirmedImpact),
          TPair<FName, EAbilityPortPayload>(TEXT("State.StackDamage"), EAbilityPortPayload::DamageResult)})
    {
        FAbilityNodeDescriptor Stack;
        Stack.TypeId = Pair.Key;
        Stack.ConfigClass = UAbilityStackNodeConfig::StaticClass();
        Stack.ExecutionClass = UAbilityStackNodeExecution::StaticClass();
        Stack.InputPayload = Pair.Value;
        Stack.bSupportsActorTargets = Stack.bSupportsMassTargets = Stack.bSupportsSnapshotTargets = true;
        Stack.bIsAsync = true;
        // 只有准确世界叠层lease；renderer为World子系统最多一份的固定共享宿主，不按层数派生Actor。
        Stack.bRuntimeFiniteSafe = true;
        Stack.Lifetime = EAbilityExecutionLifetime::TargetPersistentState;
        Stack.Ports = {{TEXT("Threshold"), EAbilityPortPayload::Origin, false},
                       {TEXT("Released"), EAbilityPortPayload::Origin, true}};
        Add(Stack);
    }
    FAbilityNodeDescriptor Move;
    Move.TypeId = TEXT("Movement.TargetDisplace");
    Move.ConfigClass = UAbilityTargetDisplaceNodeConfig::StaticClass();
    Move.ExecutionClass = UAbilityTargetDisplaceNodeExecution::StaticClass();
    Move.InputPayload = EAbilityPortPayload::LiveTarget;
    Move.bSupportsActorTargets = Move.bSupportsMassTargets = true;
    Move.bIsAsync = true;
    // 只申请已有位移服务的精确命令，不生成Actor或内部完整GA。
    Move.bRuntimeFiniteSafe = true;
    Move.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true},
                  {TEXT("Replaced"), EAbilityPortPayload::Origin, true},
                  {TEXT("TargetLost"), EAbilityPortPayload::Origin, true}};
    Add(Move);
}
