// 调用链：动画宿主/节点 -> 共享角色动作 -> 原Actor/Mass伤害、武器弹体、独立格挡句柄；每次效果重验生命。
#include "AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actor/StanderWeaponProjectile.h"
#include "Character/StanderPlayer.h"
#include "Components/CapsuleComponent.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Engine/World.h"
#include "MyGameplayTags.h"
#include "NiagaraSystem.h"
#include "Player/StanderCombatConfig.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

namespace
{
    bool Current(const FAbilitySourceToken& Source, const FAbilityCharacterActionCallbacks& Callbacks, AActor*& Avatar)
    {
        return AbilityExecutionIdentity::ResolveSource(Source, Avatar) &&
               (!Callbacks.IsCurrent || Callbacks.IsCurrent());
    }
    FDamageEffectParams TargetParams(const FDamageEffectParams& Base, AActor* Source, const FVector& Target)
    {
        auto Params = Base;
        const FVector Direction = (Target - Source->GetActorLocation()).Rotation().Vector();
        Params.KnockbackForce = Direction * Params.KnockbackForceMagnitude;
        Params.DeathImpulse = Direction * Params.DeathImpulseMagnitude;
        Params.DebuffChance = 0.f;
        Params.bIsRadialDamage = false;
        return Params;
    }
    FAbilityNodeInput DamageOutput(const FAbilityNodeInput& Input, const FAbilityTargetToken& Target,
                                   const FVector& Location, const FAbilityDamageResult& Result)
    {
        auto Output = Input;
        Output.LiveTargetToken.Reset();
        Output.Origin = Location;
        FAbilityImpactSnapshot Impact;
        Impact.Location = Location;
        Impact.Direction = (Location - Input.Origin).GetSafeNormal();
        Impact.ConfirmedTarget = Target;
        Output.Impact = Impact;
        Output.bHasDamageResult = true;
        Output.bDamageAttempted = Result.bAttempted;
        Output.bKilled = Result.bKilled;
        Output.DamageApplied = Result.Applied;
        Output.ActualDamage = Result.ActualDamage;
        return Output;
    }
} // namespace

bool UAbilityCharacterStrikeNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Strike.Damage) || Strike.Damage < 0.f || !FMath::IsFinite(Strike.Radius) ||
        Strike.Radius <= 0.f || !FMath::IsFinite(Strike.Angle) || Strike.Angle <= 0.f || Strike.Angle > 360.f ||
        LocalDeathImpulse.ContainsNaN() || !FMath::IsFinite(SwingOffset))
    {
        Error = TEXT("Character strike requires finite damage, geometry and impulse.");
    }
    return Error.IsEmpty();
}

