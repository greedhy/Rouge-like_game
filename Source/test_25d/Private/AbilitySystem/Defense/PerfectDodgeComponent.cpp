// 调用链：GA/注册节点预约并Commit -> Begin固定起点 -> 原攻击适配器确认 -> 时间服务；Tick推进共享GroundDash。
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interface/CombatInterface.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MyGameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "PureMass/PureMassSubsystem.h"
#include "Roguelike/DungeonRunDirector.h"

bool FPerfectDodgeSettings::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(Distance) || Distance <= 0.f || Distance > 2000.f || !FMath::IsFinite(MoveDuration) ||
        MoveDuration <= 0.f || MoveDuration > 2.f || !FMath::IsFinite(PerfectWindow) || PerfectWindow <= 0.f ||
        PerfectWindow > MoveDuration || !FMath::IsFinite(Invulnerability) || Invulnerability < PerfectWindow ||
        Invulnerability > MoveDuration || !FMath::IsFinite(Recovery) || Recovery < MoveDuration || Recovery > 5.f ||
        !FMath::IsFinite(RewardLock) || RewardLock < 0.f || RewardLock > 60.f)
    {
        Error = TEXT("Dodge requires finite bounded distance, ordered windows and recovery.");
        return false;
    }
    return Reward.Validate(Error);
}

UPerfectDodgeComponent::UPerfectDodgeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

UPerfectDodgeComponent* UPerfectDodgeComponent::FindOrCreate(ACharacter* Character)
{
    if (!IsValid(Character) || !Character->HasAuthority())
    {
        return nullptr;
    }
    if (auto* Existing = Character->FindComponentByClass<UPerfectDodgeComponent>())
    {
        return Existing;
    }
    auto* Component = NewObject<UPerfectDodgeComponent>(Character);
    Character->AddInstanceComponent(Component);
    Component->RegisterComponent();
    if (auto* Combat = Cast<ICombatInterface>(Character))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(Component, &ThisClass::OnOwnerDeath);
    }
    return Component;
}

bool UPerfectDodgeComponent::CanStart(ACharacter* Character)
{
    FAbilitySourceToken Token;
    FString Error;
    if (!Character || !Character->GetWorld() || Character->GetNetMode() != NM_Standalone ||
        !AbilityExecutionIdentity::CaptureSource(Character, Token, Error) ||
        !Character->GetCharacterMovement()->IsMovingOnGround() ||
        UAbilityMovementOwnershipComponent::HasActiveClaim(Character))
    {
        return false;
    }
    const auto* Defense = Character->FindComponentByClass<UPerfectDodgeComponent>();
    const auto& Tags = FMyGameplayTags::Get();
    return (!Defense || (!Defense->ExecutionId.IsValid() && !Defense->bInvalidatingLife)) &&
           !Token.ASC->HasMatchingGameplayTag(Tags.Debuff_Stun) &&
           !Token.ASC->HasMatchingGameplayTag(Tags.Effects_HitReact);
}

FGuid UPerfectDodgeComponent::Reserve(const FAbilitySourceToken& InSource, UObject* Requester,
                                      const FPerfectDodgeSettings& InSettings, const FVector& InDirection,
                                      FString& Error)
{
    Error.Reset();
    AActor* Avatar = nullptr;
    auto* Character = Cast<ACharacter>(GetOwner());
    if (!InSettings.Validate(Error) || !IsValid(Requester) || !CanStart(Character) ||
        !AbilityExecutionIdentity::ResolveSource(InSource, Avatar) || Avatar != Character ||
        InDirection.ContainsNaN() || InDirection.GetSafeNormal2D().IsNearlyZero())
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("Dodge requires a live grounded standalone Character with free movement.");
        }
        return {};
    }
    MovementOwner = UAbilityMovementOwnershipComponent::FindOrCreate(Character);
    MovementLease = MovementOwner->TryAcquire(this, InSource, Error);
    if (!MovementLease.IsValid())
    {
        return {};
    }
    Source = InSource;
    Settings = InSettings;
    ExecutionOwner = Requester;
    ExecutionId = FGuid::NewGuid();
    ExecutionGeneration = LifeGeneration;
    Direction = InDirection.GetSafeNormal2D();
    bBegun = false;
    bWindowsClosed = false;
    MoveState = {};
    OriginRoom = INDEX_NONE;
    if (auto* Director = ADungeonRunDirector::Find(GetWorld()))
    {
        OriginRoom = Director->GetRoomIndex(Character->GetActorLocation());
    }
    return ExecutionId;
}

