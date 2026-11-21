// 调用链：Scope冻结值 -> 库存/真实提交信号 -> 精确PreparedInventory -> 原PM或轻量运动 -> 外部payload。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/AbilityPrototypeFlightQueries.h"
#include "AbilitySystem/Unified/AbilityProjectileMotion.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "PureMass/PureMassAbility.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityPrototypeOrbitDetails
{
    const FName InventorySlot(TEXT("PrototypeOrbitInventory"));
    const FName PresentationSlot(TEXT("PrototypePresentation"));

    /** 子片段沿原host位置/方向进入，不继承祖先命中结果。 */
    FAbilityNodeInput Position(const FAbilityNodeInput& InInput, FVector Anchor,
                               const TOptional<FAbilityTargetToken>& Target)
    {
        auto Result = InInput;
        Result.Origin = Anchor;
        Result.EventCenter = FVector::ZeroVector;
        Result.TargetSet.Reset();
        Result.Impact.Reset();
        Result.LiveTargetToken = Target;
        Result.bConfirmedHit = Result.bHasDamageResult = Result.bDamageAttempted = Result.bKilled = false;
        Result.DamageApplied.Reset();
        Result.ActualDamage.Reset();
        return Result;
    }
} // namespace AbilityPrototypeOrbitDetails

UAbilityPrototypeOrbitLaunchFrame* UAbilityPrototypeOrbitLaunchFrame::Create(
    UObject* Outer, UAbilityGraphInstance& Owner, UAbilityPrototypeParameterFrame& InParameters, FGuid InSlot,
    FVector InPosition, float InDelta, const FAbilitySourceToken& Source)
{
    if (!InParameters.BelongsTo(Owner.GetExecutionId(), Source) || !InSlot.IsValid() || InPosition.ContainsNaN() ||
        !FMath::IsFinite(InDelta) || InDelta < 0.f)
    {
        return nullptr;
    }
    auto* Result = NewObject<UAbilityPrototypeOrbitLaunchFrame>(Outer);
    if (!Result->BindExecution(Owner.GetExecutionId(), Source))
    {
        return nullptr;
    }
    Result->Parameters = &InParameters;
    Result->Slot = InSlot;
    Result->StartPosition = InPosition;
    Result->LaunchDelta = InDelta;
    return Result;
}

const UAbilityPrototypeParameterFrame* UAbilityPrototypeOrbitLaunchFrame::GetParameters() const
{
    return Parameters;
}
FGuid UAbilityPrototypeOrbitLaunchFrame::GetSlot() const
{
    return Slot;
}
FVector UAbilityPrototypeOrbitLaunchFrame::GetStartPosition() const
{
    return StartPosition;
}
float UAbilityPrototypeOrbitLaunchFrame::GetLaunchDelta() const
{
    return LaunchDelta;
}

bool UAbilityPrototypeOrbitInventoryNodeExecution::IsActorProfile() const
{
    return Frame && Frame->GetProfile() == TEXT("PrototypeFine.Actor.v1");
}

bool UAbilityPrototypeOrbitInventoryNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                         const UAbilityNodeConfig& Configuration,
                                                         const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Frame =
        const_cast<UAbilityPrototypeParameterFrame*>(AbilityPrototypeParameterNodes::ResolveFrame(Owner, Input, Error));
    if (!Frame || Frame->GetParameters().Kind != EAbilityPrototypeKind::Orbit ||
        !Frame->GetParameters().OrbitSettings || !Frame->GetParameters().OrbitSettings->IsValidConfiguration() ||
        (Frame->GetProfile() != TEXT("PrototypeFine.Actor.v1") &&
         Frame->GetProfile() != TEXT("PrototypeFine.Mixed.v1")))
    {
        Error = TEXT("Prototype Orbit requires this root's valid Actor/Mixed inventory parameters");
        return false;
    }
    auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get());
    if (!ASC || !Cast<UPrototypeCastContext>(Input.Context.Get()))
    {
        Error = TEXT("Prototype Orbit requires its captured cast context and source attack ASC");
        return false;
    }
    Age = RefillClock = LaunchClock = ManaClock = 0.f;
    bClosing = false;
    Anchor = Input.Origin;
    Current = Input.LiveTargetToken;
    Slots.Reset();
    ActorSlots.Reset();
    LaunchFrames.Reset();
    Visual = {};
    Visual.bReady = true;
    Visual.Transform = FTransform(Input.Direction.Rotation(), Anchor);
    PresentationHost = AbilityPrototypePresentationService::Begin(
        Input.SourceToken, Frame->GetParameters(), Frame->GetStableReference(), Visual,
        IsActorProfile() ? EAbilityPrototypePresentationProfile::Actor : EAbilityPrototypePresentationProfile::Mixed,
        Error);
    if (!PresentationHost ||
        !Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeOrbitDetails::PresentationSlot, PresentationHost) ||
        !Owner.RegisterRuntimeResource(*Frame, AbilityPrototypeOrbitDetails::InventorySlot, this))
    {
        AbilityPrototypePresentationService::FinishOriginal(PresentationHost, true);
        Error = TEXT("Prototype Orbit could not bind its exact inventory/presentation resources");
        return false;
    }
    // 委托只有弱UObject接收者，Stop按准确handle解绑；ASC重绑Avatar时原source验证会关闭旧调用。
    AttackHandle = IsActorProfile()
                       ? ASC->OffensiveAttackTargeted.AddUObject(this, &ThisClass::HandleActorAttack)
                       : ASC->PrototypeMixedCommittedAttack.AddUObject(this, &ThisClass::HandleMixedAttack);
    return true;
}

