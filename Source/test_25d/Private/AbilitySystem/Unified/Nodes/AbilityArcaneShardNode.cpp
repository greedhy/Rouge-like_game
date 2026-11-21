// 调用链：注册策略 Start -> 立即首片/世界循环定时器 -> Cue、目标生命捕获、伤害 -> 尾延迟 -> Completed。
#include "AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Actor/PointCollection.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Equipment/EquipmentAbilityModifiers.h"
#include "MyGameplayTags.h"

FAbilityDamageResult AbilityArcaneShardNodes::ApplyLegacyActor(const FAbilitySourceToken& Source,
                                                               const FAbilitySourceToken& TargetLife,
                                                               const FDamageEffectParams& Parameters)
{
    FAbilityDamageResult Result;
    AActor* Avatar = nullptr;
    AActor* Target = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        !AbilityExecutionIdentity::ResolveSource(TargetLife, Target) || Source.World != TargetLife.World ||
        Avatar == Target || !ADungeonRunDirector::AllowsCombat(Avatar, Target) ||
        !FMath::IsFinite(Parameters.BaseDamage) || Parameters.BaseDamage < 0.f ||
        !Parameters.DamageGameplayEffectClass ||
        (Parameters.SourceAbilitySystemComponent && Parameters.SourceAbilitySystemComponent != Source.ASC.Get()))
    {
        return Result;
    }
    auto Params = Parameters;
    Params.WorldContextObject = Avatar;
    Params.SourceAbilitySystemComponent = Source.ASC.Get();
    Params.TargetAbilitySystemComponent = TargetLife.ASC.Get();
    Result.ImpactLocation = Target->GetActorLocation();
    Result.bAttempted = true;
    UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
    if (IsValid(Target))
    {
        Result.bKilled = Target->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Target);
    }
    return Result;
}

bool UAbilityArcaneShardNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (MaxPoints < 1 || MaxPoints > 11 || !FMath::IsFinite(Interval) || Interval <= 0.f ||
        !FMath::IsFinite(InitialStartDelay) || Interval + InitialStartDelay < 0.f || !FMath::IsFinite(TailDelay) ||
        TailDelay <= 0.f || !FMath::IsFinite(Radius) || Radius <= 0.f)
    {
        Error = TEXT(
            "Arcane shards require 1..11 points, a positive finite interval/radius/tail and nonnegative first delay.");
    }
    if (EquipmentAbilityTag.IsValid() && EquipmentAbilityTag != FMyGameplayTags::Get().Abilities_Arcane_ArcaneShards)
    {
        Error = TEXT("Shard equipment rules require the exact Shards identity.");
    }
    return Error.IsEmpty();
}

UAbilityArcaneShardContext* UAbilityArcaneShardNodeExecution::PreparePoints(UObject* Outer,
                                                                            const FAbilityNodeInput& InInput,
                                                                            const UAbilityArcaneShardNodeConfig& Config,
                                                                            FString& Error)
{
    AActor* Avatar = nullptr;
    if (!Config.Validate(Error) || !Config.PointCollectionClass ||
        !AbilityExecutionIdentity::ResolveSource(InInput.SourceToken, Avatar))
    {
        Error = TEXT("Arcane shards require the original point collection class and current authority source.");
        return nullptr;
    }
    FActorSpawnParameters Spawn;
    Spawn.Owner = Avatar;
    auto* Points = Avatar->GetWorld()->SpawnActor<APointCollection>(Config.PointCollectionClass, InInput.Origin,
                                                                    FRotator::ZeroRotator, Spawn);
    if (!Points)
    {
        Error = TEXT("Point collection spawn failed.");
        return nullptr;
    }
    auto* Context = NewObject<UAbilityArcaneShardContext>(Outer);
    Context->PointCollection = Points;
    const int32 Count = FMath::Clamp(FMath::FloorToInt(InInput.Level) + 2, 1, Config.MaxPoints);
    const auto GroundPoints = Points->GetGroundPoints(InInput.Origin, Count, FMath::FRandRange(0.f, 360.f));
    for (const auto* Point : GroundPoints)
    {
        if (!IsValid(Point) || Point->GetComponentLocation().ContainsNaN())
        {
            Points->Destroy();
            Error = TEXT("Point collection returned an invalid ground point.");
            return nullptr;
        }
        Context->Points.Add(Point->GetComponentLocation());
    }
    return Context;
}

