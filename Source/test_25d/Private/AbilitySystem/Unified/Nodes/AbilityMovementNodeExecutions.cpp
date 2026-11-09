// 调用链：原动画宿主/Movement.Character节点 -> 当前进度增量 -> 原地面和胶囊移动 -> 受阻或正常清理。
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Engine/World.h"

namespace
{
    ACharacter* CurrentCharacter(const FAbilitySourceToken& Source, const TFunction<bool()>& IsCurrent)
    {
        AActor* Avatar = nullptr;
        return AbilityExecutionIdentity::ResolveSource(Source, Avatar) && (!IsCurrent || IsCurrent())
                   ? Cast<ACharacter>(Avatar)
                   : nullptr;
    }
} // namespace

UAbilityMovementOwnershipComponent* UAbilityMovementOwnershipComponent::FindOrCreate(ACharacter* Character)
{
    if (!IsValid(Character) || !Character->HasAuthority())
    {
        return nullptr;
    }
    if (auto* Existing = Character->FindComponentByClass<UAbilityMovementOwnershipComponent>())
    {
        return Existing;
    }
    auto* Component = NewObject<UAbilityMovementOwnershipComponent>(Character);
    Character->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

bool UAbilityMovementOwnershipComponent::HasActiveClaim(const ACharacter* Character)
{
    const auto* Ownership =
        IsValid(Character) ? Character->FindComponentByClass<UAbilityMovementOwnershipComponent>() : nullptr;
    AActor* Avatar = nullptr;
    return Ownership && Ownership->OwnershipLease.IsValid() && Ownership->ExecutionOwner.IsValid() &&
           Ownership->Movement.Get() == Character->GetCharacterMovement() &&
           AbilityExecutionIdentity::ResolveSource(Ownership->CapturedSource, Avatar) && Avatar == Character;
}

FGuid UAbilityMovementOwnershipComponent::TryAcquire(UObject* Requester, const FAbilitySourceToken& Source,
                                                     FString& Error)
{
    Error.Reset();
    AActor* Avatar = nullptr;
    if (!IsValid(Requester) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) || Avatar != GetOwner())
    {
        Error = TEXT("Movement ownership requires the current source and execution owner.");
        return {};
    }
    if (OwnershipLease.IsValid() && ExecutionOwner.IsValid())
    {
        AActor* OriginalAvatar = nullptr;
        if (AbilityExecutionIdentity::ResolveSource(CapturedSource, OriginalAvatar))
        {
            Error = TEXT("Character movement is already owned by another execution.");
            return {};
        }
    }
    if (OwnershipLease.IsValid())
    {
        // 异常销毁的执行对象不保活；下一次获取前先回收其尚未释放的原状态。
        ReleaseOwnedState(false);
    }
    if (OwnershipLease.IsValid() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar) || Avatar != GetOwner())
    {
        Error = TEXT("Movement ownership or source changed during stale-lease cleanup.");
        return {};
    }
    auto* Character = Cast<ACharacter>(Avatar);
    auto* Component = Character ? Character->GetCharacterMovement() : nullptr;
    if (!Component || Component->MovementMode == MOVE_None)
    {
        Error = TEXT("Character movement is unavailable or already disabled by an external owner.");
        return {};
    }
    Movement = Component;
    ExecutionOwner = Requester;
    CapturedSource = Source;
    SavedMovementMode = Component->MovementMode;
    bSavedDesiredRotation = Component->bUseControllerDesiredRotation;
    OwnershipLease = FGuid::NewGuid();
    return OwnershipLease;
}

bool UAbilityMovementOwnershipComponent::Release(UObject* Requester, const FGuid& Lease, bool bRestoreAsFalling)
{
    if (!Lease.IsValid() || Lease != OwnershipLease || ExecutionOwner.Get() != Requester)
    {
        return false;
    }
    ReleaseOwnedState(bRestoreAsFalling);
    return true;
}

void UAbilityMovementOwnershipComponent::ReleaseOwnedState(bool bRestoreAsFalling)
{
    auto* Component = Movement.Get();
    AActor* Avatar = nullptr;
    const bool bSameLife = AbilityExecutionIdentity::ResolveSource(CapturedSource, Avatar);
    const uint8 PreviousMode = SavedMovementMode;
    const bool PreviousRotation = bSavedDesiredRotation;
    ExecutionOwner.Reset();
    OwnershipLease.Invalidate();
    Movement.Reset();
    CapturedSource = {};
    if (bSameLife && Component && Component->GetOwner() == Avatar)
    {
        // 先恢复无回调字段，最后恢复模式；模式变更回调可获取新租约而不再被旧清理覆盖。
        Component->bUseControllerDesiredRotation = PreviousRotation;
        if (Component->MovementMode == MOVE_None)
        {
            Component->SetMovementMode(bRestoreAsFalling ? MOVE_Falling : EMovementMode(PreviousMode));
        }
    }
}