void UAbilityPrototypeOrbitInventoryNodeExecution::Advance(float DeltaSeconds)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bClosing || !Frame || !Owner || Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeOrbitInventoryNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeParameterFrame> FrameGuard(Frame.Get());
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    Age += DeltaSeconds;
    const auto& Parameters = Frame->GetParameters();
    const bool bActor = IsActorProfile();
    const bool bPay = !bActor || ManaClock + DeltaSeconds >= 1.f;
    const float Cost = AbilityPrototypeTargetServices::ManaDue(ManaClock, DeltaSeconds, Parameters.ManaPerSecond,
                                                               bActor ? EAbilityPrototypeTargetProfile::Actor
                                                                      : EAbilityPrototypeTargetProfile::Mixed);
    const bool bPaid =
        !bPay || (bActor ? APrototypeRuntime::SpendMana(Avatar, Cost) : UPureMassAbility::SpendMana(Avatar, Cost));
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return;
    }
    if (!bPaid)
    {
        Owner->Cancel(EAbilityGraphEndReason::Cancelled);
        return;
    }
    // 原Orbit根是开关，只有逻辑孩子按Duration有限结束；扣费位于这个截止之前。
    if (Frame->GetLogicalDepth() > 0 && Age >= Parameters.Duration)
    {
        CompleteInventory();
        return;
    }
    Anchor = Avatar->GetActorLocation();
    const auto& Settings = *Parameters.OrbitSettings;
    RefillClock += DeltaSeconds;
    LaunchClock += DeltaSeconds;
    if (RefillClock >= Settings.SpawnInterval)
    {
        RefillClock = FMath::Fmod(RefillClock, Settings.SpawnInterval);
        if (Slots.Num() < Settings.MaxFireBolts && !Replenish())
        {
            return;
        }
        if (!bStopped && !Owner->IsFinished() && Owner->HasSuccessor(InvocationId, TEXT("Period")))
        {
            Owner->RequestChild(InvocationId, TEXT("Period"),
                                AbilityPrototypeOrbitDetails::Position(Input, Anchor, Current));
        }
    }
    if (bStopped || !Frame || Owner->IsFinished())
    {
        return;
    }
    FVector TargetPosition;
    const bool bTarget =
        Current.IsSet() &&
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Current.GetValue(), TargetPosition) &&
        FVector::DistSquared(Anchor, TargetPosition) <= FMath::Square(Settings.MaxTargetDistance);
    if (!bTarget)
    {
        Current.Reset();
        TArray<FGuid> Returning;
        for (const auto& Slot : Slots)
        {
            if (Slot.bLaunched || Slot.bLaunchPending)
            {
                Returning.Add(Slot.Id);
            }
        }
        for (const auto Id : Returning)
        {
            ReturnSlot(Id);
        }
    }
    else if (LaunchClock >= Settings.LaunchInterval &&
             (bActor || PureMassCombat::Visible(Input.SourceToken.World.Get(), Avatar, Anchor, Current->Target,
                                                PresentationHost)))
    {
        if (bActor)
        {
            // 原Actor即使所有库存都在飞行，也消费本次launch时钟；Mixed仅实际发射才清零。
            LaunchClock = 0.f;
        }
        for (auto& Slot : Slots)
        {
            if (!Slot.bLaunched && !Slot.bLaunchPending)
            {
                if (Launch(Slot, DeltaSeconds) && !bActor)
                {
                    LaunchClock = 0.f;
                }
                break;
            }
        }
    }
    if (!bStopped && !Owner->IsFinished())
    {
        ArrangeAndAdvance(DeltaSeconds);
        RefreshPresentation();
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        bClosing = true;
        if (Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired)
        {
            if (auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get()))
            {
                Context->bCancelled = true;
            }
        }
        if (auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get()))
        {
            if (IsActorProfile())
            {
                ASC->OffensiveAttackTargeted.Remove(AttackHandle);
            }
            else
            {
                ASC->PrototypeMixedCommittedAttack.Remove(AttackHandle);
            }
        }
        AttackHandle.Reset();
        TArray<FGuid> Owned;
        for (const auto& Slot : Slots)
        {
            Owned.Add(Slot.Id);
        }
        for (const auto Id : Owned)
        {
            ConsumeSlot(Id);
        }
        auto* Owner = GraphOwner.Get();
        if (Owner && Frame)
        {
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeOrbitDetails::InventorySlot, this);
            Owner->UnregisterRuntimeResource(*Frame, AbilityPrototypeOrbitDetails::PresentationSlot, PresentationHost);
        }
        AbilityPrototypePresentationService::FinishOriginal(
            PresentationHost, Reason != EAbilityGraphEndReason::Completed,
            !IsActorProfile() && Reason == EAbilityGraphEndReason::Completed);
        PresentationHost = nullptr;
        Frame = nullptr;
        Current.Reset();
        LaunchFrames.Reset();
        ActorSlots.Reset();
    }
    Super::Stop(Reason);
}