bool UPerfectDodgeComponent::Begin(FGuid Execution)
{
    if (Execution != ExecutionId || !Execution.IsValid() || bBegun || !Current())
    {
        return false;
    }
    auto* Character = CastChecked<ACharacter>(GetOwner());
    WitnessCenter = Character->GetActorLocation();
    WitnessRadius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
    WitnessHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    StartTime = Now();
    bBegun = true;
    // 注册节点和专用GA都由组件持有这一次ASC计数，完整后摇结束/取消时成对归还。
    BlockedASC = Source.ASC;
    BlockedASC->BlockAbilitiesWithTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities"))));
    if (Settings.DodgeEffect)
    {
        DodgeVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(
            Settings.DodgeEffect, Character->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
            EAttachLocation::KeepRelativeOffset, false);
    }
    Character->GetCharacterMovement()->StopMovementImmediately();
    Character->GetCharacterMovement()->DisableMovement();
    return Current() && ExecutionId == Execution;
}

bool UPerfectDodgeComponent::Current() const
{
    AActor* Avatar = nullptr;
    return ExecutionGeneration == LifeGeneration && ExecutionOwner.IsValid() &&
           AbilityExecutionIdentity::ResolveSource(Source, Avatar) && Avatar == GetOwner();
}

double UPerfectDodgeComponent::Now() const
{
    const auto* Time = GetWorld() ? GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr;
    return Time ? Time->GetGameplayRealTime() : 0.;
}

bool UPerfectDodgeComponent::WindowOpen(float Duration) const
{
    const double Age = Now() - StartTime;
    return bBegun && !bWindowsClosed && ExecutionId.IsValid() && Current() && Age >= 0. && Age < Duration &&
           !Source.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun) &&
           !Source.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Effects_HitReact);
}

bool UPerfectDodgeComponent::IsRunning(FGuid Execution) const
{
    return Execution.IsValid() && Execution == ExecutionId && Current();
}

bool UPerfectDodgeComponent::WasPerfect(FGuid Execution) const
{
    return Execution.IsValid() && LastPerfectId == Execution;
}

bool UPerfectDodgeComponent::WasCancelled(FGuid Execution) const
{
    return LastFinishedId != Execution || bLastCancelled;
}

bool UPerfectDodgeComponent::GetWitness(FVector& Center, float& Radius, float& HalfHeight) const
{
    if (!WindowOpen(Settings.PerfectWindow))
    {
        return false;
    }
    Center = WitnessCenter;
    Radius = WitnessRadius;
    HalfHeight = WitnessHalfHeight;
    return true;
}

bool UPerfectDodgeComponent::VisibleFrom(const FVector& Origin, const FVector& Center, AActor* Ignore) const
{
    if (Origin.ContainsNaN() || Center.ContainsNaN())
    {
        return false;
    }
    if (const auto* Director = ADungeonRunDirector::Find(GetWorld());
        Director && Director->GetRoomIndex(Origin) != Director->GetRoomIndex(Center))
    {
        return false;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PerfectDodgeWitness), false, GetOwner());
    Query.AddIgnoredActor(Ignore);
    FHitResult Wall;
    return !GetWorld()->LineTraceSingleByObjectType(Wall, Origin, Center, FCollisionObjectQueryParams(ECC_WorldStatic),
                                                    Query);
}

