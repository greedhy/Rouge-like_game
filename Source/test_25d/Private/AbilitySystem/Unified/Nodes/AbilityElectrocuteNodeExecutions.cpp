// 调用链：Start 捕获生命/连锁目标 -> 世界每帧至多一次 Period -> 完成或 Stop -> 解除委托、Cue、移动占用。
#include "AbilitySystem/Unified/Nodes/AbilityElectrocuteNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayCueFunctionLibrary.h"
#include "Interface/CombatInterface.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "Roguelike/DungeonRunDirector.h"

namespace
{
    bool SameRun(UWorld* World, const TOptional<FGuid>& Expected)
    {
        if (const auto* Director = ADungeonRunDirector::Find(World))
        {
            return Expected.IsSet() && Expected.GetValue() == Director->RunId;
        }
        return !Expected.IsSet();
    }
    /** 清理允许死亡，但拒绝池中下一生命；ResolveTarget 的存活条件不适合死亡委托解除。 */
    bool SameActorLife(const FAbilityTargetToken& Life)
    {
        AActor* Actor = Life.Target.Actor.Get();
        if (!IsValid(Actor) || Actor->GetWorld() != Life.World.Get() || !SameRun(Actor->GetWorld(), Life.DungeonRunId))
        {
            return false;
        }
        if (const auto* Enemy = Cast<AEnemyCharacter>(Actor))
        {
            return !Enemy->IsDungeonPoolSleeping() &&
                   (Life.ActorLease.IsSet() ? Enemy->GetDungeonPoolLease() == Life.ActorLease.GetValue()
                                            : Enemy->GetDungeonPoolLease() == 0);
        }
        return !Life.ActorLease.IsSet();
    }
} // namespace

void UAbilityElectrocuteContext::RequestCompletion()
{
    if (auto* Active = Execution.Get())
    {
        Active->CompleteChannel();
    }
}

bool UAbilityElectrocuteNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!FMath::IsFinite(DamageInterval) || DamageInterval <= 0.f || !FMath::IsFinite(SecondaryRadius) ||
        SecondaryRadius <= 0.f || MaxAdditionalTargets < 0 || MaxAdditionalTargets > 64)
    {
        Error = TEXT("Electrocute requires finite positive interval/radius and 0..64 additional targets.");
    }
    return Error.IsEmpty();
}