int32 UAbilityPrototypeOrbitInventoryNodeExecution::GetInventoryCount() const
{
    return Slots.Num();
}

int32 UAbilityPrototypeOrbitInventoryNodeExecution::GetLaunchedCount() const
{
    int32 Result = 0;
    for (const auto& Slot : Slots)
    {
        Result += Slot.bLaunched;
    }
    return Result;
}

/** Actor信号已由ASC提交门校验；保持原接收者对非空Ability的额外保护。 */
void UAbilityPrototypeOrbitInventoryNodeExecution::HandleActorAttack(UGameplayAbility* Ability, AActor* TargetActor)
{
    if (Ability)
    {
        SetTarget(FPureMassCombatTarget::FromActor(TargetActor));
    }
}

/** Mixed的现有NotifyCommittedAttack通过原门后才发这个server-local信号。 */
void UAbilityPrototypeOrbitInventoryNodeExecution::HandleMixedAttack(UGameplayAbility* Ability,
                                                                     FPureMassCombatTarget Candidate)
{
    if (Ability)
    {
        SetTarget(Candidate);
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::SetTarget(FPureMassCombatTarget Candidate)
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    FVector Location;
    FAbilityTargetToken Token;
    FString Error;
    if (bStopped || bClosing || !Frame || !Owner || Owner->IsFinished() ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) ||
        !Owner->ResolveOrCaptureOriginalTarget(Candidate, Token, Location, Error) ||
        FVector::DistSquared(Avatar->GetActorLocation(), Location) >
            FMath::Square(Frame->GetParameters().OrbitSettings->MaxTargetDistance) ||
        (IsActorProfile() && !Candidate.IsActor()))
    {
        return;
    }
    Current = Token;
}

FAbilityPrototypeOrbitSlot* UAbilityPrototypeOrbitInventoryNodeExecution::FindSlot(FGuid Id)
{
    return Slots.FindByPredicate(
        [&](const FAbilityPrototypeOrbitSlot& Slot)
        {
            return Slot.Id == Id;
        });
}