void UPerfectDodgeComponent::RewardAttack(FGuid AttackId)
{
    auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>();
    if (!AttackId.IsValid() || !WindowOpen(Settings.PerfectWindow) || WasPerfect(ExecutionId) ||
        RewardedAttacks.Contains(AttackId) || Now() < NextRewardTime || !Time || Time->HasActiveRequests())
    {
        return;
    }
    // 原子关闭本次奖励入口，再调用可能同步通知外部的时间服务，防止多骨骼/重复Trace重入。
    LastPerfectId = ExecutionId;
    RewardedAttacks.Add(AttackId);
    if (RewardedAttacks.Num() > 64)
    {
        RewardedAttacks.RemoveAt(0);
    }
    NextRewardTime = Now() + Settings.RewardLock;
    FString Error;
    RewardLease = Time->Acquire(Source, Settings.Reward, this, Error);
    if (!RewardLease.IsValid())
    {
        LastPerfectId.Invalidate();
    }
    else if (Settings.PerfectEffect)
    {
        PerfectVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(
            Settings.PerfectEffect, GetOwner()->GetRootComponent(), NAME_None, FVector::ZeroVector,
            FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
    }
}

void UPerfectDodgeComponent::ObserveActorAttack(const FAbilitySourceToken& AttackSource, FGuid AttackId,
                                                const FVector& Origin,
                                                const TFunction<bool(const FVector&, float, float)>& Contains,
                                                AActor* OnlyTarget, double EarliestSampleTime)
{
    AActor* Attacker = nullptr;
    if (!Contains || !AbilityExecutionIdentity::ResolveSource(AttackSource, Attacker))
    {
        return;
    }
    for (TActorIterator<ACharacter> It(AttackSource.World.Get()); It; ++It)
    {
        if (OnlyTarget && OnlyTarget != *It)
        {
            continue;
        }
        auto* Defense = It->FindComponentByClass<UPerfectDodgeComponent>();
        FVector Center;
        float Radius = 0.f;
        float HalfHeight = 0.f;
        if (Defense && (EarliestSampleTime < 0. || EarliestSampleTime >= Defense->StartTime) &&
            Defense->GetWitness(Center, Radius, HalfHeight) && UMyAbilitySystemLibrary::IsNotFriend(Attacker, *It) &&
            ADungeonRunDirector::AllowsCombat(Attacker, *It) && Contains(Center, Radius, HalfHeight) &&
            Defense->VisibleFrom(Origin, Center, Attacker))
        {
            Defense->RewardAttack(AttackId);
        }
    }
}

bool UPerfectDodgeComponent::TryDodgeActorHit(AActor* Target, const FAbilitySourceToken& AttackSource, FGuid AttackId,
                                              bool bAllowPerfect)
{
    auto* Defense = IsValid(Target) ? Target->FindComponentByClass<UPerfectDodgeComponent>() : nullptr;
    AActor* Attacker = nullptr;
    if (!Defense || !AbilityExecutionIdentity::ResolveSource(AttackSource, Attacker) ||
        AttackSource.World != Defense->Source.World || !UMyAbilitySystemLibrary::IsNotFriend(Attacker, Target) ||
        !ADungeonRunDirector::AllowsCombat(Attacker, Target) || !Defense->WindowOpen(Defense->Settings.Invulnerability))
    {
        return false;
    }
    if (bAllowPerfect)
    {
        Defense->RewardAttack(AttackId);
    }
    return true;
}

bool UPerfectDodgeComponent::TryDodgeMassHit(AActor* Target, int32 EntityId, FMassEntityHandle Entity,
                                             const FVector& Origin, FGuid AttackId)
{
    auto* Defense = IsValid(Target) ? Target->FindComponentByClass<UPerfectDodgeComponent>() : nullptr;
    auto* World = IsValid(Target) ? Target->GetWorld() : nullptr;
    const auto* Mass = World ? World->GetSubsystem<UPureMassSubsystem>() : nullptr;
    const auto* Entities = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!Defense || !Mass || !Entities || !Entity.IsSet() || Mass->GetEntity(EntityId) != Entity ||
        !Entities->GetEntityManager().IsEntityValid(Entity) || !Target->ActorHasTag(TEXT("Player")) ||
        !Defense->WindowOpen(Defense->Settings.Invulnerability) ||
        !Defense->VisibleFrom(Origin, Target->GetActorLocation(), nullptr))
    {
        return false;
    }
    Defense->RewardAttack(AttackId);
    return true;
}