void AbilityCharacterActions::ExecuteStrike(const FAbilitySourceToken& Source, const FDamageEffectParams& Damage,
                                            const UAbilityCharacterStrikeNodeConfig& Settings, const FVector& Origin,
                                            const FVector& Forward, const FAbilityCharacterActionCallbacks& Callbacks)
{
    AActor* Avatar = nullptr;
    FString Error;
    if (!Current(Source, Callbacks, Avatar) || !Settings.Validate(Error))
    {
        return;
    }
    auto* VisualSource = Cast<ASwordShieldPlayer>(Avatar);
    if (VisualSource && Settings.SwingEffect)
    {
        VisualSource->ShowSwordImpact(Settings.SwingEffect, Origin + Forward * Settings.SwingOffset);
    }
    TArray<FPureMassCombatTarget> Candidates;
    if (Settings.Profile == EAbilityCharacterStrikeProfile::SwordShieldActor)
    {
        TArray<AActor*> Actors;
        UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Actors, {Avatar}, Settings.Strike.Radius + 100,
                                                            Origin);
        for (AActor* Actor : Actors)
        {
            Candidates.Add(FPureMassCombatTarget::FromActor(Actor));
        }
    }
    else
    {
        Candidates = PureMassCombat::Gather(Source.World.Get(), Avatar, Origin, Settings.Strike.Radius + 100);
    }
    TArray<FAbilityTargetToken> Targets;
    TSet<FPureMassCombatTarget> Seen;
    for (const auto& Target : Candidates)
    {
        FVector Location;
        if (Seen.Contains(Target) || !PureMassCombat::Resolve(Source.World.Get(), Avatar, Target, Location))
        {
            continue;
        }
        Seen.Add(Target);
        if (Settings.Profile == EAbilityCharacterStrikeProfile::WeaponMixed)
        {
            if (FMath::Abs(Origin.Z - Location.Z) > 180.f ||
                FVector::DistSquared2D(Origin, Location) > FMath::Square(Settings.Strike.Radius) ||
                (Settings.Strike.Angle < 360.f && FVector::DotProduct(Forward, (Location - Origin).GetSafeNormal2D()) <
                                                      FMath::Cos(FMath::DegreesToRadians(Settings.Strike.Angle * .5f))))
            {
                continue;
            }
        }
        else if (!Settings.Strike.Contains(Origin, Forward, Location))
        {
            continue;
        }
        if (Settings.Profile == EAbilityCharacterStrikeProfile::SwordShieldActor)
        {
            FHitResult Wall;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(SwordMeleeWall), false, Avatar);
            Query.AddIgnoredActor(Target.Actor.Get());
            if (Source.World->LineTraceSingleByObjectType(Wall, Origin, Location,
                                                          FCollisionObjectQueryParams(ECC_WorldStatic), Query))
            {
                continue;
            }
        }
        else
        {
            if (!PureMassCombat::Visible(Source.World.Get(), Avatar, Origin, Target))
            {
                continue;
            }
            if (auto* Director = ADungeonRunDirector::Find(Source.World.Get());
                Director && Director->GetRoomIndex(Origin) != Director->GetRoomIndex(Location))
            {
                continue;
            }
        }
        FAbilityTargetToken Token;
        if (AbilityExecutionIdentity::CaptureTarget(Source, Target, Token, Error))
        {
            Targets.Add(Token);
        }
    }
    for (const auto& Target : Targets)
    {
        FVector Location;
        if (!Current(Source, Callbacks, Avatar))
        {
            return;
        }
        if (!AbilityExecutionIdentity::ResolveTarget(Source, Target, Location))
        {
            continue;
        }
        auto Params = TargetParams(Damage, Avatar, Location);
        Params.BaseDamage = Settings.Strike.Damage;
        if (Settings.Profile != EAbilityCharacterStrikeProfile::SwordShieldActor)
        {
            FVector ImpulseForward = Forward;
            const FVector Radial = (Location - Origin).GetSafeNormal2D();
            if (Settings.bRadialImpulse && !Radial.IsNearlyZero())
            {
                ImpulseForward = Radial;
            }
            Params.DeathImpulse = UStanderCombatConfig::WorldDeathImpulse(Settings.LocalDeathImpulse, ImpulseForward);
        }
        if (Target.Target.IsActor() && Callbacks.BeforeActorDamage)
        {
            Callbacks.BeforeActorDamage(Target.Target.Actor.Get());
        }
        if (!Current(Source, Callbacks, Avatar))
        {
            return;
        }
        const auto Result = AbilityDamageService::Apply(Source, Target, Params);
        const bool bApplied = Target.Target.IsActor() ? Result.bAttempted : Result.ActualDamage.Get(0.f) > 0.f;
        if (bApplied && Current(Source, Callbacks, Avatar) && VisualSource && Settings.HitEffect)
        {
            VisualSource->ShowSwordImpact(Settings.HitEffect, Location);
        }
        if (Callbacks.AfterDamage && !Callbacks.AfterDamage(Target, Location, Result))
        {
            return;
        }
    }
}

