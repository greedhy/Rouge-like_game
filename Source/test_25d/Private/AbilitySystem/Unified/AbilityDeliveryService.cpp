// 调用链：投递/传播节点 -> 本次实例 -> 真实弹体移动段 -> 多次命中流 -> 单次终态回执。
// 来源生命在每次推进/接触前复核；结束先关闭入口，再移除移动委托并释放仍属于本次投递的Actor。
#include "AbilitySystem/Unified/AbilityDeliveryService.h"
#include "Actor/MyProjectile.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "PureMass/PureMassCombatTarget.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"

bool FAbilityDeliverySettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!ProjectileClass || !FMath::IsFinite(Speed) || Speed <= 0.f || Speed > 10000.f ||
        !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.f || MaxDistance > 10000.f ||
        !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds <= 0.f || TimeoutSeconds > 60.f ||
        !FMath::IsFinite(CollisionRadius) || CollisionRadius <= 0.f || CollisionRadius > 200.f)
    {
        Error = TEXT(
            "Delivery requires a projectile class, speed/distance in (0,10000], timeout in (0,60] and radius in (0,200].");
    }
    else if (!ProjectileClass->GetDefaultObject<AMyProjectile>()->SupportsGraphDelivery())
    {
        Error = TEXT("Projectile native lifecycle has not opted into graph delivery.");
    }
    else if (bOverrideNativeCollisionRadius &&
             FlightProfile != EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults)
    {
        Error = TEXT("A native construction radius override requires the native class profile.");
    }
    else if (static_cast<uint8>(FlightProfile) >
                 static_cast<uint8>(EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults) ||
             static_cast<uint8>(ContactProfile) >
                 static_cast<uint8>(EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass) ||
             static_cast<uint8>(TargetLossPolicy) >
                 static_cast<uint8>(EAbilityDeliveryTargetLossPolicy::KeepLastAimPoint) ||
             (FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults &&
              (Motion == EAbilityProjectileMotion::Returning ||
               Motion == EAbilityProjectileMotion::LegacyTurnHoming)) ||
             static_cast<uint8>(Motion) > static_cast<uint8>(EAbilityProjectileMotion::Returning) ||
             static_cast<uint8>(ImpactPolicy) > static_cast<uint8>(EAbilityProjectileImpactPolicy::ReturnPerPhase) ||
             ImpactLimit < 1 || ImpactLimit > 64 || !FMath::IsFinite(DamageFalloff) || DamageFalloff < 0.f ||
             DamageFalloff > 1.f || !FMath::IsFinite(HomingTurnRate) || HomingTurnRate <= 0.f ||
             HomingTurnRate > 720.f || !FMath::IsFinite(HomingAccelerationMin) ||
             !FMath::IsFinite(HomingAccelerationMax) || HomingAccelerationMin < 0.f ||
             HomingAccelerationMax < HomingAccelerationMin ||
             (ImpactPolicy == EAbilityProjectileImpactPolicy::ReturnPerPhase &&
              Motion != EAbilityProjectileMotion::Returning))
    {
        Error = TEXT(
            "Delivery motion/contact policy requires bounded count, falloff, turn rate and matching return phase policy.");
    }
    return Error.IsEmpty();
}