bool UAbilityElectrocuteNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                             const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                             FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    Settings =
        DuplicateObject<UAbilityElectrocuteNodeConfig>(CastChecked<UAbilityElectrocuteNodeConfig>(&Config), this);
    Binding = Cast<UAbilityElectrocuteContext>(Input.Context.Get());
    AActor* Avatar = nullptr;
    if (!Settings->Validate(Error) || !AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        return false;
    }
    FVector CapturedLocation;
    if (Input.LiveTargetToken.IsSet() &&
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *Input.LiveTargetToken, CapturedLocation))
    {
        Error = TEXT("Electrocute primary target belongs to an expired life.");
        return false;
    }
    TimerWorld = Avatar->GetWorld();
    if (Binding)
    {
        Binding->Execution = this;
    }
    AActor* Target = Input.LiveTargetToken.IsSet() ? Input.LiveTargetToken->Target.Actor.Get()
                     : Binding                     ? Binding->AimActor.Get()
                                                   : nullptr;
    FVector Location = Input.Origin;
    USkeletalMeshComponent* Weapon =
        Avatar->Implements<UCombatInterface>() ? ICombatInterface::Execute_GetWeapon(Avatar) : nullptr;
    if (Weapon && Settings->bTraceFromWeapon)
    {
        FHitResult Hit;
        UKismetSystemLibrary::SphereTraceSingle(Avatar, Weapon->GetSocketLocation(TEXT("TipSocket")), Location, 10.f,
                                                TraceTypeQuery1, false, {Avatar}, EDrawDebugTrace::None, Hit, true);
        if (Hit.bBlockingHit)
        {
            Target = Hit.GetActor();
            Location = Hit.ImpactPoint;
        }
    }
    PrimaryActor = Target;
    if (Target && Target->Implements<UCombatInterface>() && AddTarget(Target, Weapon, Location))
    {
        if (bStopped || !IsValid(Target))
        {
            return true;
        }
        TArray<AActor*> Candidates;
        TArray<AActor*> Closest;
        UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Avatar, Candidates, {Avatar, Target},
                                                            Settings->SecondaryRadius, Target->GetActorLocation());
        UMyAbilitySystemLibrary::GetClosestTargets(
            FMath::Min(FMath::FloorToInt(Input.Level) + 1, Settings->MaxAdditionalTargets), Candidates, Closest,
            Target->GetActorLocation());
        for (AActor* Additional : Closest)
        {
            if (bStopped)
            {
                return true;
            }
            AddTarget(Additional, Target->GetRootComponent(), Additional->GetActorLocation());
        }
    }
    else
    {
        // 空地束仍显示施法者到命中位置的连线；它不授予一个伪造的敌对活目标令牌。
        PrimaryActor.Reset();
        EmptyTargetCue.SourceObject = Target;
        EmptyTargetCue.Location = Location;
        EmptyTargetCue.TargetAttachComponent = Weapon;
        if (Settings->LoopCue.IsValid())
        {
            bEmptyTargetCue = true;
            UGameplayCueFunctionLibrary::AddGameplayCueOnActor(Avatar, Settings->LoopCue, EmptyTargetCue);
        }
    }
    if (bStopped)
    {
        return true;
    }
    bSourcePresentationStarted = true;
    if (Avatar->Implements<UCombatInterface>())
    {
        ICombatInterface::Execute_SetInShockLoop(Avatar, true);
    }
    if (bStopped)
    {
        return true;
    }
    if (auto* Character = Cast<ACharacter>(Avatar))
    {
        Character->GetCharacterMovement()->DisableMovement();
    }
    if (bStopped)
    {
        return true;
    }
    FTimerManagerTimerParameters Timer;
    Timer.bLoop = true;
    Timer.bMaxOncePerFrame = true;
    Timer.FirstDelay = Settings->DamageInterval;
    TimerWorld->GetTimerManager().SetTimer(PeriodTimer, this, &ThisClass::Period, Settings->DamageInterval, Timer);
    return true;
}

bool UAbilityElectrocuteNodeExecution::AddTarget(AActor* Actor, USceneComponent* Attach, FVector Location)
{
    FAbilityElectrocuteTarget Record;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget::FromActor(Actor),
                                                 Record.Life, Error))
    {
        return false;
    }
    Record.Cue.SourceObject = Actor;
    Record.Cue.Location = Location;
    Record.Cue.TargetAttachComponent = Attach;
    const int32 Index = Targets.Add(Record);
    if (auto* Combat = Cast<ICombatInterface>(Actor))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::TargetDied);
        ICombatInterface::Execute_SetIsBeingShocked(Actor, true);
    }
    if (!bStopped && Settings->LoopCue.IsValid())
    {
        Targets[Index].bCueAdded = true;
        UGameplayCueFunctionLibrary::AddGameplayCueOnActor(Actor, Settings->LoopCue, Record.Cue);
    }
    return true;
}