AStanderWeaponProjectile* AbilityCharacterActions::SpawnWeaponProjectile(const FAbilitySourceToken& Source,
                                                                         const FDamageEffectParams& Damage,
                                                                         const FStanderWeaponActionEvent& Event,
                                                                         const FVector& Origin, const FVector& Forward,
                                                                         int32 OriginRoom)
{
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return nullptr;
    }
    auto* Player = Cast<AStanderPlayer>(Avatar);
    if (!Player)
    {
        return nullptr;
    }
    FVector Direction = Forward.RotateAngleAxis(Event.LaunchYaw, FVector::UpVector);
    const FVector Start =
        Event.SocketName.IsNone() ? Origin + Direction * 60.f : Player->GetMesh()->GetSocketLocation(Event.SocketName);
    const float Range = Event.ProjectileSpeed * Event.ProjectileLifetime;
    float Nearest = Range * Range;
    for (const auto& Target : PureMassCombat::Gather(Source.World.Get(), Player, Origin, Range))
    {
        FVector Location;
        if (!PureMassCombat::Resolve(Source.World.Get(), Player, Target, Location) ||
            FMath::Abs(Location.Z - Origin.Z) > 250.f ||
            !UStanderWeaponActionConfig::IsInAimCone(Forward.RotateAngleAxis(Event.LaunchYaw, FVector::UpVector),
                                                     Location - Origin) ||
            !PureMassCombat::Visible(Source.World.Get(), Player, Origin, Target))
        {
            continue;
        }
        if (auto* Director = ADungeonRunDirector::Find(Source.World.Get());
            Director && Director->GetRoomIndex(Location) != OriginRoom)
        {
            continue;
        }
        const float Distance = FVector::DistSquared2D(Origin, Location);
        if (Distance < Nearest)
        {
            Nearest = Distance;
            Direction = (Location - Start).GetSafeNormal();
        }
    }
    auto Params = Damage;
    Params.SourceAbilitySystemComponent = Source.ASC.Get();
    Params.WorldContextObject = Avatar;
    Params.BaseDamage = Event.Damage;
    Params.DebuffChance = 0.f;
    Params.bIsRadialDamage = false;
    Params.DeathImpulse = UStanderCombatConfig::WorldDeathImpulse(Event.DeathImpulse, Direction.GetSafeNormal2D());
    FActorSpawnParameters Spawn;
    Spawn.Owner = Player;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Projectile = Source.World->SpawnActor<AStanderWeaponProjectile>(Start, Direction.Rotation(), Spawn);
    if (Projectile)
    {
        Projectile->SetExecutionSourceToken(Source);
        Projectile->Initialize(Player, Event, Params, Direction);
    }
    return Projectile;
}

FActiveGameplayEffectHandle AbilityCharacterActions::OpenGuardWindow(const FAbilitySourceToken& Source, float Duration)
{
    AActor* Avatar = nullptr;
    if (!FMath::IsFinite(Duration) || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return {};
    }
    auto Spec = Source.ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(), 1.f, Source.ASC->MakeEffectContext());
    if (!Spec.IsValid())
    {
        return {};
    }
    if (Duration >= 0.f)
    {
        Spec.Data->SetDuration(Duration, true);
    }
    return Source.ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void AbilityCharacterActions::CloseGuardWindow(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle)
{
    const auto Owned = Handle;
    Handle.Invalidate();
    if (ASC && Owned.IsValid())
    {
        ASC->RemoveActiveGameplayEffect(Owned);
    }
}

bool AbilityCharacterActions::IsBlockedDamage(const FGameplayEffectSpec& Spec)
{
    if (!UMyAbilitySystemLibrary::IsBlockedHit(Spec.GetContext()))
    {
        return false;
    }
    for (const auto& Pair : FMyGameplayTags::Get().DamageTypesToResistance)
    {
        if (Spec.GetSetByCallerMagnitude(Pair.Key, false, 0.f) > 0.f)
        {
            return true;
        }
    }
    return false;
}

void AbilityCharacterActions::GatherTelegraphedTargets(const FAbilitySourceToken& Source,
                                                       const FStoneGolemAttackTuning& Shape, const FVector& Center,
                                                       float Yaw, const TArray<FVector>& Surface,
                                                       const TArray<int32>& Triangles, AActor* SelectedTarget,
                                                       TArray<AActor*>& Out)
{
    Out.Reset();
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return;
    }
    TArray<AActor*> Candidates;
    UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Candidates, {Avatar}, Shape.QueryRadius() + 200,
                                                        Center);
    for (AActor* Target : Candidates)
    {
        if ((Shape.bSingleTarget && Target != SelectedTarget) || !IsValid(Target) ||
            !Target->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Target) ||
            !UMyAbilitySystemLibrary::IsNotFriend(Avatar, Target) ||
            !ADungeonRunDirector::AllowsCombat(Avatar, Target) ||
            !UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
        {
            continue;
        }
        FVector Feet = Target->GetActorLocation();
        if (const auto* Character = Cast<ACharacter>(Target))
        {
            Feet.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        }
        FVector Ground;
        if (!Shape.Contains(Center, Yaw, Feet) || !StoneGolemGeometry::SurfaceContains(Surface, Triangles, Feet) ||
            !StoneGolemGeometry::Project(Source.World.Get(), Avatar, FVector(Feet.X, Feet.Y, Center.Z),
                                         Shape.HeightTolerance, Ground) ||
            FMath::Abs(Feet.Z - Ground.Z) > Shape.HeightTolerance ||
            !StoneGolemGeometry::Visible(Source.World.Get(), Avatar, Center, Ground))
        {
            continue;
        }
        Out.AddUnique(Target);
    }
}

