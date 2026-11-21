// 调用链：Scope.Ready -> 运动/逐跳/地表 -> Contact目标集 -> 共享伤害/接触子图 -> ContactEnd回执。
#include "AbilitySystem/Unified/Nodes/AbilityExtensionFlowNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeSurfaceService.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "PureMass/PureMassAbility.h"
#include "Terrain/TerrainSurface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    const FName VisualSlot(TEXT("PrototypePresentation"));
    const FName ContactSlot(TEXT("ExtensionContact"));

    UAbilityPrototypeParameterFrame* ResolveExtension(UAbilityGraphInstance& Owner, const FAbilityNodeInput& Input,
                                                      EExtensionKind Kind, FString& Error)
    {
        const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
        const auto* Node = Frame ? Cast<UExtensionNode>(&Frame->GetParameters()) : nullptr;
        if (!Node || Frame->GetProfile() != TEXT("PrototypeFine.Extension.v1") || Node->Behavior != Kind)
        {
            Error = TEXT("Extension action requires its exact immutable behavior frame.");
            return nullptr;
        }
        return const_cast<UAbilityPrototypeParameterFrame*>(Frame);
    }

    bool BeginVisual(UAbilityGraphInstance& Owner, UAbilityPrototypeParameterFrame& Frame,
                     const FAbilityNodeInput& Input, FAbilityPrototypePresentationFrame& Visual,
                     TObjectPtr<AActor>& Host, FString& Error)
    {
        Visual.bReady = true;
        Visual.Transform = FTransform(Input.Direction.Rotation(), Input.Origin);
        Visual.BeamStart = Visual.BeamEnd = Input.Origin;
        Host = AbilityPrototypePresentationService::Begin(Input.SourceToken, Frame.GetParameters(),
                                                          Frame.GetStableReference(), Visual,
                                                          EAbilityPrototypePresentationProfile::Extension, Error);
        if (Host && !Owner.IsFinished() && Owner.RegisterRuntimeResource(Frame, VisualSlot, Host))
        {
            return true;
        }
        AbilityPrototypePresentationService::Finish(Host, true, false);
        Host = nullptr;
        return false;
    }

    void EndVisual(UAbilityGraphInstance* Owner, UAbilityPrototypeParameterFrame* Frame, AActor* Host,
                   EAbilityGraphEndReason Reason)
    {
        if (Owner && Frame && Host)
        {
            Owner->UnregisterRuntimeResource(*Frame, VisualSlot, Host);
        }
        AbilityPrototypePresentationService::Finish(
            Host, Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired, true);
    }

    FAbilityNodeInput PositionInput(const FAbilityNodeInput& Input, FVector Position, FVector Center)
    {
        auto Result = Input;
        Result.Origin = Position;
        Result.EventCenter = Center;
        Result.TargetSet.Reset();
        return Result;
    }

    FAbilityNodeInput ContactInput(const FAbilityNodeInput& Input, const FAbilityTargetToken& Target, FVector Center)
    {
        auto Result = PositionInput(Input, Center, Center);
        Result.LiveTargetToken.Reset();
        auto Targets = MakeShared<FAbilityTargetSetSnapshot>();
        Targets->Source = Input.SourceToken;
        Targets->Targets.Add(Target);
        Result.TargetSet = MoveTemp(Targets);
        return Result;
    }

    /** 到期和完成分别发布，保留实际额外边；没有消费者时不制造无意义辅助事件。 */
    void PublishCompletion(UAbilityGraphInstance& Owner, FGuid Invocation, const FAbilityNodeInput& Output)
    {
        if (Owner.HasSuccessor(Invocation, TEXT("Expired")))
        {
            Owner.RequestChild(Invocation, TEXT("Expired"), Output);
        }
        if (!Owner.IsFinished() && Owner.HasSuccessor(Invocation, TEXT("Completed")))
        {
            Owner.Publish(Invocation, TEXT("Completed"), Output);
        }
        else if (!Owner.IsFinished())
        {
            Owner.FinishInvocation(Invocation);
        }
    }
} // namespace

bool AbilityExtensionFlowNodes::PrepareSurfaceEntry(UPrototypeCastContext& Context, const UExtensionNode& Node,
                                                    FVector& Position)
{
    if (Node.Behavior != EExtensionKind::Surface)
    {
        return true;
    }
    if (Context.Surfaces >= 8 || !APrototypeRuntime::ProjectSurfaceToGround(Context.Caster.Get(), Position, Position))
    {
        return false;
    }
    ++Context.Surfaces;
    return true;
}