void UAbilityElectrocuteNodeExecution::Period()
{
    auto* Owner = GraphOwner.Get();
    if (bStopped || bCompleting || !Owner)
    {
        return;
    }
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        Owner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    if (Binding && Binding->CommitPeriodCost && !Binding->CommitPeriodCost())
    {
        CompleteChannel();
        return;
    }
    ++PeriodSequence;
    for (auto& Target : Targets)
    {
        if (bStopped || bCompleting)
        {
            return;
        }
        FVector Location;
        if (Target.bReleased || !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target.Life, Location))
        {
            if (!Target.bReleased)
            {
                const bool bPrimaryLost = Target.Life.Target.Actor == PrimaryActor;
                ReleaseTarget(Target, false);
                if (bPrimaryLost)
                {
                    CompleteChannel();
                    return;
                }
            }
            continue;
        }
        ApplyPeriodDamage(Target.Life.Target.Actor.Get(), Target.Life, Location);
    }
    if (!bStopped && !bCompleting && !PrimaryActor.IsValid() && Targets.IsEmpty())
    {
        // 原图空地分支把 CueTarget 设为 Avatar，并仍调用 ApplyDamageToActor；保留该实际行为。
        FAbilityTargetToken SelfSnapshot;
        SelfSnapshot.World = Input.SourceToken.World;
        SelfSnapshot.Target = FPureMassCombatTarget::FromActor(Avatar);
        SelfSnapshot.ActorLease = Input.SourceToken.EnemyLease;
        SelfSnapshot.DungeonRunId = Input.SourceToken.DungeonRunId;
        ApplyPeriodDamage(Avatar, SelfSnapshot, Avatar->GetActorLocation());
    }
    if (!bStopped && !bCompleting)
    {
        Owner->Publish(InvocationId, TEXT("PeriodElapsed"), Input);
    }
}

void UAbilityElectrocuteNodeExecution::ApplyPeriodDamage(AActor* Actor, const FAbilityTargetToken& Life,
                                                         FVector Location)
{
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        if (GraphOwner.IsValid())
        {
            GraphOwner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        }
        return;
    }
    auto* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
    if (!TargetASC || !Input.DamageParams.DamageGameplayEffectClass)
    {
        return;
    }
    // 保留原 ApplyDamageToActor：目标 ASC 创建上下文及 Spec，固定 Damage.Thunder，不套用最终命中的额外参数。
    auto Spec = TargetASC->MakeOutgoingSpec(Input.DamageParams.DamageGameplayEffectClass, Input.Level,
                                            TargetASC->MakeEffectContext());
    if (!Spec.IsValid())
    {
        return;
    }
    Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.Thunder")),
                                       Input.DamageParams.BaseDamage);
    TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    if (bStopped || bCompleting || !GraphOwner.IsValid())
    {
        return;
    }
    FAbilityNodeInput Payload = Input;
    Payload.Origin = Location;
    Payload.LiveTargetToken.Reset();
    Payload.Impact = FAbilityImpactSnapshot{Location, Input.Direction, Life, PeriodSequence};
    Payload.bHasDamageResult = true;
    Payload.bDamageAttempted = true;
    Payload.bKilled =
        IsValid(Actor) && Actor->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Actor);
    GraphOwner->Publish(InvocationId, TEXT("DamageResolved"), Payload);
}

void UAbilityElectrocuteNodeExecution::TargetDied(AActor* Actor)
{
    if (bStopped || bCompleting)
    {
        return;
    }
    const bool bPrimary = PrimaryActor.Get() == Actor;
    for (auto& Target : Targets)
    {
        if (Target.Life.Target.Actor.Get() == Actor)
        {
            ReleaseTarget(Target, false);
        }
    }
    if (bPrimary)
    {
        CompleteChannel();
    }
}

bool UAbilityElectrocuteNodeExecution::SourceStillCurrent() const
{
    AActor* Avatar = Input.SourceToken.Avatar.Get();
    if (!IsValid(Avatar) || !Input.SourceToken.ASC.IsValid() || Input.SourceToken.ASC->GetAvatarActor() != Avatar ||
        !SameRun(Avatar->GetWorld(), Input.SourceToken.DungeonRunId))
    {
        return false;
    }
    if (auto* Enemy = Cast<AEnemyCharacter>(Avatar))
    {
        return !Enemy->IsDungeonPoolSleeping() &&
               (Input.SourceToken.EnemyLease.IsSet() ? Enemy->GetDungeonPoolLease() == *Input.SourceToken.EnemyLease
                                                     : Enemy->GetDungeonPoolLease() == 0);
    }
    return true;
}

