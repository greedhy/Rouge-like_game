// 调用链：传统GA/图节点 -> 共享动作 -> 原投射物Actor或Pawn；每次产生效果前核对来源生命。
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actor/MyProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interface/CombatInterface.h"

bool FAbilityActorProjectileSettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!ProjectileClass || Count < 0 || !FMath::IsFinite(Spread) || !FMath::IsFinite(PitchOverride) ||
        !FMath::IsFinite(HomingAccelerationMin) || !FMath::IsFinite(HomingAccelerationMax) ||
        HomingAccelerationMin < 0.f || HomingAccelerationMax < HomingAccelerationMin)
    {
        Error = TEXT("Actor projectiles require a class, nonnegative count and finite launch settings.");
    }
    return Error.IsEmpty();
}

bool UAbilityActorProjectileNodeConfig::Validate(FString& Error) const
{
    return Settings.Validate(Error);
}

bool UAbilitySummonNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (Count <= 0 || MinionClasses.IsEmpty() || MinionClasses.Contains(nullptr) || !FMath::IsFinite(MinDistance) ||
        !FMath::IsFinite(MaxDistance) || MinDistance < 0.f || MaxDistance < MinDistance || !FMath::IsFinite(Spread) ||
        !FMath::IsFinite(SpawnHeight) || !FMath::IsFinite(Interval) || Interval < 0.f ||
        PreparedLocations.ContainsByPredicate(
            [](const FVector& Point)
            {
                return Point.ContainsNaN();
            }))
    {
        Error = TEXT("Summon requires pawn classes, positive count and finite locations/cadence.");
    }
    return Error.IsEmpty();
}

AMyProjectile* AbilityTraditionalActions::CreateProjectileDeferred(const FAbilitySourceToken& Source,
                                                                   const FDamageEffectParams& Damage,
                                                                   TSubclassOf<AMyProjectile> Class,
                                                                   const FTransform& Transform)
{
    AActor* Avatar = nullptr;
    if (!Class || Transform.ContainsNaN() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return nullptr;
    }
    AMyProjectile* Projectile = Source.World->SpawnActorDeferred<AMyProjectile>(
        Class, Transform, Avatar, Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Projectile)
    {
        Projectile->DamageEffectParams = Damage;
        Projectile->DamageEffectParams.SourceAbilitySystemComponent = Source.ASC.Get();
        Projectile->SetExecutionSourceToken(Source);
    }
    return Projectile;
}

TArray<AMyProjectile*> AbilityTraditionalActions::SpawnProjectiles(const FAbilitySourceToken& Source,
                                                                   const FDamageEffectParams& Damage,
                                                                   const FAbilityActorProjectileSettings& Settings,
                                                                   float Level, const FVector& Origin,
                                                                   const FVector& AimPoint, AActor* HomingTarget)
{
    TArray<AMyProjectile*> Spawned;
    FString Error;
    AActor* Avatar = nullptr;
    if (!Settings.Validate(Error) || !FMath::IsFinite(Level) || Origin.ContainsNaN() || AimPoint.ContainsNaN() ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return Spawned;
    }
    FRotator Rotation = (AimPoint - Origin).Rotation();
    if (Settings.bOverridePitch)
    {
        Rotation.Pitch = Settings.PitchOverride;
    }
    const int32 Count =
        Settings.bLimitCountToLevel ? FMath::Min(Settings.Count, FMath::FloorToInt(Level)) : Settings.Count;
    if (Count <= 0)
    {
        return Spawned;
    }
    const auto Rotations =
        UMyAbilitySystemLibrary::EvenlySpacedRotators(Rotation.Vector(), FVector::UpVector, Settings.Spread, Count);
    for (const FRotator& Direction : Rotations)
    {
        const FTransform Transform(Direction, Origin);
        AMyProjectile* Projectile = CreateProjectileDeferred(Source, Damage, Settings.ProjectileClass, Transform);
        if (!Projectile)
        {
            continue;
        }
        if (Settings.bConfigureHoming)
        {
            if (IsValid(HomingTarget) && HomingTarget->Implements<UCombatInterface>())
            {
                Projectile->ProjectileMovement->HomingTargetComponent = HomingTarget->GetRootComponent();
            }
            else
            {
                // 此独立位置组件随投射物持有，不依赖GA在命中前保持激活。
                Projectile->HomingTargetSceneComponent = NewObject<USceneComponent>(Projectile);
                Projectile->HomingTargetSceneComponent->SetWorldLocation(AimPoint);
                Projectile->ProjectileMovement->HomingTargetComponent = Projectile->HomingTargetSceneComponent;
            }
            Projectile->ProjectileMovement->HomingAccelerationMagnitude =
                FMath::FRandRange(Settings.HomingAccelerationMin, Settings.HomingAccelerationMax);
            Projectile->ProjectileMovement->bIsHomingProjectile = Settings.bLaunchHoming;
        }
        Projectile->FinishSpawning(Transform);
        if (IsValid(Projectile))
        {
            Spawned.Add(Projectile);
        }
    }
    return Spawned;
}