void UPerfectDodgeComponent::ReleaseMovement()
{
    const FGuid PreviousLease = MovementLease;
    MovementLease.Invalidate();
    if (MovementOwner)
    {
        MovementOwner->Release(this, PreviousLease);
    }
}

void UPerfectDodgeComponent::Finish(FGuid Execution, bool bCancelled)
{
    if (!Execution.IsValid() || ExecutionId != Execution)
    {
        return;
    }
    // 先关闭窗口；恢复移动可同步触发其它技能，旧执行不能再参与伤害或释放新租约。
    ExecutionId.Invalidate();
    LastFinishedId = Execution;
    bLastCancelled = bCancelled;
    bBegun = false;
    bWindowsClosed = true;
    if (DodgeVisual)
    {
        DodgeVisual->DestroyComponent();
        DodgeVisual = nullptr;
    }
    ExecutionOwner.Reset();
    if (auto* ASC = BlockedASC.Get())
    {
        BlockedASC.Reset();
        ASC->UnBlockAbilitiesWithTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Abilities"))));
    }
    if (bCancelled && LastPerfectId == Execution)
    {
        if (auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>())
        {
            Time->Release(RewardLease);
        }
        RewardLease.Invalidate();
        if (PerfectVisual)
        {
            PerfectVisual->DestroyComponent();
            PerfectVisual = nullptr;
        }
    }
    ReleaseMovement();
    OnFinished.Broadcast(Execution, bCancelled);
}

void UPerfectDodgeComponent::InvalidateLife()
{
    if (bInvalidatingLife)
    {
        return;
    }
    bInvalidatingLife = true;
    ++LifeGeneration;
    Finish(ExecutionId, true);
    if (auto* Time = GetWorld() ? GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>() : nullptr)
    {
        Time->Release(RewardLease);
    }
    RewardLease.Invalidate();
    if (PerfectVisual)
    {
        PerfectVisual->DestroyComponent();
        PerfectVisual = nullptr;
    }
    LastPerfectId.Invalidate();
    RewardedAttacks.Reset();
    NextRewardTime = 0.;
    Source = {};
    bInvalidatingLife = false;
}

void UPerfectDodgeComponent::OnOwnerDeath(AActor*)
{
    InvalidateLife();
}

void UPerfectDodgeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>();
    if (PerfectVisual && (!Time || !Time->IsActive(RewardLease)))
    {
        PerfectVisual->DestroyComponent();
        PerfectVisual = nullptr;
    }
    AActor* Avatar = nullptr;
    if ((ExecutionId.IsValid() && !Current()) ||
        (RewardLease.IsValid() && !AbilityExecutionIdentity::ResolveSource(Source, Avatar)))
    {
        InvalidateLife();
        return;
    }
    if (!ExecutionId.IsValid() || !bBegun)
    {
        return;
    }
    if (Source.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Debuff_Stun) ||
        Source.ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Effects_HitReact))
    {
        Finish(ExecutionId, true);
        return;
    }
    const FGuid CurrentId = ExecutionId;
    const double Age = Now() - StartTime;
    if (MovementLease.IsValid())
    {
        AbilityMovementActions::GroundDash(Source, Direction,
                                           Settings.Distance *
                                               FMath::Clamp(static_cast<float>(Age / Settings.MoveDuration), 0.f, 1.f),
                                           OriginRoom, MoveState,
                                           [this, CurrentId]()
                                           {
                                               return IsRunning(CurrentId);
                                           });
        if (!IsRunning(CurrentId))
        {
            return;
        }
        if (MoveState.bBlocked || Age >= Settings.MoveDuration)
        {
            bWindowsClosed |= MoveState.bBlocked;
            ReleaseMovement();
        }
    }
    if (Age >= Settings.Recovery)
    {
        Finish(CurrentId, false);
    }
}

void UPerfectDodgeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    InvalidateLife();
    if (auto* Combat = Cast<ICombatInterface>(GetOwner()))
    {
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnOwnerDeath);
    }
    Super::EndPlay(Reason);
}
