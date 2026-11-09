// 调用链：原生阶段/图Advance -> 原走廊扫掠或孢子时钟 -> 共享目标捕获/伤害；取消只释放本次预警和护甲句柄。
#include "AbilitySystem/Unified/Nodes/AbilityEnemySpecialNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "Character/GruxEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Engine/World.h"

void FShroomPulseClock::Reset(double Now, float InPeriod, float Duration)
{
    Started = Now;
    Ends = Now + Duration;
    Period = InPeriod;
    NextSlot = 0;
}

bool FShroomPulseClock::Consume(double Now)
{
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Period) || Period <= 0 || Now < Started || Now >= Ends)
    {
        return false;
    }
    const int32 Slot = FMath::FloorToInt((Now - Started) / Period + 1.e-6);
    if (Slot < NextSlot)
    {
        return false;
    }
    NextSlot = Slot + 1;
    return true;
}

bool UAbilityEnemySpecialNodeConfig::Validate(FString& Error) const
{
    if (!Super::Validate(Error))
    {
        return false;
    }
    if (!FMath::IsFinite(Duration) || Duration <= 0.f || !FMath::IsFinite(WarningSeconds) || WarningSeconds < .1f ||
        (!Shape.bExclamation && !Shape.bDrawFootprint) ||
        (Kind == EAbilityEnemySpecialKind::Charge && (Duration > 2.f || Shape.Shape != EStoneGolemShape::Rectangle)) ||
        (Kind == EAbilityEnemySpecialKind::Spore &&
         (!FMath::IsFinite(Period) || Period < .1f || Duration < Period || Duration > 10.f ||
          Duration / Period > 20.f || !Shape.bDrawFootprint)) ||
        (Kind == EAbilityEnemySpecialKind::Roar &&
         (!FMath::IsFinite(Armor) || Armor <= 0.f || Duration > 30.f || !FMath::IsFinite(Shape.PlayRate) ||
          Shape.PlayRate <= 0.f || !FMath::IsFinite(Shape.HitTime()) || !FMath::IsFinite(Shape.RecoveryEnd) ||
          Shape.RecoveryEnd <= Shape.HitTime())))
    {
        Error = TEXT("Enemy special requires visible warning, finite geometry and bounded charge/spore/armor timing.");
    }
    return Error.IsEmpty();
}

bool UAbilityEnemySpecialNodeConfig::MatchesType(FName TypeId) const
{
    return TypeId == (Kind == EAbilityEnemySpecialKind::Charge  ? TEXT("Move.GruxCharge")
                      : Kind == EAbilityEnemySpecialKind::Spore ? TEXT("Field.ShroomSpore")
                                                                : TEXT("Status.GruxRoarArmor"));
}

bool FAbilityEnemyAreaAction::Start(const FAbilityNodeInput& InInput, const UAbilityEnemySpecialNodeConfig& Config,
                                    FAbilityCharacterActionCallbacks InCallbacks, double Now, FString& Error)
{
    Input = InInput;
    Callbacks = MoveTemp(InCallbacks);
    Kind = Config.Kind;
    Shape = Config.Shape;
    Surface = Config.LockedSurface;
    Triangles = Config.LockedTriangles;
    Duration = Config.Duration;
    Yaw = Input.Direction.Rotation().Yaw;
    Started = Now;
    AActor* Avatar = nullptr;
    if (!Config.Validate(Error) || !FMath::IsFinite(Now) || !Current(Avatar) ||
        Kind == EAbilityEnemySpecialKind::Roar ||
        (Kind == EAbilityEnemySpecialKind::Charge && !Cast<ACharacter>(Avatar)))
    {
        Error = TEXT("Enemy area action requires a current source and valid charge/spore settings.");
        bStopped = true;
        return false;
    }
    StartPosition = PreviousPosition = Avatar->GetActorLocation();
    const auto* Director = ADungeonRunDirector::Find(Input.SourceToken.World.Get());
    ChargeRoom = Director ? Director->GetRoomIndex(StartPosition) : INDEX_NONE;
    Clock.Reset(Now, Config.Period, Config.Duration);
    return true;
}