/** 补充是真实物理资源准入一次，不占用Context逻辑生成；视觉宿主不重复收费。 */
bool UAbilityPrototypeOrbitInventoryNodeExecution::Replenish()
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    if (!Frame || !Owner || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return false;
    }
    FAbilityPrototypeOrbitSlot Slot;
    Slot.Id = FGuid::NewGuid();
    if (!Owner->ReservePhysicalSpawnWithLease(InvocationId, Slot.PhysicalLease))
    {
        Owner->Cancel(EAbilityGraphEndReason::BudgetExceeded);
        return false;
    }
    const auto& Settings = *Frame->GetParameters().OrbitSettings;
    if (IsActorProfile())
    {
        const FTransform Transform(FRotator::ZeroRotator,
                                   Anchor + FVector(Settings.OrbitRadius, 0, Settings.OrbitHeight));
        auto* Bolt = Avatar->GetWorld()->SpawnActorDeferred<AMyOrbitingFireBolt>(
            Settings.FireBoltClass, Transform, Avatar, Cast<APawn>(Avatar),
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Bolt)
        {
            Owner->ReleasePhysicalSpawnLease(Slot.PhysicalLease);
            return true;
        }
        Bolt->DamageEffectParams = Input.DamageParams;
        Bolt->DamageEffectParams.BaseDamage = Frame->GetDamage();
        Bolt->DamageEffectParams.DamageType = Frame->GetParameters().DamageType;
        Bolt->SetExecutionSourceToken(Input.SourceToken);
        Bolt->SetCastContext(Cast<UPrototypeCastContext>(Input.Context.Get()));
        Bolt->InitializeOrbit(Avatar, Frame->GetParameters().OrbitSettings, 0.f);
        Slot.Actor = Bolt;
        Slot.Position = Transform.GetLocation();
        ActorSlots.Add(Bolt, Slot.Id);
        Bolt->OnDestroyed.AddUniqueDynamic(this, &ThisClass::ResourceDestroyed);
        Slots.Add(Slot);
        Bolt->FinishSpawning(Transform);
    }
    else
    {
        const int32 Index = Slots.Num();
        const float Angle = Age * Settings.OrbitDegreesPerSecond + 360.f * Index / (Index + 1);
        Slot.Position =
            Anchor + FVector(Settings.OrbitRadius, 0, Settings.OrbitHeight).RotateAngleAxis(Angle, FVector::UpVector);
        Slots.Add(Slot);
    }
    return !bStopped && !Owner->IsFinished();
}

bool UAbilityPrototypeOrbitInventoryNodeExecution::Launch(FAbilityPrototypeOrbitSlot& Slot, float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (!Owner || !Frame || !Current.IsSet() || !Owner->HasSuccessor(InvocationId, TEXT("Launch")))
    {
        return false;
    }
    const FGuid Id = Slot.Id;
    const FVector Start = Slot.Actor.IsValid() ? Slot.Actor->GetActorLocation() : Slot.Position;
    auto* LaunchFrame =
        UAbilityPrototypeOrbitLaunchFrame::Create(this, *Owner, *Frame, Id, Start, Delta, Input.SourceToken);
    if (!LaunchFrame ||
        !Owner->RegisterRuntimeResource(*LaunchFrame, AbilityPrototypeOrbitDetails::InventorySlot, this))
    {
        Owner->Cancel(EAbilityGraphEndReason::Failed);
        return false;
    }
    Slot.bLaunchPending = true;
    LaunchFrames.Add(Id, LaunchFrame);
    auto Payload = AbilityPrototypeOrbitDetails::Position(Input, Start, Current);
    Payload.ParameterFrame = LaunchFrame;
    if (!Owner->Publish(InvocationId, TEXT("Launch"), Payload))
    {
        ReturnSlot(Id);
        return false;
    }
    Owner->FlushImmediatePayloads(true);
    const auto* After = FindSlot(Id);
    return !bStopped && !Owner->IsFinished() && After && After->bLaunched;
}