bool UAbilityDeliveryInstance::Begin(const FAbilityDeliveryRequest& Request, FString& Error)
{
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    AActor* Source = nullptr;
    FVector TargetLocation = Request.AimPoint.IsSet() ? Request.AimPoint.GetValue() : FVector::ZeroVector;
    const bool bNativeFlight =
        Request.Settings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
    const auto Motion = Request.Settings.Motion == EAbilityProjectileMotion::Linear && Request.Settings.bHoming
                            ? EAbilityProjectileMotion::NativeHoming
                            : Request.Settings.Motion;
    const bool bNativeSetup = bNativeFlight && Request.Settings.NativeConfigureHoming;
    const bool bNeedsAim = Motion == EAbilityProjectileMotion::NativeHoming ||
                           Motion == EAbilityProjectileMotion::LegacyTurnHoming || bNativeSetup;
    Error.Reset();
    if (bStarted || !Request.Settings.Validate(Error) || Request.Origin.ContainsNaN() ||
        Request.Direction.ContainsNaN() || Request.Direction.IsNearlyZero() || !Request.RootCastId.IsValid() ||
        !FMath::IsFinite(Request.DamageScale) || Request.DamageScale < 0.f ||
        !FMath::IsFinite(Request.DebuffDamageScale) || Request.DebuffDamageScale < 0.f ||
        !FMath::IsFinite(Request.DamageParams.BaseDamage * Request.DamageScale) ||
        !FMath::IsFinite(Request.DamageParams.DebuffDamage * Request.DebuffDamageScale) || !Request.StepId.IsValid() ||
        !AbilityExecutionIdentity::ResolveSource(Request.Source, Source) ||
        (Request.AimPoint.IsSet() && Request.AimPoint->ContainsNaN()) ||
        (Request.Target.IsSet() &&
         !AbilityExecutionIdentity::ResolveTarget(Request.Source, Request.Target.GetValue(), TargetLocation)) ||
        (bNeedsAim && !Request.Target.IsSet() && !Request.AimPoint.IsSet()))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT(
                "Delivery requires an original live source, finite geometry/scales and explicit homing target or point.");
        }
        return false;
    }
    ActiveRequest = Request;
    RetainedSettings = Request.Settings;
    RetainedDamage = Request.DamageParams;
    LastAimPoint = TargetLocation;
    MotionState.Position = Request.Origin;
    MotionState.Direction = Request.Direction.GetSafeNormal();
    PreviousLocation = Request.Origin;
    Result.DeliveryId = FGuid::NewGuid();
    Result.RootCastId = Request.RootCastId;
    Result.StepId = Request.StepId;
    Result.Location = Request.Origin;
    Result.Direction = Request.Direction.GetSafeNormal();
    Result.DamageScale = Request.DamageScale;
    Result.DebuffDamageScale = Request.DebuffDamageScale;
    const FTransform Transform(Result.Direction.Rotation(), Request.Origin);
    auto* Shot = AbilityTraditionalActions::CreateProjectileDeferred(Request.Source, RetainedDamage,
                                                                     RetainedSettings.ProjectileClass, Transform);
    if (!Shot)
    {
        Error = TEXT("Delivery could not create its original projectile actor.");
        return false;
    }
    bStarted = true;
    Projectile = Shot;
    const TWeakObjectPtr<UAbilityDeliveryInstance> Weak(this);
    Shot->SetGraphDeliveryHandlers(
        [Weak](const FAbilityTargetToken& Target, FVector Position, FVector Direction)
        {
            if (auto* Instance = Weak.Get())
            {
                return Instance->Contact(Target, Position, Direction);
            }
            return EAbilityProjectileContactDisposition::Ignore;
        },
        [Weak](bool bReflected)
        {
            if (auto* Instance = Weak.Get())
            {
                Instance->Retired(bReflected);
            }
        },
        Request.IgnoredTargets);
    if (RetainedSettings.ContactProfile == EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass)
    {
        Shot->SetGraphDeliveryBlockingContactHandler(
            [Weak](FVector Location)
            {
                if (auto* Instance = Weak.Get())
                {
                    Instance->NativeBlocked(Location);
                }
            });
    }
    auto ConfigureHoming = [&]()
    {
        Shot->HomingTargetSceneComponent = NewObject<USceneComponent>(Shot);
        Shot->HomingTargetSceneComponent->SetWorldLocation(TargetLocation);
        Shot->ProjectileMovement->HomingTargetComponent = Shot->HomingTargetSceneComponent;
        // 原native配置与启用独立；即使禁追踪仍消耗原每弹一次随机采样。
        Shot->ProjectileMovement->HomingAccelerationMagnitude =
            FMath::FRandRange(RetainedSettings.HomingAccelerationMin, RetainedSettings.HomingAccelerationMax);
        Shot->ProjectileMovement->bIsHomingProjectile = Motion == EAbilityProjectileMotion::NativeHoming;
    };
    if (bNativeSetup)
    {
        ConfigureHoming(); // 与原SpawnProjectiles相同：在Construction/BeginPlay之前写入。
    }
    for (const auto& Ignored : Request.IgnoredTargets)
    {
        FVector Location;
        if (Ignored.Target.IsActor() && AbilityExecutionIdentity::ResolveTarget(Request.Source, Ignored, Location))
        {
            Shot->Sphere->IgnoreActorWhenMoving(Ignored.Target.Actor.Get(), true);
        }
    }
    Shot->FinishSpawning(Transform);
    if (bFinished || !IsValid(Shot) || Shot->IsActorBeingDestroyed())
    {
        return true;
    }
    if (!Shot->IsActorInitialized())
    {
        Shot->InitializeComponents();
        Shot->PostInitializeComponents();
    }
    if (!Shot->HasActorBegunPlay())
    {
        Shot->DispatchBeginPlay();
    }
    if (bNativeFlight)
    {
        // 读取实际完成BP构造/BeginPlay后的参数，保留其muzzle禁碰撞、速度、球半径与寿命。
        RetainedSettings.Speed = Shot->ProjectileMovement->InitialSpeed;
        if (RetainedSettings.bOverrideNativeCollisionRadius)
        {
            const float ShapeScale = Shot->Sphere->GetShapeScale();
            if (!FMath::IsFinite(ShapeScale) || ShapeScale <= UE_SMALL_NUMBER)
            {
                Error = TEXT("Native projectile sphere requires a finite positive shape scale.");
                Finish(EAbilityDeliveryOutcome::Cancelled, true);
                return false;
            }
            // 初始化期间不额外触发同步接触；原真实移动/启用碰撞在完成配置后产生回执。
            if (!Shot->SetGraphDeliveryCollisionRadius(RetainedSettings.CollisionRadius))
            {
                Error = TEXT("Native projectile could not initialize its selected replicated radius.");
                Finish(EAbilityDeliveryOutcome::Cancelled, true);
                return false;
            }
        }
        RetainedSettings.CollisionRadius = Shot->Sphere->GetScaledSphereRadius();
        const float Life = Shot->GetLifeSpan();
        RetainedSettings.TimeoutSeconds = Life > 0.f ? Life : Shot->GetConfiguredGraphFlightLifeSpan();
        if (!FMath::IsFinite(RetainedSettings.Speed) || RetainedSettings.Speed <= 0.f ||
            !FMath::IsFinite(RetainedSettings.CollisionRadius) || RetainedSettings.CollisionRadius <= 0.f ||
            !FMath::IsFinite(RetainedSettings.TimeoutSeconds) || RetainedSettings.TimeoutSeconds <= 0.f ||
            RetainedSettings.TimeoutSeconds > 60.f)
        {
            Error = TEXT(
                "Native projectile class requires finite positive speed/radius and a bounded configured lifespan.");
            Finish(EAbilityDeliveryOutcome::Cancelled, true);
            return false;
        }
    }
    else
    {
        // Explicit旧路径继续由配置决定全部飞行参数。
        Shot->Sphere->SetSphereRadius(RetainedSettings.CollisionRadius);
        Shot->ProjectileMovement->InitialSpeed = RetainedSettings.Speed;
        Shot->ProjectileMovement->MaxSpeed = RetainedSettings.Speed;
        Shot->ProjectileMovement->Velocity = Result.Direction * RetainedSettings.Speed;
        Shot->ProjectileMovement->bIsHomingProjectile = Motion == EAbilityProjectileMotion::NativeHoming;
        if (Motion == EAbilityProjectileMotion::NativeHoming)
        {
            ConfigureHoming();
        }
    }
    Shot->Sphere->IgnoreActorWhenMoving(Source, true);
    Shot->Sphere->TransformUpdated.AddUObject(this, &ThisClass::Moved);
    Shot->ProjectileMovement->bSimulationEnabled =
        Motion != EAbilityProjectileMotion::LegacyTurnHoming && Motion != EAbilityProjectileMotion::Returning;
    if (bNativeFlight && bNativeSetup && Request.Target.IsSet() && Request.Target->Target.IsActor())
    {
        if (auto* TargetActor = Request.Target->Target.Actor.Get())
        {
            ObservedHomingRoot = TargetActor->GetRootComponent();
        }
        if (auto* Root = ObservedHomingRoot.Get())
        {
            Root->TransformUpdated.AddUObject(this, &ThisClass::HomingTargetMoved);
            HomingTargetMoved(Root, EUpdateTransformFlags::None, ETeleportType::None);
        }
    }
    return true;
}