TArray<FVector> AbilityTraditionalActions::GatherSummonLocations(const FAbilitySourceToken& Source,
                                                                 const FVector& Origin, const FVector& Forward,
                                                                 int32 Count, float MinDistance, float MaxDistance,
                                                                 float Spread)
{
    TArray<FVector> Locations;
    AActor* Avatar = nullptr;
    if (Count <= 0 || Origin.ContainsNaN() || Forward.ContainsNaN() || !FMath::IsFinite(MinDistance) ||
        !FMath::IsFinite(MaxDistance) || MinDistance < 0.f || MaxDistance < MinDistance || !FMath::IsFinite(Spread) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return Locations;
    }
    const float Delta = Count > 1 ? Spread / (Count - 1.f) : 0.f;
    const FVector Left = Forward.RotateAngleAxis(Count > 1 ? -Spread / 2.f : 0.f, FVector::UpVector);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FVector Direction = Left.RotateAngleAxis(Delta * Index, FVector::UpVector);
        FVector Location = Origin + Direction * FMath::FRandRange(MinDistance, MaxDistance);
        FHitResult Hit;
        Source.World->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 400), Location - FVector(0, 0, 400),
                                               ECC_Visibility);
        if (Hit.bBlockingHit)
        {
            Location = Hit.ImpactPoint;
        }
        Locations.Add(Location);
    }
    return Locations;
}

TSubclassOf<APawn> AbilityTraditionalActions::ChooseMinionClass(const TArray<TSubclassOf<APawn>>& Classes)
{
    return Classes.IsEmpty() ? nullptr : Classes[FMath::RandRange(0, Classes.Num() - 1)];
}

bool UAbilitySummonedPawnLease::Initialize(const FAbilitySourceToken& InSource)
{
    AActor* Avatar = nullptr;
    if (bCounted || !GetOwner() || !AbilityExecutionIdentity::ResolveSource(InSource, Avatar) ||
        !Avatar->Implements<UCombatInterface>())
    {
        return false;
    }
    Source = InSource;
    GetOwner()->OnDestroyed.AddDynamic(this, &ThisClass::OnPawnDestroyed);
    bCounted = true;
    ICombatInterface::Execute_IncreaseMinionCount(Avatar, 1);
    return true;
}

void UAbilitySummonedPawnLease::OnPawnDestroyed(AActor* Pawn)
{
    if (!bCounted)
    {
        return;
    }
    bCounted = false;
    AActor* Avatar = nullptr;
    if (AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        ICombatInterface::Execute_IncreaseMinionCount(Avatar, -1);
    }
    if (Pawn)
    {
        Pawn->OnDestroyed.RemoveDynamic(this, &ThisClass::OnPawnDestroyed);
    }
}

APawn* AbilityTraditionalActions::SpawnSummonedPawn(const FAbilitySourceToken& Source, TSubclassOf<APawn> Class,
                                                    const FTransform& Transform)
{
    AActor* Avatar = nullptr;
    if (!Class || Transform.ContainsNaN() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return nullptr;
    }
    // 与旧K2 SpawnActor一致：Pawn的Owner/Instigator留空，AI控制器仍由原Pawn类决定。
    APawn* Pawn = Source.World->SpawnActorDeferred<APawn>(Class, Transform, nullptr, nullptr,
                                                          ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Pawn)
    {
        return nullptr;
    }
    Pawn->FinishSpawning(Transform);
    if (!IsValid(Pawn) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        if (IsValid(Pawn))
        {
            Pawn->Destroy();
        }
        return nullptr;
    }
    Pawn->SpawnDefaultController();
    if (!IsValid(Pawn) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        if (IsValid(Pawn))
        {
            Pawn->Destroy();
        }
        return nullptr;
    }
    auto* Lease = NewObject<UAbilitySummonedPawnLease>(Pawn);
    Pawn->AddInstanceComponent(Lease);
    Lease->RegisterComponent();
    Lease->Initialize(Source);
    return Pawn;
}