bool FAbilityEnemyAreaAction::Current(AActor*& Avatar) const
{
    return !bStopped && AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) &&
           (!Callbacks.IsCurrent || Callbacks.IsCurrent());
}

void FAbilityEnemyAreaAction::Pulse(const TOptional<TPair<FVector, FVector>>& Segment)
{
    AActor* Avatar = nullptr;
    if (!Current(Avatar))
    {
        return;
    }
    AActor* Selected = nullptr;
    FVector Location;
    if (Input.LiveTargetToken.IsSet() &&
        AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *Input.LiveTargetToken, Location))
    {
        Selected = Input.LiveTargetToken->Target.Actor.Get();
    }
    TArray<AActor*> Targets;
    AbilityCharacterActions::GatherTelegraphedTargets(Input.SourceToken, Shape, Input.Origin, Yaw, Surface, Triangles,
                                                      Selected, Targets);
    if (Segment.IsSet())
    {
        Targets.RemoveAll(
            [&](AActor* Target)
            {
                FVector Point = Target->GetActorLocation();
                FVector A = Segment->Key, B = Segment->Value;
                Point.Z = A.Z = B.Z = 0;
                if (HitActors.Contains(Target) || FMath::PointDistToSegment(Point, A, B) > Shape.Width * .5f)
                {
                    return true;
                }
                HitActors.Add(Target);
                return false;
            });
    }
    if (Current(Avatar))
    {
        // 共享服务在捕获本批全部生命后报告首目标，报告观察者不能把旧选择替换成新生命。
        auto GuardedCallbacks = Callbacks;
        GuardedCallbacks.IsCurrent = [this]()
        {
            AActor* CurrentAvatar = nullptr;
            return Current(CurrentAvatar);
        };
        AbilityCharacterActions::ApplyTelegraphedTargets(Input.SourceToken, Input.DamageParams, Shape, Targets,
                                                         GuardedCallbacks);
    }
}

EAbilityEnemyActionState FAbilityEnemyAreaAction::AdvanceCharge(double Now)
{
    AActor* Avatar = nullptr;
    if (!Current(Avatar))
    {
        return EAbilityEnemyActionState::Failed;
    }
    auto* Character = CastChecked<ACharacter>(Avatar);
    const float Alpha = FMath::Clamp(float(Now - Started) / Duration, 0.f, 1.f);
    FVector Next = StartPosition + FRotator(0, Yaw, 0).Vector() * Shape.Length * Alpha;
    const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Floor;
    if (!StoneGolemGeometry::Project(Input.SourceToken.World.Get(), Avatar, Next - FVector(0, 0, Half),
                                     Shape.HeightTolerance, Floor))
    {
        return EAbilityEnemyActionState::Failed;
    }
    Next.Z = Floor.Z + Half + 2;
    if (const auto* Director = ADungeonRunDirector::Find(Input.SourceToken.World.Get());
        Director && Director->GetRoomIndex(Next) != ChargeRoom)
    {
        return EAbilityEnemyActionState::Failed;
    }
    FHitResult Hit;
    Character->SetActorLocation(Next, true, &Hit);
    if (!Current(Avatar))
    {
        return EAbilityEnemyActionState::Failed;
    }
    const FVector Actual = Avatar->GetActorLocation();
    Pulse(TPair<FVector, FVector>(PreviousPosition, Actual));
    if (!Current(Avatar))
    {
        return EAbilityEnemyActionState::Failed;
    }
    PreviousPosition = Actual;
    return Hit.bBlockingHit || Alpha >= 1.f ? EAbilityEnemyActionState::Completed : EAbilityEnemyActionState::Running;
}