EAbilityProjectileMotion UAbilityDeliveryInstance::EffectiveMotion() const
{
    return RetainedSettings.Motion == EAbilityProjectileMotion::Linear && RetainedSettings.bHoming
               ? EAbilityProjectileMotion::NativeHoming
               : RetainedSettings.Motion;
}

bool UAbilityDeliveryInstance::RetireHomingTarget()
{
    if (auto* Root = ObservedHomingRoot.Get())
    {
        Root->TransformUpdated.RemoveAll(this);
    }
    ObservedHomingRoot.Reset();
    bHomingTargetRetired = true;
    if (RetainedSettings.TargetLossPolicy == EAbilityDeliveryTargetLossPolicy::KeepLastAimPoint)
    {
        return true;
    }
    Finish(EAbilityDeliveryOutcome::TargetLost, true);
    return false;
}

void UAbilityDeliveryInstance::HomingTargetMoved(USceneComponent*, EUpdateTransformFlags, ETeleportType)
{
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    if (bFinished || bHomingTargetRetired || !ActiveRequest.Target.IsSet())
    {
        return;
    }
    FVector Position;
    if (!AbilityExecutionIdentity::ResolveTarget(ActiveRequest.Source, ActiveRequest.Target.GetValue(), Position))
    {
        RetireHomingTarget();
        return;
    }
    LastAimPoint = Position;
    if (auto* Shot = Projectile.Get(); Shot && Shot->HomingTargetSceneComponent)
    {
        Shot->HomingTargetSceneComponent->SetWorldLocation(Position);
    }
}