ATerrainSurface* AbilityExtensionFlowNodes::SpawnSurface(AActor& Caster, const UExtensionNode& Node, FVector Position)
{
    return AbilityPrototypeSurfaceService::Spawn(Caster, Node, Position, EAbilityPrototypeSurfaceProfile::Mixed);
}

bool UAbilityExtensionProjectileNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                     const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                     FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Frame = ResolveExtension(Owner, Input, EExtensionKind::Projectile, Error);
    if (!Frame || !Owner.ReservePhysicalSpawnWithLease(InvocationId, PhysicalLease))
    {
        return false;
    }
    Position = Input.Origin;
    return BeginVisual(Owner, *Frame, Input, Visual, PresentationHost, Error) &&
           Owner.RegisterRuntimeResource(*Frame, ContactSlot, this);
}

void UAbilityExtensionProjectileNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || bAwaitingContact || !Owner || Owner->IsFinished() || !Frame)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityExtensionProjectileNodeExecution> Guard(this);
    const auto& Node = *CastChecked<UExtensionNode>(&Frame->GetParameters());
    const float ActiveDelta = FMath::Min(DeltaSeconds, FMath::Max(0.f, Node.Duration - Age - 2 * KINDA_SMALL_NUMBER));
    Age += DeltaSeconds;
    const float Step = FMath::Min(Node.Speed * ActiveDelta, Node.Range - Travel);
    const FVector End = Position + Input.Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector) * Step;
    FHitResult Wall;
    float Time = 1.f;
    const auto Target = PureMassCombat::Sweep(Input.SourceToken.World.Get(), Input.SourceToken.Avatar.Get(), Position,
                                              End, Node.CollisionRadius, Time, Wall, nullptr, PresentationHost);
    Position = Target.IsSet() ? FMath::Lerp(Position, End, Time) : Wall.bBlockingHit ? Wall.ImpactPoint : End;
    Visual.Transform.SetLocation(Position);
    ++Visual.Revision;
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
    if (Target.IsSet())
    {
        FAbilityTargetToken Token;
        FVector TargetPosition;
        FString Error;
        if (Owner->ResolveOrCaptureOriginalTarget(Target, Token, TargetPosition, Error))
        {
            bAwaitingContact = true;
            if (Owner->HasSuccessor(InvocationId, TEXT("Contact")))
            {
                Owner->Publish(InvocationId, TEXT("Contact"), ContactInput(Input, Token, Position));
                Owner->FlushImmediatePayloads(true);
            }
            else
            {
                CompleteContact();
            }
            return;
        }
        CompleteFlight();
        return;
    }
    Travel += Step;
    if (Wall.bBlockingHit || Travel >= Node.Range || Age + KINDA_SMALL_NUMBER >= Node.Duration)
    {
        CompleteFlight();
    }
}

void UAbilityExtensionProjectileNodeExecution::CompleteContact()
{
    if (bAwaitingContact && !bStopped && !bCompleting)
    {
        bAwaitingContact = false;
        CompleteFlight();
    }
}

void UAbilityExtensionProjectileNodeExecution::CompleteFlight()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    // 原投射在本体命中/墙面/范围结束后立即关闭；孩子独立存在。
    EndVisual(Owner, Frame, PresentationHost, EAbilityGraphEndReason::Completed);
    PublishCompletion(*Owner, InvocationId, PositionInput(Input, Position, Position));
}

void UAbilityExtensionProjectileNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    auto* Owner = GraphOwner.Get();
    if (Owner && Frame)
    {
        Owner->UnregisterRuntimeResource(*Frame, ContactSlot, this);
    }
    EndVisual(Owner, Frame, PresentationHost, Reason);
    if (Owner && PhysicalLease.IsValid())
    {
        Owner->ReleasePhysicalSpawnLease(PhysicalLease);
    }
    PhysicalLease.Invalidate();
    PresentationHost = nullptr;
    Frame = nullptr;
    Super::Stop(Reason);
}

bool UAbilityExtensionSequenceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                   const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                   FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Frame = ResolveExtension(Owner, Input, EExtensionKind::Chain, Error);
    if (!Frame || !BeginVisual(Owner, *Frame, Input, Visual, PresentationHost, Error) ||
        !Owner.RegisterRuntimeResource(*Frame, ContactSlot, this))
    {
        return false;
    }
    Current = Input.LiveTargetToken;
    if (!Current.IsSet() && Input.Impact.IsSet())
    {
        Current = Input.Impact->ConfirmedTarget;
    }
    LastPosition = Input.Origin;
    // Extension独有合同：首次接触在Start，不借用Actor/Mixed首次Advance的连锁策略。
    Contact();
    return true;
}