bool UAbilityActorProjectileNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityActorProjectileNodeConfig>(&Config);
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return false;
    }
    FVector Origin = Input.Origin;
    if (Settings.SocketTag.IsValid())
    {
        if (!Avatar->Implements<UCombatInterface>())
        {
            Error = TEXT("Projectile socket requires CombatInterface.");
            return false;
        }
        Origin = ICombatInterface::Execute_GetCombatSocketLocation(Avatar, Settings.SocketTag);
    }
    FVector AimPoint = Input.EventCenter;
    AActor* HomingTarget = nullptr;
    if (Input.LiveTargetToken.IsSet())
    {
        if (!AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), AimPoint))
        {
            Error = TEXT("Projectile target life is no longer current.");
            return false;
        }
        HomingTarget = Input.LiveTargetToken->Target.Actor.Get();
    }
    else if (AimPoint.Equals(Origin))
    {
        AimPoint = Origin + Input.Direction * 1000.f;
    }
    AbilityTraditionalActions::SpawnProjectiles(Input.SourceToken, Input.DamageParams, Settings.Settings, Input.Level,
                                                Origin, AimPoint, HomingTarget);
    return Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilitySummonNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                        const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    Settings = DuplicateObject(CastChecked<UAbilitySummonNodeConfig>(&Config), this);
    Locations = Settings->PreparedLocations.IsEmpty()
                    ? AbilityTraditionalActions::GatherSummonLocations(Input.SourceToken, Input.Origin, Input.Direction,
                                                                       Settings->Count, Settings->MinDistance,
                                                                       Settings->MaxDistance, Settings->Spread)
                    : Settings->PreparedLocations;
    NextIndex = 0;
    Elapsed = 0.f;
    if (Locations.IsEmpty())
    {
        return Owner.Publish(Id, TEXT("Completed"), Input);
    }
    SpawnNext();
    return true;
}

void UAbilitySummonNodeExecution::SpawnNext()
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    if (bStopped || !Owner || Owner->IsFinished() || !Settings || !Locations.IsValidIndex(NextIndex) ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return;
    }
    const FVector Location = Locations[NextIndex++] + FVector(0, 0, Settings->SpawnHeight);
    const FTransform Transform((Location - Avatar->GetActorLocation()).Rotation(), Location);
    AbilityTraditionalActions::SpawnSummonedPawn(
        Input.SourceToken, AbilityTraditionalActions::ChooseMinionClass(Settings->MinionClasses), Transform);
    // FinishSpawning和计数接口允许蓝图重入；取消后不得发布新的完成事件。
    if (!bStopped && !Owner->IsFinished() && NextIndex >= Locations.Num())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilitySummonNodeExecution::Advance(float DeltaSeconds)
{
    if (bStopped || !Settings)
    {
        return;
    }
    Elapsed += DeltaSeconds;
    // 旧Delay每次恢复最多生成一只；大帧不把整个波次挤在同一帧。
    if (Elapsed >= Settings->Interval)
    {
        Elapsed = 0.f;
        SpawnNext();
    }
}

void UAbilitySummonNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    Locations.Reset();
    Settings = nullptr;
}

void AbilityTraditionalNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Projectile;
    Projectile.TypeId = TEXT("Delivery.ActorProjectiles");
    Projectile.ConfigClass = UAbilityActorProjectileNodeConfig::StaticClass();
    Projectile.ExecutionClass = UAbilityActorProjectileNodeExecution::StaticClass();
    Projectile.InputPayload = EAbilityPortPayload::Origin;
    Projectile.bSupportsActorTargets = Projectile.bSupportsSnapshotTargets = true;
    Projectile.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Summon;
    Summon.TypeId = TEXT("Spawn.Summon");
    Summon.ConfigClass = UAbilitySummonNodeConfig::StaticClass();
    Summon.ExecutionClass = UAbilitySummonNodeExecution::StaticClass();
    Summon.InputPayload = EAbilityPortPayload::Origin;
    Summon.bSupportsActorTargets = Summon.bSupportsSnapshotTargets = true;
    Summon.bIsAsync = true;
    Summon.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Projectile, Summon})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Traditional action registration: %s"), *Error);
        }
    }
}