void UAbilityDeliveryInstance::NativeBlocked(FVector Location)
{
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    if (bFinished)
    {
        return;
    }
    AActor* Source = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(ActiveRequest.Source, Source))
    {
        Finish(EAbilityDeliveryOutcome::Cancelled, true);
        return;
    }
    Result.Location = Location;
    if (auto* Shot = Projectile.Get())
    {
        Shot->PlayGraphDeliveryImpactAt(Location, true);
    }
    Finish(EAbilityDeliveryOutcome::Blocked, true);
}

void UAbilityDeliveryInstance::Advance(float DeltaSeconds)
{
    if (!bStarted || bFinished || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f)
    {
        return;
    }
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    AActor* Source = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(ActiveRequest.Source, Source))
    {
        Finish(EAbilityDeliveryOutcome::Cancelled, true);
        return;
    }
    const auto Motion = EffectiveMotion();
    const bool bNativeFlight =
        RetainedSettings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
    const bool bRequiresTarget =
        Motion == EAbilityProjectileMotion::NativeHoming || Motion == EAbilityProjectileMotion::LegacyTurnHoming ||
        (!bNativeFlight && RetainedSettings.ImpactPolicy == EAbilityProjectileImpactPolicy::StopFirst &&
         !Result.Impact.IsSet());
    FVector TargetLocation = LastAimPoint;
    if (bRequiresTarget && !bHomingTargetRetired && ActiveRequest.Target.IsSet() &&
        !AbilityExecutionIdentity::ResolveTarget(ActiveRequest.Source, ActiveRequest.Target.GetValue(), TargetLocation))
    {
        if (!RetireHomingTarget())
        {
            return;
        }
        TargetLocation = LastAimPoint;
    }
    LastAimPoint = TargetLocation;
    auto* Shot = Projectile.Get();
    Elapsed += DeltaSeconds;
    if (!IsValid(Shot) || Elapsed + UE_SMALL_NUMBER >= RetainedSettings.TimeoutSeconds)
    {
        Finish(EAbilityDeliveryOutcome::Expired, true);
        return;
    }
    if (Motion == EAbilityProjectileMotion::NativeHoming && Shot->HomingTargetSceneComponent)
    {
        Shot->HomingTargetSceneComponent->SetWorldLocation(TargetLocation);
    }
    if (Motion != EAbilityProjectileMotion::Returning && Motion != EAbilityProjectileMotion::LegacyTurnHoming)
    {
        if (!bNativeFlight && Travelled + UE_SMALL_NUMBER >= RetainedSettings.MaxDistance)
        {
            Finish(EAbilityDeliveryOutcome::Expired, true);
        }
        return;
    }
    FAbilityProjectileMotionConfig Config;
    Config.Motion = Motion;
    Config.Speed = RetainedSettings.Speed;
    Config.Range = RetainedSettings.MaxDistance;
    Config.CollisionRadius = RetainedSettings.CollisionRadius;
    Config.HomingTurnRate = RetainedSettings.HomingTurnRate;
    float RemainingTime = DeltaSeconds;
    // 最多两个阶段：到达去程端点后消费该帧余时返航，不制造下一帧前的额外命中。
    for (int32 SegmentIndex = 0; SegmentIndex < 2 && !bFinished && RemainingTime > UE_SMALL_NUMBER; ++SegmentIndex)
    {
        MotionState.Position = Shot->GetActorLocation();
        MotionState.OutboundDistance = Travelled;
        FAbilityFlightSegment Segment;
        const TOptional<FVector> Aim =
            bRequiresTarget && ActiveRequest.Target.IsSet() ? TOptional<FVector>(TargetLocation) : TOptional<FVector>();
        if (!AbilityProjectileMotion::ComputeFlightSegment(Config, MotionState, RemainingTime,
                                                           Source->GetActorLocation(), Aim, Segment))
        {
            Finish(EAbilityDeliveryOutcome::TargetLost, true);
            return;
        }
        MotionState.Direction = Segment.Direction;
        Shot->ProjectileMovement->Velocity = Segment.Direction * RetainedSettings.Speed;
        Shot->SetActorRotation(Segment.Direction.Rotation());
        FHitResult Hit;
        Shot->ProjectileMovement->MoveUpdatedComponent(Segment.End - Segment.Start, Shot->GetActorQuat(), true, &Hit);
        if (bFinished)
        {
            return;
        }
        if (Motion == EAbilityProjectileMotion::Returning && Segment.bReachesPhaseEnd)
        {
            if (MotionState.Phase == EAbilityProjectilePhase::Returning)
            {
                Finish(EAbilityDeliveryOutcome::Returned, true);
                return;
            }
            MotionState.Phase = EAbilityProjectilePhase::Returning;
            PhaseImpacts.Reset();
            RefreshIgnoredTargets();
            RemainingTime = Segment.RemainingSeconds;
        }
        else
        {
            if (Segment.bReachesPhaseEnd)
            {
                Finish(EAbilityDeliveryOutcome::Expired, true);
            }
            return;
        }
    }
}