EAbilityEnemyActionState FAbilityEnemyAreaAction::Advance(double Now)
{
    AActor* Avatar = nullptr;
    if (!FMath::IsFinite(Now) || !Current(Avatar))
    {
        return EAbilityEnemyActionState::Failed;
    }
    if (Kind == EAbilityEnemySpecialKind::Charge)
    {
        return AdvanceCharge(Now);
    }
    if (Now >= Clock.Ends)
    {
        return EAbilityEnemyActionState::Completed;
    }
    if (Clock.Consume(Now))
    {
        Pulse();
    }
    return Current(Avatar) ? EAbilityEnemyActionState::Running : EAbilityEnemyActionState::Failed;
}

void FAbilityEnemyAreaAction::Stop()
{
    bStopped = true;
}

bool AbilityEnemySpecialActions::PlanCharge(const FAbilitySourceToken& Source, const FVector& Center, float Yaw,
                                            FStoneGolemAttackTuning& Shape)
{
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return false;
    }
    auto* Character = Cast<ACharacter>(Avatar);
    if (!Character)
    {
        return false;
    }
    const auto* Capsule = Character->GetCapsuleComponent();
    const FVector Direction = FRotator(0, Yaw, 0).Vector();
    const FVector Start = Avatar->GetActorLocation();
    float Length = Shape.Length;
    FHitResult Hit;
    if (Source.World->SweepSingleByObjectType(
            Hit, Start, Start + Direction * Length, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 2),
            FCollisionQueryParams(SCENE_QUERY_STAT(GruxChargePlan), false, Avatar)))
    {
        Length = FMath::Max(0.f, Hit.Distance - 2.f);
    }
    const auto* Director = ADungeonRunDirector::Find(Source.World.Get());
    const int32 Room = Director ? Director->GetRoomIndex(Start) : INDEX_NONE;
    float Safe = 0;
    for (float D = FMath::Min(25.f, Length); D <= Length + .01f; D = FMath::Min(D + 25.f, Length))
    {
        FVector Floor;
        const FVector Point = Center + Direction * D;
        if (!StoneGolemGeometry::Project(Source.World.Get(), Avatar, Point, Shape.HeightTolerance, Floor) ||
            (Director && Director->GetRoomIndex(Point) != Room))
        {
            break;
        }
        Safe = D;
        if (D >= Length)
        {
            break;
        }
    }
    if (Safe < 25.f)
    {
        return false;
    }
    Shape.Length = Safe;
    return true;
}

FActiveGameplayEffectHandle AbilityEnemySpecialActions::ApplyRoar(const FAbilitySourceToken& Source, float Armor,
                                                                  float Duration)
{
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return {};
    }
    auto* Grux = Cast<AGruxEnemyCharacter>(Avatar);
    if (!Grux)
    {
        return {};
    }
    const auto Handle = Grux->ApplyRoarArmor(Armor, Duration);
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        RemoveRoar(Source, Handle);
        return {};
    }
    return Handle;
}

void AbilityEnemySpecialActions::RemoveRoar(const FAbilitySourceToken& Source, FActiveGameplayEffectHandle Handle)
{
    if (auto* Grux = Cast<AGruxEnemyCharacter>(Source.Avatar.Get()))
    {
        Grux->RemoveRoarArmor(Handle);
    }
}

bool UAbilityEnemySpecialNodeExecution::CurrentPresentation() const
{
    AActor* Avatar = nullptr;
    return !bStopped && AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) && Presenter.IsValid() &&
           !Presenter->bIsStunned && Presenter->IsAttackCurrent(Serial, Lease);
}

bool UAbilityEnemySpecialNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings =
        DuplicateObject<UAbilityEnemySpecialNodeConfig>(CastChecked<UAbilityEnemySpecialNodeConfig>(&Config), this);
    Presenter = Cast<ATelegraphedEnemyCharacter>(Input.SourceToken.Avatar.Get());
    if (!Settings->Validate(Error) || !Presenter.IsValid())
    {
        Error = TEXT("Enemy special graph requires the original telegraphed presentation host.");
        return false;
    }
    Lease = Presenter->GetDungeonPoolLease();
    if (Settings->Kind == EAbilityEnemySpecialKind::Charge &&
        !AbilityEnemySpecialActions::PlanCharge(Input.SourceToken, Input.Origin, Input.Direction.Rotation().Yaw,
                                                Settings->Shape))
    {
        Error = TEXT("Charge corridor has no safe ground segment.");
        return false;
    }
    MovementOwnership = UAbilityMovementOwnershipComponent::FindOrCreate(Presenter.Get());
    MovementLease = MovementOwnership ? MovementOwnership->TryAcquire(this, Input.SourceToken, Error) : FGuid();
    if (!MovementLease.IsValid())
    {
        return false;
    }
    Serial = Presenter->BeginAttack(Settings->Shape, Input.Origin, Input.Direction.Rotation().Yaw);
    if (!Serial)
    {
        Error = TEXT("Enemy presentation is already owned by another attack.");
        return false;
    }
    if (bStopped)
    {
        if (Presenter.IsValid() && Presenter->IsAttackCurrent(Serial, Lease))
        {
            Presenter->FinishAttack(Serial);
        }
        return true;
    }
    auto* Movement = Presenter->GetCharacterMovement();
    Movement->StopMovementImmediately();
    Movement->DisableMovement();
    if (bStopped || !CurrentPresentation())
    {
        return true;
    }
    Movement->bUseControllerDesiredRotation = false;
    if (auto* AI = Cast<AAIController>(Presenter->GetController()))
    {
        AI->StopMovement();
        AI->ClearFocus(EAIFocusPriority::Gameplay);
    }
    // 独立节点和BeginAttack的红色范围必须来自同一形状/落点，不能继承其他施法的锁定地面。
    // 原生动画前端直接启动FAbilityEnemyAreaAction，仍保留其真实锁定时刻的快照。
    StoneGolemGeometry::BuildSurface(Input.SourceToken.World.Get(), Presenter.Get(), Settings->Shape, Input.Origin,
                                     Input.Direction.Rotation().Yaw, Settings->LockedSurface,
                                     Settings->LockedTriangles);
    return true;
}

void UAbilityEnemySpecialNodeExecution::BeginAction()
{
    bActionStarted = true;
    ActionStartedAt = Elapsed;
    if (Settings->Kind == EAbilityEnemySpecialKind::Roar)
    {
        ArmorHandle = AbilityEnemySpecialActions::ApplyRoar(Input.SourceToken, Settings->Armor, Settings->Duration);
        if (!CurrentPresentation())
        {
            AbilityEnemySpecialActions::RemoveRoar(Input.SourceToken, ArmorHandle);
            return;
        }
        if (!ArmorHandle.IsValid())
        {
            GraphOwner->Cancel(EAbilityGraphEndReason::Failed);
            return;
        }
        Presenter->ShowImpact(Serial, Input.Origin);
        if (CurrentPresentation())
        {
            // 实际护甲已生效；保留原接触到RecoveryEnd的表现窗口，避免同帧Phase2被Phase0覆盖。
            GraphOwner->Publish(InvocationId, TEXT("Applied"), Input);
        }
        return;
    }
    Presenter->ShowActiveFootprint(Serial, Settings->Kind == EAbilityEnemySpecialKind::Spore ? &Input.Origin : nullptr);
    if (!CurrentPresentation())
    {
        return;
    }
    FAbilityCharacterActionCallbacks Callbacks;
    const TWeakObjectPtr<UAbilityEnemySpecialNodeExecution> WeakThis(this);
    Callbacks.IsCurrent = [WeakThis]()
    {
        auto* Self = WeakThis.Get();
        return Self && Self->CurrentPresentation();
    };
    Callbacks.AfterDamage =
        [WeakThis](const FAbilityTargetToken& Target, const FVector& Location, const FAbilityDamageResult& Result)
    {
        auto* Self = WeakThis.Get();
        if (!Self || !Self->CurrentPresentation())
        {
            return false;
        }
        auto Payload = Self->Input;
        Payload.LiveTargetToken.Reset();
        Payload.Impact = FAbilityImpactSnapshot{Location, Self->Input.Direction, Target, 0};
        Payload.bHasDamageResult = true;
        Payload.bDamageAttempted = Result.bAttempted;
        Payload.bKilled = Result.bKilled;
        Payload.DamageApplied = Result.Applied;
        Payload.ActualDamage = Result.ActualDamage;
        return Self->GraphOwner->Publish(Self->InvocationId, TEXT("DamageResolved"), Payload);
    };
    Action = MakeShared<FAbilityEnemyAreaAction>();
    FString Error;
    if (!Action->Start(Input, *Settings, MoveTemp(Callbacks), Elapsed, Error))
    {
        GraphOwner->Cancel(EAbilityGraphEndReason::Failed);
    }
}