/** 原Mixed按库存逆序推进；这里调用已准备投递的物理步骤，不建立另一套图/任务执行器。 */
void UAbilityPrototypeOrbitInventoryNodeExecution::ArrangeAndAdvance(float Delta)
{
    if (!Frame)
    {
        return;
    }
    const auto& Settings = *Frame->GetParameters().OrbitSettings;
    if (IsActorProfile())
    {
        const int32 OrbitCount = Slots.Num() - GetLaunchedCount();
        int32 Phase = 0;
        for (auto& Slot : Slots)
        {
            if (Slot.Actor.IsValid() && !Slot.bLaunched)
            {
                Slot.Actor->SetOrbitPhase(360.f * Phase++ / FMath::Max(1, OrbitCount));
            }
        }
        return;
    }
    TArray<FGuid> Order;
    for (int32 Index = Slots.Num() - 1; Index >= 0; --Index)
    {
        Order.Add(Slots[Index].Id);
    }
    for (const auto Id : Order)
    {
        if (bStopped || !Frame)
        {
            return;
        }
        auto* Slot = FindSlot(Id);
        if (!Slot)
        {
            continue;
        }
        if (Slot->bLaunched)
        {
            if (auto* Delivery = Slot->Delivery.Get())
            {
                Delivery->AdvanceLightResource(Delta);
            }
        }
        else
        {
            const int32 Index = static_cast<int32>(Slot - Slots.GetData());
            const float Angle = Age * Settings.OrbitDegreesPerSecond + 360.f * Index / FMath::Max(1, Slots.Num());
            Slot->Position =
                Anchor +
                FVector(Settings.OrbitRadius, 0, Settings.OrbitHeight).RotateAngleAxis(Angle, FVector::UpVector);
        }
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::RefreshPresentation()
{
    if (!Frame || bStopped)
    {
        return;
    }
    Visual.Revision++;
    Visual.Transform.SetLocation(Anchor);
    Visual.OrbitPositions.Reset();
    if (!IsActorProfile())
    {
        for (const auto& Slot : Slots)
        {
            Visual.OrbitPositions.Add(Slot.Position);
        }
    }
    AbilityPrototypePresentationService::Update(PresentationHost, Visual);
}

void UAbilityPrototypeOrbitInventoryNodeExecution::ReturnSlot(FGuid Id)
{
    auto* Slot = FindSlot(Id);
    if (!Slot)
    {
        return;
    }
    const auto Delivery = Slot->Delivery;
    Slot->bLaunched = Slot->bLaunchPending = false;
    Slot->Delivery.Reset();
    if (auto* Bolt = Slot->Actor.Get())
    {
        Bolt->ClearGraphDeliveryHandlers();
        Bolt->ReturnToOrbit();
    }
    if (auto* Owner = GraphOwner.Get())
    {
        if (auto* Prepared = LaunchFrames.FindRef(Id).Get())
        {
            Owner->UnregisterRuntimeResource(*Prepared, AbilityPrototypeOrbitDetails::InventorySlot, this);
        }
    }
    LaunchFrames.Remove(Id);
    if (auto* Execution = Delivery.Get(); Execution && !Execution->bStopped)
    {
        Execution->Complete(false);
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::ConsumeSlot(FGuid Id)
{
    const int32 Index = Slots.IndexOfByPredicate(
        [&](const FAbilityPrototypeOrbitSlot& Slot)
        {
            return Slot.Id == Id;
        });
    if (Index == INDEX_NONE)
    {
        return;
    }
    const auto Owned = Slots[Index];
    Slots.RemoveAt(Index);
    auto* Owner = GraphOwner.Get();
    if (Owner)
    {
        Owner->ReleasePhysicalSpawnLease(Owned.PhysicalLease);
        if (auto* Prepared = LaunchFrames.FindRef(Id).Get())
        {
            Owner->UnregisterRuntimeResource(*Prepared, AbilityPrototypeOrbitDetails::InventorySlot, this);
        }
    }
    LaunchFrames.Remove(Id);
    if (auto* Bolt = Owned.Actor.Get())
    {
        ActorSlots.Remove(Bolt);
        Bolt->OnDestroyed.RemoveDynamic(this, &ThisClass::ResourceDestroyed);
        Bolt->ClearGraphDeliveryHandlers();
        Bolt->Destroy();
    }
    if (auto* Delivery = Owned.Delivery.Get(); Delivery && !Delivery->bCompleting && !bClosing)
    {
        Delivery->Complete(false);
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::ResourceDestroyed(AActor* Resource)
{
    const auto* Id = ActorSlots.Find(Resource);
    if (Id)
    {
        const FGuid Retired = *Id;
        ActorSlots.Remove(Resource);
        ConsumeSlot(Retired);
    }
}

void UAbilityPrototypeOrbitInventoryNodeExecution::CompleteInventory()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bClosing || !Owner || !Frame)
    {
        return;
    }
    bClosing = true;
    auto Payload = AbilityPrototypeOrbitDetails::Position(Input, Anchor, Current);
    if (Owner->HasSuccessor(InvocationId, TEXT("Expired")))
    {
        Owner->RequestChild(InvocationId, TEXT("Expired"), Payload);
    }
    Owner->Publish(InvocationId, TEXT("Completed"), Payload);
}

bool UAbilityPrototypeOrbitDeliveryNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                        const UAbilityNodeConfig& Configuration,
                                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    Ticket = Cast<UAbilityPrototypeOrbitLaunchFrame>(Input.ParameterFrame.Get());
    Inventory = Ticket ? Cast<UAbilityPrototypeOrbitInventoryNodeExecution>(
                             Owner.FindRuntimeResource(*Ticket, AbilityPrototypeOrbitDetails::InventorySlot))
                       : nullptr;
    auto* Host = Inventory.Get();
    auto* Slot = Host && Ticket ? Host->FindSlot(Ticket->GetSlot()) : nullptr;
    FVector Location;
    if (!Ticket || !Host || !Slot || !Slot->bLaunchPending || Slot->bLaunched ||
        Host->LaunchFrames.FindRef(Slot->Id).Get() != Ticket.Get() || !Input.LiveTargetToken.IsSet() ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Location))
    {
        Error = TEXT("Orbit delivery requires this root's exact pending inventory ticket and original live target");
        return false;
    }
    Target = Input.LiveTargetToken.GetValue();
    Position = Ticket->GetStartPosition();
    FlightAge = 0.f;
    bCompleting = false;
    Slot->bLaunchPending = false;
    Slot->bLaunched = true;
    Slot->Delivery = this;
    if (Host->IsActorProfile())
    {
        auto* Bolt = Slot->Actor.Get();
        if (!Bolt || !Target.Target.IsActor())
        {
            Error = TEXT("Actor Orbit delivery requires the prepared native orbit resource and Actor target");
            return false;
        }
        Actor = Bolt;
        const TWeakObjectPtr<UAbilityPrototypeOrbitDeliveryNodeExecution> WeakThis(this);
        Bolt->SetGraphDeliveryHandlers(
            [WeakThis](const FAbilityTargetToken& ContactTarget, FVector ContactPosition, FVector ContactDirection)
            {
                if (auto* Self = WeakThis.Get())
                {
                    return Self->ActorContact(ContactTarget, ContactPosition, ContactDirection);
                }
                return EAbilityProjectileContactDisposition::Terminate;
            },
            {});
        // 处理器在启用Sphere/PM之前安装；原生bolt自己的目标/施法生命门与返航行为仍保留。
        Bolt->LaunchAt(Target.Target.Actor.Get());
    }
    return true;
}

void UAbilityPrototypeOrbitDeliveryNodeExecution::Advance(float DeltaSeconds)
{
    if (bStopped || bCompleting || !Ticket)
    {
        return;
    }
    auto* Host = Inventory.Get();
    if (!Host || Host->bStopped || Host->bClosing || !Host->FindSlot(Ticket->GetSlot()))
    {
        Complete(false);
        return;
    }
    if (Host->IsActorProfile() && (!Actor.IsValid() || !Actor->IsLaunched()))
    {
        // 原Actor超时/失去目标是返航，不销毁库存；Mixed由原逆序轻量物理步骤单独推进。
        Complete(false);
    }
}

/** 原Mixed物理步骤由库存逆序调度一次；不是重新调用另一个图的Advance或激活GA。 */
void UAbilityPrototypeOrbitDeliveryNodeExecution::AdvanceLightResource(float Delta)
{
    auto* Host = Inventory.Get();
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Ticket || !Host || Host->IsActorProfile() || !Host->Frame || !Owner ||
        Owner->IsFinished())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityPrototypeOrbitDeliveryNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeOrbitLaunchFrame> TicketGuard(Ticket.Get());
    const auto& Parameters = Host->Frame->GetParameters();
    FVector TargetPosition;
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target, TargetPosition))
    {
        Complete(false);
        return;
    }
    FlightAge += Delta;
    const FVector End =
        FMath::VInterpConstantTo(Position, TargetPosition, Delta, Parameters.OrbitSettings->ProjectileSpeed);
    auto Resolve =
        [&](const FPureMassCombatTarget& Candidate, FAbilityTargetToken& Life, FVector& Location, FString& Error)
    {
        return Owner->ResolveOrCaptureOriginalTarget(Candidate, Life, Location, Error);
    };
    FAbilityPrototypeFlightQueryHit Hit;
    FHitResult Wall;
    FString Error;
    if (!AbilityPrototypeFlightQueries::SweepMixedSegment(Input.SourceToken, Position, End, Parameters.CollisionRadius,
                                                          {}, Resolve, Hit, Wall, Error, Host->PresentationHost))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    if (Hit.Candidate.IsSet())
    {
        if (Hit.Life.IsSet())
        {
            Contact(Hit.Life.GetValue(), Hit.Hit.Location, Input.Direction);
        }
        Complete(true);
        return;
    }
    if (Wall.bBlockingHit || FlightAge >= Parameters.OrbitSettings->MaxFlightSeconds)
    {
        Complete(true);
        return;
    }
    Position = End;
    if (auto* Slot = Host->FindSlot(Ticket->GetSlot()))
    {
        Slot->Position = Position;
    }
}