void UAbilityDeliveryInstance::Cancel()
{
    if (bStarted && !bFinished)
    {
        Finish(EAbilityDeliveryOutcome::Cancelled, true);
    }
}
AMyProjectile* UAbilityDeliveryInstance::GetProjectile() const
{
    return Projectile.Get();
}
void UAbilityDeliveryInstance::Moved(USceneComponent* Component, EUpdateTransformFlags, ETeleportType Teleport)
{
    const FVector Start = PreviousLocation;
    const FVector ActualEnd = Component->GetComponentLocation();
    PreviousLocation = ActualEnd;
    auto* Shot = Projectile.Get();
    if (!bStarted || bFinished || bContactClaimed || bProcessingMovement || !IsValid(Shot) ||
        Shot->IsActorBeingDestroyed() || Teleport != ETeleportType::None || !Shot->Sphere->IsQueryCollisionEnabled())
    {
        return;
    }
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    TGuardValue<bool> Moving(bProcessingMovement, true);
    AActor* Source = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(ActiveRequest.Source, Source))
    {
        Finish(EAbilityDeliveryOutcome::Cancelled, true);
        return;
    }
    const bool bReturning = EffectiveMotion() == EAbilityProjectileMotion::Returning;
    const bool bReturnPhase = bReturning && MotionState.Phase == EAbilityProjectilePhase::Returning;
    const bool bNativeFlight =
        RetainedSettings.FlightProfile == EAbilityDeliveryFlightProfile::NativeFireBoltClassDefaults;
    const float Remaining =
        bReturnPhase || bNativeFlight ? MAX_flt : FMath::Max(0.f, RetainedSettings.MaxDistance - Travelled);
    const FVector Delta = ActualEnd - Start;
    const float Distance = Delta.Size();
    const FVector End = Start + Delta.GetSafeNormal() * FMath::Min(Distance, Remaining);
    if (!bReturnPhase)
    {
        Travelled += FMath::Min(Distance, Remaining);
    }
    Result.Location = End;
    Result.Direction = Delta.IsNearlyZero() ? Shot->GetActorForwardVector() : Delta.GetSafeNormal();
    if (RetainedSettings.ContactProfile == EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass)
    {
        // 与原Mass桥相同，只桥接最早Mass；Actor/墙保留真实Sphere通知，Actor胶囊仍能挡住后方Mass。
        float Time = 1.f;
        FHitResult Wall;
        const auto Target = PureMassCombat::Sweep(ActiveRequest.Source.World.Get(), Source, Start, End,
                                                  RetainedSettings.CollisionRadius, Time, Wall, nullptr, Shot);
        if (Target.IsSet() && !Target.IsActor())
        {
            FAbilityTargetToken Token;
            FString Error;
            if (AbilityExecutionIdentity::CaptureTarget(ActiveRequest.Source, Target, Token, Error))
            {
                Shot->DispatchGraphDeliveryContact(Token, FMath::Lerp(Start, End, Time), Result.Direction);
            }
        }
        return;
    }
    TSet<FPureMassCombatTarget> Ignored;
    for (const auto& Token : ActiveRequest.IgnoredTargets)
    {
        FVector Location;
        if (AbilityExecutionIdentity::ResolveTarget(ActiveRequest.Source, Token, Location))
        {
            Ignored.Add(Token.Target);
        }
    }
    for (const auto& Token : PhaseImpacts)
    {
        FVector Location;
        if (AbilityExecutionIdentity::ResolveTarget(ActiveRequest.Source, Token, Location))
        {
            Ignored.Add(Token.Target);
        }
    }
    FHitResult Wall;
    // 同一个已移动物理段按时间消费接触；去重集合增长后重查，保留墙优先规则。
    for (int32 ContactIndex = 0; ContactIndex < 64 && !bFinished; ++ContactIndex)
    {
        float HitTime = 1.f;
        const auto Target = PureMassCombat::Sweep(ActiveRequest.Source.World.Get(), Source, Start, End,
                                                  RetainedSettings.CollisionRadius, HitTime, Wall, &Ignored, Shot);
        if (!Target.IsSet())
        {
            break;
        }
        Ignored.Add(Target);
        const FVector Position = FMath::Lerp(Start, End, HitTime);
        if (Target.IsActor())
        {
            FHitResult Hit;
            Hit.TraceStart = Start;
            Hit.TraceEnd = End;
            Hit.Location = Hit.ImpactPoint = Position;
            Shot->OnSphereOverlap(Shot->Sphere, Target.Actor.Get(), nullptr, 0, true, Hit);
        }
        else
        {
            FAbilityTargetToken Token;
            FString Error;
            if (AbilityExecutionIdentity::CaptureTarget(ActiveRequest.Source, Target, Token, Error))
            {
                Shot->DispatchGraphDeliveryContact(Token, Position, Result.Direction);
            }
        }
        if (bFinished)
        {
            return;
        }
        if (!AbilityExecutionIdentity::ResolveSource(ActiveRequest.Source, Source))
        {
            Finish(EAbilityDeliveryOutcome::Cancelled, true);
            return;
        }
    }
    if (Wall.bBlockingHit && !bFinished)
    {
        Result.Location = Wall.ImpactPoint;
        Shot->PlayGraphDeliveryImpactAt(Wall.ImpactPoint, true);
        Finish(EAbilityDeliveryOutcome::Blocked, true);
        return;
    }
    if (!bReturning && !bNativeFlight && Distance + UE_SMALL_NUMBER >= Remaining && !bFinished)
    {
        Finish(EAbilityDeliveryOutcome::Expired, true);
    }
    else if (!bFinished)
    {
        // 非终结接触不把弹体移回命中点；终态位置应继续反映该物理段的实际终点。
        Result.Location = End;
    }
}