void UAbilityElectrocuteNodeExecution::ReleaseTarget(FAbilityElectrocuteTarget& Target, bool bFinalDamage)
{
    if (Target.bReleased)
    {
        return;
    }
    Target.bReleased = true;
    AActor* Actor = Target.Life.Target.Actor.Get();
    if (auto* Combat = Cast<ICombatInterface>(Actor))
    {
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::TargetDied);
    }
    if (!SameActorLife(Target.Life))
    {
        return;
    }
    if (Actor->Implements<UCombatInterface>())
    {
        ICombatInterface::Execute_SetIsBeingShocked(Actor, false);
    }
    if (Target.bCueAdded && Settings->LoopCue.IsValid())
    {
        UGameplayCueFunctionLibrary::RemoveGameplayCueOnActor(Actor, Settings->LoopCue, Target.Cue);
    }
    if (bFinalDamage && !bStopped)
    {
        AActor* Avatar = nullptr;
        FVector TargetLocation;
        if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) ||
            !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Target.Life, TargetLocation))
        {
            return;
        }
        auto Params = Input.DamageParams;
        const FVector Direction = (TargetLocation - Avatar->GetActorLocation()).GetSafeNormal();
        Params.DeathImpulse = Direction * Params.DeathImpulseMagnitude;
        Params.KnockbackForce = Direction * Params.KnockbackForceMagnitude;
        AbilityDamageService::Apply(Input.SourceToken, Target.Life, Params);
    }
}

void UAbilityElectrocuteNodeExecution::Cleanup(bool bFinalDamage)
{
    if (bCleaned)
    {
        return;
    }
    bCleaned = true;
    if (TimerWorld.IsValid())
    {
        TimerWorld->GetTimerManager().ClearTimer(PeriodTimer);
    }
    if (Binding)
    {
        Binding->Execution.Reset();
    }
    AActor* Avatar = Input.SourceToken.Avatar.Get();
    if (SourceStillCurrent())
    {
        if (bSourcePresentationStarted && Avatar->Implements<UCombatInterface>())
        {
            ICombatInterface::Execute_SetInShockLoop(Avatar, false);
        }
        if (auto* Character = Cast<ACharacter>(Avatar); bSourcePresentationStarted && Character)
        {
            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        }
        if (bEmptyTargetCue && Settings->LoopCue.IsValid())
        {
            UGameplayCueFunctionLibrary::RemoveGameplayCueOnActor(Avatar, Settings->LoopCue, EmptyTargetCue);
        }
    }
    for (auto& Target : Targets)
    {
        ReleaseTarget(Target, bFinalDamage && !bStopped);
    }
    AActor* ValidSource = nullptr;
    if (bFinalDamage && Binding && Binding->CommitEndCooldown &&
        AbilityExecutionIdentity::ResolveSource(Input.SourceToken, ValidSource))
    {
        Binding->CommitEndCooldown();
    }
}

void UAbilityElectrocuteNodeExecution::CompleteChannel()
{
    if (bStopped || bCompleting || !GraphOwner.IsValid())
    {
        return;
    }
    bCompleting = true;
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        GraphOwner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return;
    }
    Cleanup(true);
    if (!bStopped && GraphOwner.IsValid())
    {
        GraphOwner->Publish(InvocationId, TEXT("Completed"), Input);
    }
}

void UAbilityElectrocuteNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    Super::Stop(Reason);
    Cleanup(false);
    Binding = nullptr;
    // 回调可在施加 GE 时同步取消；保留数组直至对象回收，避免外层 Period/Cleanup 的迭代引用失效。
}

void AbilityElectrocuteNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Channel.Electrocute")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Channel.Electrocute");
    Descriptor.ConfigClass = UAbilityElectrocuteNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityElectrocuteNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    Descriptor.Ports = {{TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                        {TEXT("PeriodElapsed"), EAbilityPortPayload::Origin, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                        {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                        {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Electrocute registration failed: %s"), *Error);
}