void AbilityCharacterActions::ApplyTelegraphedTargets(const FAbilitySourceToken& Source,
                                                      const FDamageEffectParams& Damage,
                                                      const FStoneGolemAttackTuning& Shape,
                                                      const TArray<AActor*>& Targets,
                                                      const FAbilityCharacterActionCallbacks& Callbacks)
{
    TArray<FAbilityTargetToken> Captured;
    FString Error;
    for (AActor* Actor : Targets)
    {
        FAbilityTargetToken Token;
        if (AbilityExecutionIdentity::CaptureTarget(Source, FPureMassCombatTarget::FromActor(Actor), Token, Error))
        {
            Captured.Add(Token);
        }
    }
    // 报告委托可同步回收目标并开始新生命，必须先固定整批身份，再报告原批次的首个目标。
    if (!Captured.IsEmpty() && Callbacks.BeforeActorDamage)
    {
        AActor* Avatar = nullptr;
        if (!Current(Source, Callbacks, Avatar))
        {
            return;
        }
        Callbacks.BeforeActorDamage(Captured[0].Target.Actor.Get());
    }
    for (const auto& Target : Captured)
    {
        AActor* Avatar = nullptr;
        FVector Location;
        if (!Current(Source, Callbacks, Avatar))
        {
            return;
        }
        if (!AbilityExecutionIdentity::ResolveTarget(Source, Target, Location))
        {
            continue;
        }
        auto Params = TargetParams(Damage, Avatar, Location);
        Params.KnockbackChance = Shape.KnockbackSpeed > 0 || Shape.KnockbackUpSpeed > 0 ? 100 : 0;
        Params.KnockbackForce = (Location - Avatar->GetActorLocation()).GetSafeNormal2D() * Shape.KnockbackSpeed;
        Params.KnockbackForce.Z = Shape.KnockbackUpSpeed;
        const auto Result = AbilityDamageService::Apply(Source, Target, Params);
        if (Callbacks.AfterDamage && !Callbacks.AfterDamage(Target, Location, Result))
        {
            return;
        }
    }
}

bool UAbilityWeaponProjectileNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Event.ProjectileSpeed) || Event.ProjectileSpeed <= 0.f ||
        !FMath::IsFinite(Event.ProjectileLifetime) || Event.ProjectileLifetime <= 0.f ||
        !FMath::IsFinite(Event.Damage) || Event.Damage < 0.f || !FMath::IsFinite(Event.LaunchYaw) ||
        Event.DeathImpulse.ContainsNaN())
    {
        Error = TEXT("Weapon projectile requires finite positive speed/lifetime and valid damage/impulse.");
    }
    return Error.IsEmpty();
}

bool UAbilityGuardWindowNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Duration) || Duration <= 0.f)
    {
        Error = TEXT("Guard window requires a finite positive duration.");
    }
    return Error.IsEmpty();
}

bool UAbilityTelegraphedContactNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Shape.Radius) || Shape.Radius <= 0.f || !FMath::IsFinite(Shape.Angle) || Shape.Angle <= 0.f ||
        Shape.Angle > 360.f || !FMath::IsFinite(Shape.Length) || Shape.Length <= 0.f || !FMath::IsFinite(Shape.Width) ||
        Shape.Width <= 0.f || !FMath::IsFinite(Shape.HeightTolerance) || Shape.HeightTolerance < 0.f ||
        !FMath::IsFinite(Shape.KnockbackSpeed) || !FMath::IsFinite(Shape.KnockbackUpSpeed) ||
        LockedSurface.ContainsByPredicate(
            [](const FVector& Point)
            {
                return Point.ContainsNaN();
            }) ||
        LockedTriangles.Num() % 3 != 0 ||
        LockedTriangles.ContainsByPredicate(
            [this](int32 Index)
            {
                return !LockedSurface.IsValidIndex(Index);
            }))
    {
        Error = TEXT("Telegraphed contact requires finite geometry and valid locked surface indices.");
    }
    return Error.IsEmpty();
}

bool UAbilityCharacterStrikeNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                 const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                 FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [&]()
    {
        return !bStopped && !Owner.IsFinished();
    };
    Callbacks.AfterDamage =
        [&](const FAbilityTargetToken& Target, const FVector& Location, const FAbilityDamageResult& Result)
    {
        return !bStopped && !Owner.IsFinished() &&
               Owner.Publish(Id, TEXT("DamageResolved"), DamageOutput(Input, Target, Location, Result));
    };
    AbilityCharacterActions::ExecuteStrike(Input.SourceToken, Input.DamageParams,
                                           *CastChecked<UAbilityCharacterStrikeNodeConfig>(&Config), Input.Origin,
                                           Input.Direction.GetSafeNormal2D(), Callbacks);
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

bool UAbilityWeaponProjectileNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                  const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                  FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityWeaponProjectileNodeConfig>(&Config);
    int32 Room = INDEX_NONE;
    if (auto* Director = ADungeonRunDirector::Find(Input.SourceToken.World.Get()))
    {
        Room = Director->GetRoomIndex(Input.Origin);
    }
    Projectile = AbilityCharacterActions::SpawnWeaponProjectile(Input.SourceToken, Input.DamageParams, Settings.Event,
                                                                Input.Origin, Input.Direction.GetSafeNormal2D(), Room);
    if (!Projectile.IsValid())
    {
        Error = TEXT("Weapon projectile requires a valid Stander source and spawn.");
        return false;
    }
    Projectile->OnDestroyed.AddDynamic(this, &ThisClass::ProjectileDestroyed);
    return Owner.Publish(Id, TEXT("Spawned"), Input);
}

void UAbilityWeaponProjectileNodeExecution::ProjectileDestroyed(AActor* Actor)
{
    if (bStopped)
    {
        return;
    }
    Projectile.Reset();
    if (auto* Owner = GraphOwner.Get())
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityWeaponProjectileNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    auto* Actor = Projectile.Get();
    Projectile.Reset();
    Super::Stop(Reason);
    if (IsValid(Actor))
    {
        Actor->OnDestroyed.RemoveDynamic(this, &ThisClass::ProjectileDestroyed);
        if (Reason != EAbilityGraphEndReason::Completed)
        {
            Actor->Destroy();
        }
    }
}

bool UAbilityGuardWindowNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityGuardWindowNodeConfig>(&Config);
    Remaining = Settings.Duration;
    bCloseOnBlock = Settings.bCloseOnBlock;
    Guard = AbilityCharacterActions::OpenGuardWindow(Input.SourceToken);
    if (!Guard.IsValid())
    {
        Error = TEXT("Guard effect could not be applied to the source ASC.");
        return false;
    }
    Listener = Input.SourceToken.ASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &ThisClass::GuardHit);
    return Owner.Publish(Id, TEXT("Opened"), Input);
}

void UAbilityGuardWindowNodeExecution::GuardHit(UAbilitySystemComponent* ASC, const FGameplayEffectSpec& Spec,
                                                FActiveGameplayEffectHandle Effect)
{
    AActor* Avatar = nullptr;
    if (bStopped || bBlockPending || !Guard.IsValid() || !AbilityCharacterActions::IsBlockedDamage(Spec) ||
        !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return;
    }
    FAbilityTargetToken Target;
    FString Error;
    auto* AttackerASC = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
    AActor* Attacker = AttackerASC ? AttackerASC->GetAvatarActor() : nullptr;
    bool bCaptured = IsValid(Attacker) &&
                     AbilityExecutionIdentity::CaptureTarget(Input.SourceToken,
                                                             FPureMassCombatTarget::FromActor(Attacker), Target, Error);
    if (!bCaptured && Spec.Def && Spec.Def->IsA<UPureMassIncomingDamageEffect>())
    {
        const FHitResult* Hit = Spec.GetContext().GetHitResult();
        bCaptured =
            Hit && Hit->Item > 0 &&
            AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget(Hit->Item), Target, Error);
    }
    if (!bCaptured)
    {
        return;
    }
    BlockInput = Input;
    BlockInput.LiveTargetToken = Target;
    bBlockPending = true;
    // 在下一次图推进才发反击事件，避免在伤害回调栈内切换动作或关闭ASC监听。
}