EAbilityProjectileContactDisposition UAbilityPrototypeOrbitDeliveryNodeExecution::ActorContact(
    const FAbilityTargetToken& ContactTarget, FVector ContactPosition, FVector ContactDirection)
{
    const TStrongObjectPtr<UAbilityPrototypeOrbitDeliveryNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<AMyOrbitingFireBolt> BoltGuard(Actor.Get());
    auto* Owner = GraphOwner.Get();
    FAbilityTargetToken Original;
    FVector Location;
    FString Error;
    if (bStopped || bCompleting || !Owner || !Actor.IsValid() ||
        !Owner->ResolveOrCaptureOriginalTarget(ContactTarget.Target, Original, Location, Error))
    {
        Complete(true);
        return EAbilityProjectileContactDisposition::Terminate;
    }
    // 原Actor先锁定终结表现，再调用唯一外部payload；不回落内部ApplyDamage或重复随机force。
    Actor->PlayGraphDeliveryImpactAt(ContactPosition, true);
    Contact(Original, ContactPosition, ContactDirection);
    Complete(true);
    return EAbilityProjectileContactDisposition::Terminate;
}

bool UAbilityPrototypeOrbitDeliveryNodeExecution::Contact(const FAbilityTargetToken& ContactTarget,
                                                          FVector ContactPosition, FVector ContactDirection)
{
    auto* Owner = GraphOwner.Get();
    auto* Host = Inventory.Get();
    if (bStopped || bCompleting || !Ticket || !Owner || Owner->IsFinished() || !Host || !Host->Frame)
    {
        return false;
    }
    const TStrongObjectPtr<UAbilityPrototypeOrbitDeliveryNodeExecution> SelfGuard(this);
    const TStrongObjectPtr<UAbilityPrototypeOrbitLaunchFrame> TicketGuard(Ticket.Get());
    const bool bActor = Host->IsActorProfile();
    FVector BeforeDamage;
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, ContactTarget, BeforeDamage))
    {
        return false;
    }
    auto Payload = Input;
    Payload.Origin = Payload.EventCenter = bActor ? ContactPosition : BeforeDamage;
    Payload.Direction = ContactDirection;
    Payload.LiveTargetToken = ContactTarget;
    Payload.TargetSet.Reset();
    FAbilityImpactSnapshot Impact;
    Impact.Location = Payload.Origin;
    Impact.Direction = ContactDirection;
    Impact.ConfirmedTarget = ContactTarget;
    Impact.Sequence = Owner->GetAcceptedEventCount() + 1;
    Impact.ContactDamageProfile = bActor                           ? EAbilityContactDamageProfile::NativeActorProjectile
                                  : ContactTarget.Target.IsActor() ? EAbilityContactDamageProfile::ConfiguredForces
                                                                   : EAbilityContactDamageProfile::NativeMassProjectile;
    Payload.Impact = Impact;
    Payload.bConfirmedHit = true;
    if (Owner->HasSuccessor(InvocationId, TEXT("Contact")))
    {
        if (!Owner->Publish(InvocationId, TEXT("Contact"), Payload))
        {
            return false;
        }
        Owner->FlushImmediatePayloads(true);
    }
    if (bStopped || !Ticket || Owner->IsFinished() || !Host->Frame)
    {
        return false;
    }
    auto Hook = AbilityPrototypeOrbitDetails::Position(Host->Input, Payload.Origin,
                                                       TOptional<FAbilityTargetToken>(ContactTarget));
    Hook.ParameterFrame = Host->Frame;
    if (bActor && IsValid(ContactTarget.Target.Actor.Get()))
    {
        Hook.Origin = ContactTarget.Target.Actor->GetActorLocation();
    }
    Hook.Impact = Payload.Impact;
    Hook.Impact->Location = Hook.Origin;
    FVector StillLive;
    if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, ContactTarget, StillLive))
    {
        Hook.LiveTargetToken.Reset();
    }
    if (Owner->HasSuccessor(InvocationId, TEXT("OnHit")))
    {
        Owner->RequestChild(InvocationId, TEXT("OnHit"), Hook);
    }
    return !bStopped && !Owner->IsFinished();
}