void UAbilityExtensionSequenceNodeExecution::Contact()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || bAwaitingContact || !Owner || Owner->IsFinished() || !Frame)
    {
        return;
    }
    if (!Current.IsSet() || !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *Current, PendingPosition) ||
        !PureMassCombat::Visible(Input.SourceToken.World.Get(), Input.SourceToken.Avatar.Get(), LastPosition,
                                 Current->Target, PresentationHost))
    {
        CompleteSequence();
        return;
    }
    const auto& Node = *CastChecked<UExtensionNode>(&Frame->GetParameters());
    Visual.BeamStart = LastPosition;
    Visual.BeamEnd = PendingPosition;
    ++Visual.Revision;
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
    Visited.Add(Current->Target);
    auto Payload = ContactInput(Input, *Current, Input.Origin);
    Payload.DamageScale *= FMath::Pow(Node.DamageFalloff, Hits++);
    bAwaitingContact = true;
    if (Owner->HasSuccessor(InvocationId, TEXT("Contact")))
    {
        Owner->Publish(InvocationId, TEXT("Contact"), Payload);
        Owner->FlushImmediatePayloads(true);
    }
    else
    {
        CompleteContact();
    }
}

void UAbilityExtensionSequenceNodeExecution::CompleteContact()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !bAwaitingContact || !Owner || Owner->IsFinished() || !Frame)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityExtensionSequenceNodeExecution> Guard(this);
    bAwaitingContact = false;
    LastPosition = PendingPosition;
    const auto& Node = *CastChecked<UExtensionNode>(&Frame->GetParameters());
    if (Hits >= Node.Count)
    {
        CompleteSequence();
        return;
    }
    TArray<FAbilityTargetCandidate> Eligible;
    for (const auto Target : PureMassCombat::Gather(Input.SourceToken.World.Get(), Input.SourceToken.Avatar.Get(),
                                                    LastPosition, Node.Radius))
    {
        FAbilityTargetToken Token;
        FVector Position;
        FString Error;
        if (!Visited.Contains(Target) && Owner->ResolveOrCaptureOriginalTarget(Target, Token, Position, Error) &&
            PureMassCombat::Visible(Input.SourceToken.World.Get(), Input.SourceToken.Avatar.Get(), LastPosition, Target,
                                    PresentationHost))
        {
            Eligible.Add({Token, Position});
        }
    }
    // 原Extension严格半径、等距保留首个；Actor/Mixed默认的Last tie不适用。
    const auto Next = AbilityBehaviorPrimitives::SelectNearest(Eligible, LastPosition, Node.Radius, {},
                                                               EAbilityCandidateTieBreak::First, false);
    Current = Next.IsSet() ? TOptional<FAbilityTargetToken>(Next->Target) : TOptional<FAbilityTargetToken>();
    if (!Current.IsSet())
    {
        CompleteSequence();
        return;
    }
    PumpCadence();
}

void UAbilityExtensionSequenceNodeExecution::Advance(float DeltaSeconds)
{
    if (bStopped || bCompleting || !Frame || !GraphOwner.IsValid() || GraphOwner->IsFinished())
    {
        return;
    }
    const auto& Node = *CastChecked<UExtensionNode>(&Frame->GetParameters());
    const float ActiveDelta = FMath::Min(DeltaSeconds, FMath::Max(0.f, Node.Duration - Age - 2 * KINDA_SMALL_NUMBER));
    Age += DeltaSeconds;
    Clock += ActiveDelta;
    StepsInAdvance = 0;
    bAdvanceExpired = Age + KINDA_SMALL_NUMBER >= Node.Duration;
    PumpCadence();
}

void UAbilityExtensionSequenceNodeExecution::PumpCadence()
{
    if (bStopped || bCompleting || bAwaitingContact || !Frame)
    {
        return;
    }
    const auto& Node = *CastChecked<UExtensionNode>(&Frame->GetParameters());
    if (Clock + KINDA_SMALL_NUMBER >= Node.Period && StepsInAdvance < 16)
    {
        Clock -= Node.Period;
        ++StepsInAdvance;
        Contact();
        return;
    }
    if (StepsInAdvance >= 16)
    {
        Clock = FMath::Min(Clock, Node.Period);
    }
    if (bAdvanceExpired)
    {
        CompleteSequence();
    }
}