void UAbilityDeliveryInstance::RefreshIgnoredTargets()
{
    if (auto* Shot = Projectile.Get())
    {
        auto Targets = ActiveRequest.IgnoredTargets;
        Targets.Append(PhaseImpacts);
        Shot->SetGraphDeliveryIgnoredTargets(Targets);
    }
}

EAbilityProjectileContactDisposition UAbilityDeliveryInstance::Contact(const FAbilityTargetToken& Target,
                                                                       FVector Location, FVector Direction)
{
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    FVector Current;
    if (bFinished || bContactClaimed || !AbilityExecutionIdentity::ResolveTarget(ActiveRequest.Source, Target, Current))
    {
        return EAbilityProjectileContactDisposition::Ignore;
    }
    const bool bRepeated = PhaseImpacts.ContainsByPredicate(
        [&](const FAbilityTargetToken& Previous)
        {
            return AbilityBehaviorPrimitives::SameTargetLife(Previous, Target);
        });
    const auto Admission =
        AbilityProjectileMotion::AdmitImpact(RetainedSettings.ImpactPolicy, MotionState.Phase, PhaseImpacts.Num(),
                                             bRepeated, RetainedSettings.ImpactLimit, RetainedSettings.DamageFalloff);
    if (!Admission.bAccepted)
    {
        return EAbilityProjectileContactDisposition::Ignore;
    }
    bContactClaimed = true;
    PhaseImpacts.Add(Target);
    RefreshIgnoredTargets();
    FAbilityDeliveryImpact Event;
    Event.DeliveryId = Result.DeliveryId;
    Event.RootCastId = Result.RootCastId;
    Event.StepId = Result.StepId;
    Event.ImpactSequence = ++ImpactSequence;
    Event.Phase = MotionState.Phase;
    Event.DamageScale = ActiveRequest.DamageScale * Admission.DamageScale;
    Event.DebuffDamageScale = ActiveRequest.DebuffDamageScale;
    Event.bRequireImmediatePayload =
        RetainedSettings.ContactProfile == EAbilityDeliveryContactProfile::NativeFireBoltActorAndMass;
    Event.Impact.Location = Location;
    Event.Impact.Direction = Direction.GetSafeNormal();
    Event.Impact.ConfirmedTarget = Target;
    Event.Impact.Sequence = Event.ImpactSequence;
    if (Event.bRequireImmediatePayload)
    {
        Event.Impact.ContactDamageProfile = Target.Target.IsActor()
                                                ? EAbilityContactDamageProfile::NativeActorProjectile
                                                : EAbilityContactDamageProfile::NativeMassProjectile;
        if (Target.Target.IsActor())
        {
            // 原Actor命中取ActorForward与当前Actor位置，不改成曲线飞行末段方向/几何胶囊近点。
            if (auto* Shot = Projectile.Get())
            {
                Event.Impact.Location = Shot->GetActorLocation();
                Event.Impact.Direction = Shot->GetActorForwardVector();
            }
        }
    }
    Result.Location = Event.Impact.Location;
    Result.Direction = Event.Impact.Direction;
    Result.Impact = Event.Impact;
    Result.DamageScale = Event.DamageScale;
    Result.DebuffDamageScale = Event.DebuffDamageScale;
    if (auto* Shot = Projectile.Get())
    {
        Shot->PlayGraphDeliveryImpactAt(Result.Location, Admission.bTerminateAfterImpact);
    }
    if (RetainedSettings.bApplyDamage)
    {
        FDamageEffectParams Prepared;
        if (AbilityDamageService::PrepareContactDamage(RetainedDamage, Result.Direction, Event.DamageScale,
                                                       Event.DebuffDamageScale, Prepared,
                                                       Event.Impact.ContactDamageProfile))
        {
            // 实际结算只消费这份局部副本一次；Apply不得再次乘累计倍率。
            Event.Damage = AbilityDamageService::Apply(ActiveRequest.Source, Target, Prepared);
        }
    }
    Result.Damage = Event.Damage;
    if (!bFinished)
    {
        OnImpact.Broadcast(Event);
    }
    if (bFinished)
    {
        return EAbilityProjectileContactDisposition::Terminate;
    }
    bContactClaimed = false;
    if (Admission.bTerminateAfterImpact)
    {
        Finish(EAbilityDeliveryOutcome::Hit, true);
        return EAbilityProjectileContactDisposition::Terminate;
    }
    return EAbilityProjectileContactDisposition::Continue;
}