void UAbilityPrototypeOrbitDeliveryNodeExecution::Complete(bool bConsume)
{
    if (bStopped || bCompleting)
    {
        return;
    }
    bCompleting = true;
    auto* Owner = GraphOwner.Get();
    const TStrongObjectPtr<UAbilityPrototypeOrbitLaunchFrame> TicketGuard(Ticket.Get());
    auto Payload = Input;
    Payload.LiveTargetToken.Reset();
    Payload.Origin = Position;
    Payload.EventCenter = FVector::ZeroVector;
    if (Ticket)
    {
        Payload.ParameterFrame = const_cast<UAbilityPrototypeParameterFrame*>(Ticket->GetParameters());
        if (auto* Host = Inventory.Get())
        {
            if (bConsume)
            {
                Host->ConsumeSlot(Ticket->GetSlot());
            }
            else
            {
                Host->ReturnSlot(Ticket->GetSlot());
            }
        }
    }
    if (Owner && !Owner->IsFinished())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Payload);
    }
}

void UAbilityPrototypeOrbitDeliveryNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (!bStopped)
    {
        if (auto* Bolt = Actor.Get())
        {
            Bolt->ClearGraphDeliveryHandlers();
        }
        if (!bCompleting && Ticket)
        {
            bCompleting = true;
            if (auto* Host = Inventory.Get())
            {
                Host->ReturnSlot(Ticket->GetSlot());
            }
        }
        Ticket = nullptr;
        Inventory.Reset();
        Actor.Reset();
    }
    Super::Stop(Reason);
}

void AbilityPrototypeOrbitNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const bool bDelivery : {false, true})
    {
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = bDelivery ? TEXT("Delivery.PrototypeOrbit") : TEXT("Control.PrototypeOrbitInventory");
        Descriptor.ConfigClass = bDelivery ? UAbilityPrototypeOrbitDeliveryNodeConfig::StaticClass()
                                           : UAbilityPrototypeOrbitInventoryNodeConfig::StaticClass();
        Descriptor.ExecutionClass = bDelivery ? UAbilityPrototypeOrbitDeliveryNodeExecution::StaticClass()
                                              : UAbilityPrototypeOrbitInventoryNodeExecution::StaticClass();
        Descriptor.InputPayload = bDelivery ? EAbilityPortPayload::PreparedInventory : EAbilityPortPayload::Origin;
        Descriptor.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = Descriptor.bPrototypeCompatibilitySafe = true;
        if (!bDelivery)
        {
            // 根库存无Duration截止；有限深度孩子也保守声明，不能把新增异步后继放入无限发射/补充路径。
            Descriptor.PotentiallyUnboundedOutputPorts = {TEXT("Launch"), TEXT("Period")};
        }
        Descriptor.Ports =
            bDelivery ? TArray<FAbilityPortDescriptor>{{TEXT("Contact"), EAbilityPortPayload::LiveTarget, false},
                                                       {TEXT("OnHit"), EAbilityPortPayload::ConfirmedImpact, false},
                                                       {TEXT("Completed"), EAbilityPortPayload::Origin, true}}
                      : TArray<FAbilityPortDescriptor>{{TEXT("Launch"), EAbilityPortPayload::PreparedInventory, false},
                                                       {TEXT("Period"), EAbilityPortPayload::Origin, false},
                                                       {TEXT("Expired"), EAbilityPortPayload::Origin, false},
                                                       {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype Orbit registration: %s"), *Error);
        }
    }
}