void UAbilityExtensionSequenceNodeExecution::CompleteSequence()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner || Owner->IsFinished())
    {
        return;
    }
    bCompleting = true;
    EndVisual(Owner, Frame, PresentationHost, EAbilityGraphEndReason::Completed);
    auto Output = PositionInput(Input, LastPosition, Input.Origin);
    Output.LiveTargetToken = Current;
    PublishCompletion(*Owner, InvocationId, Output);
}

void UAbilityExtensionSequenceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (GraphOwner.IsValid() && Frame)
    {
        GraphOwner->UnregisterRuntimeResource(*Frame, ContactSlot, this);
    }
    EndVisual(GraphOwner.Get(), Frame, PresentationHost, Reason);
    PresentationHost = nullptr;
    Frame = nullptr;
    Super::Stop(Reason);
}

bool UAbilityExtensionSurfaceNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                  const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                  FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Frame = ResolveExtension(Owner, Input, EExtensionKind::Surface, Error);
    FAbilityPrototypePresentationFrame Visual;
    if (!Frame || !BeginVisual(Owner, *Frame, Input, Visual, PresentationHost, Error))
    {
        return false;
    }
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Caster))
    {
        return false;
    }
    Surface = AbilityExtensionFlowNodes::SpawnSurface(*Caster, *CastChecked<UExtensionNode>(&Frame->GetParameters()),
                                                      Input.Origin);
    return Surface != nullptr;
}

void UAbilityExtensionSurfaceNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    Age += DeltaSeconds;
    if (Age + KINDA_SMALL_NUMBER >= Frame->GetParameters().Duration)
    {
        bCompleting = true;
        if (IsValid(Surface))
        {
            Surface->Destroy();
        }
        Surface = nullptr;
        EndVisual(Owner, Frame, PresentationHost, EAbilityGraphEndReason::Completed);
        PublishCompletion(*Owner, InvocationId, PositionInput(Input, Input.Origin, Input.Origin));
    }
}

void UAbilityExtensionSurfaceNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (IsValid(Surface))
    {
        Surface->Destroy();
    }
    Surface = nullptr;
    EndVisual(GraphOwner.Get(), Frame, PresentationHost, Reason);
    PresentationHost = nullptr;
    Frame = nullptr;
    Super::Stop(Reason);
}

bool UAbilityExtensionContactEndNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                     const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                     FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto* Frame = AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error);
    auto* Contact =
        Frame ? Cast<UAbilityExtensionContactOwner>(Owner.FindRuntimeResource(*Frame, ContactSlot)) : nullptr;
    if (!Contact)
    {
        Error = TEXT("Extension contact completion lost its exact frame owner.");
        return false;
    }
    Contact->CompleteContact();
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityExtensionFlowNodes::Register(FAbilityNodeRegistry& Registry)
{
    auto Add = [&](FAbilityNodeDescriptor Descriptor)
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Extension fragment registration: %s"), *Error);
        }
    };
    FAbilityNodeDescriptor Flow;
    Flow.TypeId = TEXT("Delivery.ExtensionProjectile");
    Flow.ConfigClass = UAbilityExtensionProjectileNodeConfig::StaticClass();
    Flow.ExecutionClass = UAbilityExtensionProjectileNodeExecution::StaticClass();
    Flow.InputPayload = EAbilityPortPayload::Origin;
    Flow.bPrototypeCompatibilitySafe = Flow.bIsAsync = true;
    Flow.bSupportsActorTargets = Flow.bSupportsMassTargets = Flow.bSupportsSnapshotTargets = true;
    Flow.Ports = {{TEXT("Contact"), EAbilityPortPayload::TargetSet, false},
                  {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Flow);
    Flow.TypeId = TEXT("Control.ExtensionSequence");
    Flow.ConfigClass = UAbilityExtensionSequenceNodeConfig::StaticClass();
    Flow.ExecutionClass = UAbilityExtensionSequenceNodeExecution::StaticClass();
    Add(Flow);
    Flow.TypeId = TEXT("State.ExtensionSurface");
    Flow.ConfigClass = UAbilityExtensionSurfaceNodeConfig::StaticClass();
    Flow.ExecutionClass = UAbilityExtensionSurfaceNodeExecution::StaticClass();
    Flow.Ports = {{TEXT("Expired"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Flow);
    Flow.TypeId = TEXT("Control.ExtensionContactEnd");
    Flow.ConfigClass = UAbilityExtensionContactEndNodeConfig::StaticClass();
    Flow.ExecutionClass = UAbilityExtensionContactEndNodeExecution::StaticClass();
    Flow.bIsAsync = false;
    Flow.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    Add(Flow);
}