void AbilityMovementActions::GroundDash(const FAbilitySourceToken& Source, const FVector& Forward,
                                        float DesiredDistance, int32 OriginRoom, FAbilityCharacterMoveState& State,
                                        const TFunction<bool()>& IsCurrent)
{
    if (State.bBlocked || !FMath::IsFinite(DesiredDistance) || Forward.ContainsNaN())
    {
        return;
    }
    float Remaining = FMath::Max(0.f, DesiredDistance - State.AppliedDistance);
    while (Remaining > KINDA_SMALL_NUMBER)
    {
        ACharacter* Player = CurrentCharacter(Source, IsCurrent);
        if (!Player)
        {
            return;
        }
        const float Step = FMath::Min(Remaining, 20.f);
        const FVector From = Player->GetActorLocation();
        FVector Next = From + Forward * Step;
        if (auto* Director = ADungeonRunDirector::Find(Source.World.Get());
            Director && (OriginRoom == INDEX_NONE || Director->GetRoomIndex(Next) != OriginRoom))
        {
            State.bBlocked = true;
            return;
        }
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(StanderDashFloor), false, Player);
        const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (!Source.World->LineTraceSingleByObjectType(Floor, Next + FVector(0, 0, 30), Next - FVector(0, 0, Half + 70),
                                                       FCollisionObjectQueryParams(ECC_WorldStatic), Query) ||
            Floor.ImpactNormal.Z < .7f || FMath::Abs(Floor.ImpactPoint.Z + Half + 2 - From.Z) > 35)
        {
            State.bBlocked = true;
            return;
        }
        Next.Z = Floor.ImpactPoint.Z + Half + 2;
        FHitResult Block;
        Player->SetActorLocation(Next, true, &Block, ETeleportType::None);
        State.AppliedDistance += Step;
        Remaining -= Step;
        if (Block.bBlockingHit)
        {
            State.bBlocked = true;
            return;
        }
    }
}

void AbilityMovementActions::WeaponArc(const FAbilitySourceToken& Source, const FVector& Forward, float Distance,
                                       float Height, float BeforeFraction, float AfterFraction, int32 OriginRoom,
                                       FAbilityCharacterMoveState& State, const TFunction<bool()>& IsCurrent)
{
    if (AfterFraction <= BeforeFraction || !FMath::IsFinite(Distance) || !FMath::IsFinite(Height) ||
        !FMath::IsFinite(BeforeFraction) || !FMath::IsFinite(AfterFraction) || Forward.ContainsNaN())
    {
        return;
    }
    float Remaining = State.bBlocked ? 0.f : Distance * (AfterFraction - BeforeFraction);
    const float JumpHeight = UStanderWeaponActionConfig::JumpHeight(AfterFraction, Height);
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Remaining) / 20.f));
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        ACharacter* Player = CurrentCharacter(Source, IsCurrent);
        if (!Player)
        {
            return;
        }
        const FVector From = Player->GetActorLocation();
        FVector Next = From + Forward * (Remaining / Steps);
        if (auto* Director = ADungeonRunDirector::Find(Source.World.Get());
            Director && (OriginRoom == INDEX_NONE || Director->GetRoomIndex(Next) != OriginRoom))
        {
            State.bBlocked = true;
            Next = From;
            Remaining = 0.f;
        }
        const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float GroundCenter = From.Z - State.JumpOffset;
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WeaponMoveFloor), false, Player);
        const FVector Probe(Next.X, Next.Y, GroundCenter);
        if (!Source.World->LineTraceSingleByObjectType(Floor, Probe + FVector(0, 0, 30),
                                                       Probe - FVector(0, 0, Half + 70),
                                                       FCollisionObjectQueryParams(ECC_WorldStatic), Query) ||
            Floor.ImpactNormal.Z < .7f || FMath::Abs(Floor.ImpactPoint.Z + Half + 2 - GroundCenter) > 35)
        {
            State.bBlocked = true;
            return;
        }
        Next.Z = Floor.ImpactPoint.Z + Half + 2 + JumpHeight;
        FHitResult Hit;
        Player->SetActorLocation(Next, true, &Hit);
        State.JumpOffset = FMath::Max(0.f, Player->GetActorLocation().Z - (Floor.ImpactPoint.Z + Half + 2));
        if (Hit.bBlockingHit)
        {
            State.bBlocked = true;
            return;
        }
    }
}

bool UAbilityCharacterMoveNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Distance) || FMath::Abs(Distance) > 10000.f || !FMath::IsFinite(Height) || Height < 0.f ||
        Height > 10000.f || !FMath::IsFinite(Duration) || Duration <= 0.f || Duration > 60.f ||
        (Profile == EAbilityCharacterMoveProfile::GroundDash && Distance < 0.f))
    {
        Error = TEXT("Character movement requires finite bounded distance/height and positive duration.");
    }
    return Error.IsEmpty();
}

bool UAbilityCharacterMoveNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                               const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                               FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    ACharacter* Player = CurrentCharacter(Input.SourceToken, {});
    if (!Player)
    {
        Error = TEXT("Character movement requires a valid Character source.");
        return false;
    }
    Settings = DuplicateObject(CastChecked<UAbilityCharacterMoveNodeConfig>(&Config), this);
    if (auto* Director = ADungeonRunDirector::Find(Input.SourceToken.World.Get()))
    {
        OriginRoom = Director->GetRoomIndex(Player->GetActorLocation());
    }
    MovementOwnership = UAbilityMovementOwnershipComponent::FindOrCreate(Player);
    MovementLease = MovementOwnership ? MovementOwnership->TryAcquire(this, Input.SourceToken, Error) : FGuid();
    if (!MovementLease.IsValid())
    {
        return false;
    }
    auto* Movement = Player->GetCharacterMovement();
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
    return true;
}

void UAbilityCharacterMoveNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished() || !Settings)
    {
        return;
    }
    const float Before = FMath::Clamp(Elapsed / Settings->Duration, 0.f, 1.f);
    Elapsed += Delta;
    const float After = FMath::Clamp(Elapsed / Settings->Duration, 0.f, 1.f);
    const FVector Forward = Input.Direction.GetSafeNormal2D();
    const TFunction<bool()> IsCurrent = [&]()
    {
        return !bStopped && !Owner->IsFinished();
    };
    if (Settings->Profile == EAbilityCharacterMoveProfile::GroundDash)
    {
        AbilityMovementActions::GroundDash(Input.SourceToken, Forward, Settings->Distance * After, OriginRoom, State,
                                           IsCurrent);
    }
    else
    {
        AbilityMovementActions::WeaponArc(Input.SourceToken, Forward, Settings->Distance, Settings->Height, Before,
                                          After, OriginRoom, State, IsCurrent);
    }
    if (bStopped || Owner->IsFinished())
    {
        return;
    }
    auto Output = Input;
    if (AActor* Avatar = Input.SourceToken.Avatar.Get())
    {
        Output.Origin = Avatar->GetActorLocation();
    }
    if (State.bBlocked && !bBlockedPublished)
    {
        bBlockedPublished = true;
        if (!Owner->Publish(InvocationId, TEXT("Blocked"), Output))
        {
            return;
        }
    }
    if (After >= 1.f || (State.bBlocked && Settings->Profile == EAbilityCharacterMoveProfile::GroundDash))
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Output);
    }
}

void UAbilityCharacterMoveNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    if (MovementOwnership)
    {
        MovementOwnership->Release(this, MovementLease, State.JumpOffset > 5.f);
        MovementOwnership = nullptr;
    }
    MovementLease.Invalidate();
    Settings = nullptr;
}

void AbilityMovementNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Move;
    Move.TypeId = TEXT("Movement.Character");
    Move.ConfigClass = UAbilityCharacterMoveNodeConfig::StaticClass();
    Move.ExecutionClass = UAbilityCharacterMoveNodeExecution::StaticClass();
    Move.InputPayload = EAbilityPortPayload::Origin;
    Move.bSupportsActorTargets = Move.bSupportsSnapshotTargets = true;
    Move.bIsAsync = true;
    Move.Ports = {{TEXT("Blocked"), EAbilityPortPayload::Origin, false},
                  {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    if (!Registry.Find(Move.TypeId))
    {
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Move, Error), TEXT("Movement action registration: %s"), *Error);
    }
}