void UAbilityDeliveryInstance::Retired(bool bReflected)
{
    if (bReflected && !bFinished)
    {
        if (auto* Shot = Projectile.Get())
        {
            // 接触伤害owner转回原弹体时只冻结累计数值；不准备force、不提前掷RNG，归属仍由TryReflect提交。
            Shot->DamageEffectParams.BaseDamage = RetainedDamage.BaseDamage * ActiveRequest.DamageScale;
            Shot->DamageEffectParams.DebuffDamage = RetainedDamage.DebuffDamage * ActiveRequest.DebuffDamageScale;
        }
    }
    AActor* Source = nullptr;
    const bool bOriginalSourceRetired = !AbilityExecutionIdentity::ResolveSource(ActiveRequest.Source, Source);
    Finish(bReflected || bOriginalSourceRetired ? EAbilityDeliveryOutcome::Cancelled : EAbilityDeliveryOutcome::Expired,
           false);
}
void UAbilityDeliveryInstance::Finish(EAbilityDeliveryOutcome Outcome, bool bDestroyProjectile)
{
    const TStrongObjectPtr<UAbilityDeliveryInstance> Self(this);
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    Result.Outcome = Outcome;
    if (auto* Root = ObservedHomingRoot.Get())
    {
        Root->TransformUpdated.RemoveAll(this);
    }
    ObservedHomingRoot.Reset();
    auto* Shot = Projectile.Get();
    if (IsValid(Shot))
    {
        if (Outcome != EAbilityDeliveryOutcome::Hit && Outcome != EAbilityDeliveryOutcome::Blocked)
        {
            Result.Location = Shot->GetActorLocation();
        }
        Shot->Sphere->TransformUpdated.RemoveAll(this);
        Shot->ClearGraphDeliveryHandlers();
        if (bDestroyProjectile && !Shot->IsActorBeingDestroyed())
        {
            Shot->Destroy();
        }
    }
    Projectile.Reset();
    OnFinished.Broadcast(Result);
    OnFinished.Clear();
    OnImpact.Clear();
}