bool UAbilityArcaneShardNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    Settings =
        DuplicateObject<UAbilityArcaneShardNodeConfig>(CastChecked<UAbilityArcaneShardNodeConfig>(&Config), this);
    ResolvedDamage = Input.DamageParams;
    int32 ExtraChainTargets = 0;
    if (!EquipmentAbilityModifiers::ApplySpellRules(Input.SourceToken, Settings->EquipmentAbilityTag, Settings->Radius,
                                                    ResolvedDamage, ExtraChainTargets))
    {
        Error = TEXT("Shard equipment parameters failed to resolve.");
        return false;
    }
    Prepared = Cast<UAbilityArcaneShardContext>(Input.Context.Get());
    if (!Prepared)
    {
        Prepared = PreparePoints(this, Input, *Settings, Error);
    }
    if (!Prepared || Prepared->Points.IsEmpty() || Prepared->Points.Num() > Settings->MaxPoints ||
        Prepared->Points.ContainsByPredicate(
            [](const FVector& Point)
            {
                return Point.ContainsNaN();
            }))
    {
        Error = TEXT("Arcane shard input requires finite captured points within its configured limit.");
        return false;
    }
    TimerWorld = Input.SourceToken.World;
    if (!TimerWorld.IsValid())
    {
        Error = TEXT("Arcane shard timer world is missing.");
        return false;
    }
    // 与原 K2_SetTimerDelegate 一致：FirstDelay=Interval+InitialStartDelay，随后显式触发首片。
    TimerWorld->GetTimerManager().SetTimer(SpawnTimer, this, &ThisClass::SpawnNext, Settings->Interval, true,
                                           Settings->Interval + Settings->InitialStartDelay);
    SpawnNext();
    return true;
}

void UAbilityArcaneShardNodeExecution::SpawnNext()
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    if (NextPoint >= Prepared->Points.Num())
    {
        TimerWorld->GetTimerManager().ClearTimer(SpawnTimer);
        TimerWorld->GetTimerManager().SetTimer(TailTimer, this, &ThisClass::CompleteTail, Settings->TailDelay, false);
        return;
    }
    const FVector Location = Prepared->Points[NextPoint++];
    EmitPoint(Location, uint64(NextPoint));
}

void UAbilityArcaneShardNodeExecution::EmitPoint(FVector Location, uint64 Sequence)
{
    auto* Owner = GraphOwner.Get();
    AActor* Avatar = nullptr;
    AbilityArcaneShardNodes::ExecuteCue(Input.SourceToken, Location, *Settings);
    if (bStopped || !GraphOwner.IsValid())
    {
        return;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    const auto CapturedHits = AbilityArcaneShardNodes::CaptureHits(Input.SourceToken, Location, *Settings);
    for (const auto& Hit : CapturedHits)
    {
        if (bStopped || !GraphOwner.IsValid())
        {
            return;
        }
        FVector TargetLocation;
        if (!AbilityArcaneShardNodes::ResolveHitLocation(Input.SourceToken, Hit, TargetLocation))
        {
            continue;
        }
        const auto Result = AbilityArcaneShardNodes::ApplyHit(Input.SourceToken, Hit, ResolvedDamage, Location,
                                                              Input.DamageScale, Input.DebuffDamageScale);
        if (bStopped || !GraphOwner.IsValid())
        {
            return;
        }
        if (Settings->bPublishDamageResults)
        {
            Owner->Publish(InvocationId, TEXT("DamageResolved"),
                           AbilityArcaneShardNodes::DamagePayload(Input, Location, Hit, Result, Sequence));
        }
    }
    if (!bStopped && GraphOwner.IsValid())
    {
        auto Payload = Input;
        Payload.Origin = Location;
        Owner->Publish(InvocationId, TEXT("PeriodElapsed"), Payload);
    }
}

void AbilityArcaneShardNodes::ExecuteCue(const FAbilitySourceToken& Source, FVector Location,
                                         const UAbilityArcaneShardNodeConfig& Settings)
{
    AActor* Avatar = nullptr;
    if (Settings.CueTag.IsValid() && AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        FGameplayCueParameters Cue;
        Cue.Location = Location;
        Source.ASC->ExecuteGameplayCue(Settings.CueTag, Cue);
    }
}

TArray<FAbilityArcaneShardHit> AbilityArcaneShardNodes::CaptureHits(const FAbilitySourceToken& Source, FVector Location,
                                                                    const UAbilityArcaneShardNodeConfig& Settings)
{
    TArray<FAbilityArcaneShardHit> CapturedHits;
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return CapturedHits;
    }
    TArray<AActor*> Actors;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Actors, {Avatar}, Settings.Radius, Location);
    TArray<FPureMassCombatTarget> Targets;
    for (AActor* Actor : Actors)
    {
        Targets.Add(FPureMassCombatTarget::FromActor(Actor));
    }
    if (Settings.bIncludeMass)
    {
        for (const auto& Target : PureMassCombat::Gather(Avatar->GetWorld(), Avatar, Location, Settings.Radius))
        {
            if (!Target.IsActor())
            {
                Targets.AddUnique(Target);
            }
        }
    }
    // 先冻结这一片查到的每条生命，再执行伤害；前一个同步 GE 不能让后一个 Actor 的新租约被旧查询命中。
    for (const auto& Target : Targets)
    {
        FAbilityArcaneShardHit Hit;
        FString Error;
        if (Target.IsActor() && !Settings.bRequireHostileActorTargets)
        {
            FAbilitySourceToken Life;
            if (!AbilityExecutionIdentity::CaptureSource(Target.Actor.Get(), Life, Error) || Life.World != Source.World)
            {
                continue;
            }
            Hit.LegacyActorLife = Life;
            // 仅作为历史伤害快照，绝不赋给 LiveTargetToken，也不绕过通用 ResolveTarget 的敌对规则。
            Hit.Token.World = Life.World;
            Hit.Token.Target = Target;
            Hit.Token.ActorLease = Life.EnemyLease;
            Hit.Token.DungeonRunId = Life.DungeonRunId;
        }
        else if (!AbilityExecutionIdentity::CaptureTarget(Source, Target, Hit.Token, Error))
        {
            continue;
        }
        CapturedHits.Add(MoveTemp(Hit));
    }
    return CapturedHits;
}

