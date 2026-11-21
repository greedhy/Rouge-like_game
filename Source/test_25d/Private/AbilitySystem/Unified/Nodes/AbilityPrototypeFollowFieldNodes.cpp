// 调用链：唯一冻结帧 -> 随动/真实GE费用 -> 有界本体脉冲 -> 同图独立查询/效果 -> 准确资源清理。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFollowFieldNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "PureMass/PureMassAbility.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityPrototypeFollowFieldDetails
{
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    /** 新位置事实不继承祖先命中权限；原OnPeriod继续携带父级已选活目标的可用证明。 */
    FAbilityNodeInput Position(const FAbilityNodeInput& Input, FVector Anchor)
    {
        auto Result = Input;
        Result.Origin = Anchor;
        Result.EventCenter = FVector::ZeroVector;
        Result.TargetSet.Reset();
        Result.Impact.Reset();
        Result.bConfirmedHit = Result.bHasDamageResult = Result.bDamageAttempted = Result.bKilled = false;
        Result.DamageApplied.Reset();
        Result.ActualDamage.Reset();
        if (Result.LiveTargetToken.IsSet())
        {
            FVector Location;
            if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Result.LiveTargetToken.GetValue(),
                                                         Location))
            {
                Result.LiveTargetToken.Reset();
            }
        }
        return Result;
    }
} // namespace AbilityPrototypeFollowFieldDetails

bool UAbilityPrototypeFollowFieldNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                      const UAbilityNodeConfig& Configuration,
                                                      const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    const auto* Field = Frame ? Cast<UBatch6FieldPrototype>(&Frame->GetParameters()) : nullptr;
    if (!Frame || Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1") || Frame->GetLogicalDepth() != 0 || !Field ||
        Field->Kind != EAbilityPrototypeKind::Field || !Field->bFollowCasterToggle)
    {
        Error = TEXT("Follow Field requires the original depth-zero Mixed Batch6 toggle frame");
        return false;
    }
    Anchor = Input.Origin;
    Clock = 0.f;
    Visual = {};
    Visual.bReady = true;
    Visual.Transform = FTransform(Input.Direction.Rotation(), Anchor);
    PresentationHost =
        AbilityPrototypePresentationService::Begin(Input.SourceToken, *Field, Frame->GetStableReference(), Visual,
                                                   EAbilityPrototypePresentationProfile::Mixed, Error);
    if (!PresentationHost)
    {
        return false;
    }
    if (!Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeFollowFieldDetails::PresentationSlot, PresentationHost))
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, true);
        PresentationHost = nullptr;
        Error = TEXT("Follow Field could not bind its exact presentation resource");
        return false;
    }
    return true;
}

void UAbilityPrototypeFollowFieldNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeFollowFieldNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeParameterFrame> FrameGuard(Frame.Get());
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    Anchor = Avatar->GetActorLocation();
    Visual.Revision++;
    Visual.Transform.SetLocation(Anchor);
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
    const auto& Parameters = Frame->GetParameters();
    // 与原Mixed一致：整帧随动后先扣整帧费用，失败不能继续本帧脉冲；Duration不裁剪此开关根。
    const bool bPaid = UPureMassAbility::SpendMana(Avatar, Parameters.ManaPerSecond * DeltaSeconds);
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return;
    }
    if (!bPaid)
    {
        if (auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get()))
        {
            Context->bCancelled = true;
        }
        Owner->Cancel(EAbilityGraphEndReason::Cancelled);
        return;
    }
    AbilityBehaviorPrimitives::AdvanceCadence(Clock, DeltaSeconds, Parameters.Period, 8,
                                              EAbilityCadenceRemainder::Preserve,
                                              [this]()
                                              {
                                                  return Pulse();
                                              });
}

bool UAbilityPrototypeFollowFieldNodeExecution::Pulse()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Frame || !Owner || Owner->IsFinished())
    {
        return false;
    }
    const auto Payload = AbilityPrototypeFollowFieldDetails::Position(Input, Anchor);
    if (Owner->HasSuccessor(InvocationId, TEXT("Action")))
    {
        if (!Owner->Publish(InvocationId, TEXT("Action"), Payload))
        {
            return false;
        }
        Owner->FlushImmediatePayloads(true);
    }
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return false;
    }
    if (Owner->HasSuccessor(InvocationId, TEXT("Period")))
    {
        Owner->RequestChild(InvocationId, TEXT("Period"), Payload);
    }
    return !bStopped && !Owner->IsFinished();
}

void UAbilityPrototypeFollowFieldNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        const bool bCancelled =
            Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired;
        if (bCancelled && Frame)
        {
            if (auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get()))
            {
                Context->bCancelled = true;
            }
        }
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, bCancelled);
        if (auto* Owner = GraphOwner.Get(); Owner && Frame && PresentationHost)
        {
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeFollowFieldDetails::PresentationSlot,
                                             PresentationHost);
        }
        PresentationHost = nullptr;
        Frame = nullptr;
        Clock = 0.f;
    }
    Super::Stop(Reason);
}

void AbilityPrototypeFollowFieldNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Control.PrototypeFollowField");
    Descriptor.ConfigClass = UAbilityPrototypeFollowFieldNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityPrototypeFollowFieldNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
    // Start严格要求Mixed depth0随动开关；两个周期输出可能无次数上限，新增后继须独立证明为同步纯载荷。
    Descriptor.PotentiallyUnboundedOutputPorts = {TEXT("Action"), TEXT("Period")};
    Descriptor.Ports = {{TEXT("Action"), EAbilityPortPayload::Origin, false},
                        {TEXT("Period"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Descriptor.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Follow Field registration: %s"), *Error);
    }
}