void UAbilityGuardWindowNodeExecution::Advance(float Delta)
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || !Owner || Owner->IsFinished())
    {
        return;
    }
    if (bBlockPending)
    {
        bBlockPending = false;
        if (!Owner->Publish(InvocationId, TEXT("Blocked"), BlockInput))
        {
            return;
        }
        if (bCloseOnBlock)
        {
            Owner->Publish(InvocationId, TEXT("Completed"), Input);
            return;
        }
    }
    Remaining -= Delta;
    if (Remaining <= 0.f)
    {
        Owner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityGuardWindowNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    Super::Stop(Reason);
    if (auto* ASC = Input.SourceToken.ASC.Get())
    {
        ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Listener);
        AbilityCharacterActions::CloseGuardWindow(ASC, Guard);
    }
    Listener.Reset();
    Guard.Invalidate();
    bBlockPending = false;
}

bool UAbilityTelegraphedContactNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                    const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                                    FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityTelegraphedContactNodeConfig>(&Config);
    TArray<FVector> Surface = Settings.LockedSurface;
    TArray<int32> Triangles = Settings.LockedTriangles;
    const float Yaw = Input.Direction.Rotation().Yaw;
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return false;
    }
    if (Surface.IsEmpty())
    {
        StoneGolemGeometry::BuildSurface(Input.SourceToken.World.Get(), Avatar, Settings.Shape, Input.Origin, Yaw,
                                         Surface, Triangles);
    }
    AActor* Selected = nullptr;
    if (Input.LiveTargetToken.IsSet())
    {
        FVector Position;
        if (AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Position))
        {
            Selected = Input.LiveTargetToken->Target.Actor.Get();
        }
    }
    TArray<AActor*> Targets;
    AbilityCharacterActions::GatherTelegraphedTargets(Input.SourceToken, Settings.Shape, Input.Origin, Yaw, Surface,
                                                      Triangles, Selected, Targets);
    FAbilityCharacterActionCallbacks Callbacks;
    Callbacks.IsCurrent = [&]()
    {
        return !bStopped && !Owner.IsFinished();
    };
    Callbacks.AfterDamage =
        [&](const FAbilityTargetToken& Target, const FVector& Location, const FAbilityDamageResult& Result)
    {
        return !bStopped && !Owner.IsFinished() &&
               Owner.Publish(Id, TEXT("DamageResolved"), DamageOutput(Input, Target, Location, Result));
    };
    AbilityCharacterActions::ApplyTelegraphedTargets(Input.SourceToken, Input.DamageParams, Settings.Shape, Targets,
                                                     Callbacks);
    return bStopped || Owner.IsFinished() || Owner.Publish(Id, TEXT("Completed"), Input);
}

void AbilityCharacterNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Strike;
    Strike.TypeId = TEXT("Combat.CharacterStrike");
    Strike.ConfigClass = UAbilityCharacterStrikeNodeConfig::StaticClass();
    Strike.ExecutionClass = UAbilityCharacterStrikeNodeExecution::StaticClass();
    Strike.InputPayload = EAbilityPortPayload::Origin;
    Strike.bSupportsActorTargets = Strike.bSupportsMassTargets = Strike.bSupportsSnapshotTargets = true;
    Strike.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto Projectile = Strike;
    Projectile.TypeId = TEXT("Delivery.WeaponProjectile");
    Projectile.ConfigClass = UAbilityWeaponProjectileNodeConfig::StaticClass();
    Projectile.ExecutionClass = UAbilityWeaponProjectileNodeExecution::StaticClass();
    Projectile.bIsAsync = true;
    Projectile.Ports = {{TEXT("Spawned"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto Guard = Strike;
    Guard.TypeId = TEXT("Status.GuardWindow");
    Guard.ConfigClass = UAbilityGuardWindowNodeConfig::StaticClass();
    Guard.ExecutionClass = UAbilityGuardWindowNodeExecution::StaticClass();
    Guard.bIsAsync = true;
    Guard.Ports = {{TEXT("Opened"), EAbilityPortPayload::Origin, false},
                   {TEXT("Blocked"), EAbilityPortPayload::Origin, false},
                   {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto Contact = Strike;
    Contact.TypeId = TEXT("Combat.TelegraphedContact");
    Contact.ConfigClass = UAbilityTelegraphedContactNodeConfig::StaticClass();
    Contact.ExecutionClass = UAbilityTelegraphedContactNodeExecution::StaticClass();
    Contact.bSupportsMassTargets = false;
    for (const auto& Descriptor : {Strike, Projectile, Guard, Contact})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Character action registration: %s"), *Error);
        }
    }
}