bool AbilityArcaneShardNodes::ResolveHitLocation(const FAbilitySourceToken& Source, const FAbilityArcaneShardHit& Hit,
                                                 FVector& TargetLocation)
{
    if (Hit.LegacyActorLife.IsSet())
    {
        AActor* TargetActor = nullptr;
        if (!AbilityExecutionIdentity::ResolveSource(Hit.LegacyActorLife.GetValue(), TargetActor))
        {
            return false;
        }
        TargetLocation = TargetActor->GetActorLocation();
    }
    else if (!AbilityExecutionIdentity::ResolveTarget(Source, Hit.Token, TargetLocation))
    {
        return false;
    }
    return true;
}

FAbilityDamageResult AbilityArcaneShardNodes::ApplyHit(const FAbilitySourceToken& Source,
                                                       const FAbilityArcaneShardHit& Hit,
                                                       const FDamageEffectParams& Parameters, FVector Location,
                                                       float DamageScale, float DebuffDamageScale)
{
    FVector TargetLocation;
    if (Location.ContainsNaN() || !FMath::IsFinite(DamageScale) || DamageScale < 0.f ||
        !FMath::IsFinite(DebuffDamageScale) || DebuffDamageScale < 0.f ||
        !FMath::IsFinite(Parameters.BaseDamage * DamageScale) ||
        !FMath::IsFinite(Parameters.DebuffDamage * DebuffDamageScale) ||
        !ResolveHitLocation(Source, Hit, TargetLocation))
    {
        return {};
    }
    auto Params = Parameters;
    Params.BaseDamage *= DamageScale;
    Params.DebuffDamage *= DebuffDamageScale;
    Params.RadialDamageOrigin = Location;
    FRotator ImpulseRotation = (TargetLocation - Location).GetSafeNormal().Rotation();
    ImpulseRotation.Pitch = 15.f;
    Params.KnockbackForce = ImpulseRotation.Vector() * Params.KnockbackForceMagnitude;
    Params.DeathImpulse = ImpulseRotation.Vector() * Params.DeathImpulseMagnitude;
    return Hit.LegacyActorLife.IsSet() ? ApplyLegacyActor(Source, Hit.LegacyActorLife.GetValue(), Params)
                                       : AbilityDamageService::Apply(Source, Hit.Token, Params);
}

FAbilityNodeInput AbilityArcaneShardNodes::DamagePayload(const FAbilityNodeInput& Input, FVector Location,
                                                         const FAbilityArcaneShardHit& Hit,
                                                         const FAbilityDamageResult& Result, uint64 Sequence)
{
    auto Payload = Input;
    Payload.Origin = Location;
    Payload.Impact = FAbilityImpactSnapshot{Result.ImpactLocation, Input.Direction, Hit.Token, Sequence};
    Payload.LiveTargetToken.Reset();
    Payload.bHasDamageResult = true;
    Payload.bDamageAttempted = Result.bAttempted;
    Payload.bKilled = Result.bKilled;
    Payload.DamageApplied = Result.Applied;
    Payload.ActualDamage = Result.ActualDamage;
    return Payload;
}

void UAbilityArcaneShardNodeExecution::CompleteTail()
{
    if (!bStopped && GraphOwner.IsValid())
    {
        GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityArcaneShardNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    Super::Stop(Reason);
    if (TimerWorld.IsValid())
    {
        TimerWorld->GetTimerManager().ClearTimer(SpawnTimer);
        TimerWorld->GetTimerManager().ClearTimer(TailTimer);
    }
    if (Prepared && IsValid(Prepared->PointCollection))
    {
        Prepared->PointCollection->Destroy();
        Prepared->PointCollection = nullptr;
    }
    Prepared = nullptr;
    Settings = nullptr;
}

void AbilityArcaneShardNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Shape.ArcaneShards")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Shape.ArcaneShards");
    Descriptor.ConfigClass = UAbilityArcaneShardNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityArcaneShardNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsMassTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    Descriptor.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("PeriodElapsed"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                        {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                        {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Arcane shard registration failed: %s"), *Error);
}