void UAbilityEnemySpecialNodeExecution::Advance(float DeltaSeconds)
{
    if (!CurrentPresentation())
    {
        if (!bStopped && GraphOwner.IsValid())
        {
            GraphOwner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        }
        return;
    }
    Elapsed += DeltaSeconds;
    if (!bActionStarted && Elapsed >= Settings->WarningSeconds)
    {
        BeginAction();
    }
    if (!bStopped && bActionStarted && Settings->Kind == EAbilityEnemySpecialKind::Roar &&
        Elapsed - ActionStartedAt >= Settings->Shape.RecoveryEnd - Settings->Shape.HitTime())
    {
        GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
        return;
    }
    const auto Running = Action;
    if (!bStopped && Running)
    {
        const auto State = Running->Advance(Elapsed);
        if (!bStopped && GraphOwner.IsValid())
        {
            if (State == EAbilityEnemyActionState::Completed)
            {
                GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
            }
            else if (State == EAbilityEnemyActionState::Failed)
            {
                GraphOwner->Cancel(EAbilityGraphEndReason::Failed);
            }
        }
    }
}

void UAbilityEnemySpecialNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    Super::Stop(Reason);
    if (Action)
    {
        Action->Stop();
        Action.Reset();
    }
    if (Reason != EAbilityGraphEndReason::Completed && Reason != EAbilityGraphEndReason::Expired)
    {
        AbilityEnemySpecialActions::RemoveRoar(Input.SourceToken, ArmorHandle);
    }
    if (Presenter.IsValid() && Presenter->IsAttackCurrent(Serial, Lease))
    {
        Presenter->FinishAttack(Serial);
    }
    // 眩晕会先递增预警序号，移动归还不能依赖旧序号；组件验证执行租约和来源生命后恢复。
    if (MovementOwnership)
    {
        MovementOwnership->Release(this, MovementLease);
        MovementLease.Invalidate();
    }
}

void AbilityEnemySpecialNodes::Register(FAbilityNodeRegistry& Registry)
{
    for (const auto* Name : {TEXT("Move.GruxCharge"), TEXT("Field.ShroomSpore"), TEXT("Status.GruxRoarArmor")})
    {
        if (Registry.Find(Name))
        {
            continue;
        }
        FAbilityNodeDescriptor Descriptor;
        Descriptor.TypeId = Name;
        Descriptor.ConfigClass = UAbilityEnemySpecialNodeConfig::StaticClass();
        Descriptor.ExecutionClass = UAbilityEnemySpecialNodeExecution::StaticClass();
        Descriptor.InputPayload = EAbilityPortPayload::Origin;
        Descriptor.bSupportsActorTargets = Descriptor.bSupportsSnapshotTargets = true;
        Descriptor.bIsAsync = true;
        Descriptor.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                            {TEXT("Applied"), EAbilityPortPayload::Origin, false},
                            {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                            {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                            {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
        FString Error;
        ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Enemy special registration failed: %s"), *Error);
    }
}
