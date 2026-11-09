// 调用链：Start订阅真实ASC攻击 -> 定时补充/扣费/逐个发射 -> 原投射物伤害 -> Stop释放本次全部句柄和Actor。
#include "AbilitySystem/Unified/Nodes/AbilityOrbitNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/Nodes/AbilityTraditionalNodeExecutions.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Interface/CombatInterface.h"
#include "GameplayEffect.h"
#include "Engine/World.h"

bool UAbilityOrbitNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    if (!OrbitSettings || !OrbitSettings->IsValidConfiguration())
    {
        Error = TEXT("Orbit requires a valid configured bolt class, capacity, intervals and mana costs.");
    }
    return Error.IsEmpty();
}

bool UAbilityOrbitNodeExecution::SpendMana(UAbilitySystemComponent* ASC, AActor* Avatar, float Level, float Amount)
{
    if (!ASC || !IsValid(Avatar) || !Avatar->HasAuthority() || !FMath::IsFinite(Amount) || Amount < 0.f ||
        ASC->GetAvatarActor() != Avatar || !ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetManaAttribute()) ||
        ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()) < Amount)
    {
        return false;
    }
    if (Amount == 0.f)
    {
        return true;
    }
    auto* Cost = NewObject<UGameplayEffect>(GetTransientPackage());
    Cost->DurationPolicy = EGameplayEffectDurationType::Instant;
    auto& Modifier = Cost->Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UMyAttributeSet::GetManaAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-Amount);
    return ASC->ApplyGameplayEffectToSelf(Cost, Level, ASC->MakeEffectContext()).WasSuccessfullyApplied();
}

bool UAbilityOrbitNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id, const UAbilityNodeConfig& Config,
                                       const FAbilityNodeInput& InInput, FString& Error)
{
    Super::Start(Owner, Id, Config, InInput, Error);
    const auto* Orbit = CastChecked<UAbilityOrbitNodeConfig>(&Config);
    if (!Orbit->Validate(Error) || !ValidateSource())
    {
        return false;
    }
    auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get());
    AActor* Avatar = Input.SourceToken.Avatar.Get();
    if (!ASC || !Avatar->Implements<UCombatInterface>())
    {
        Error = TEXT("Orbit requires the source combat interface and offensive attack ASC.");
        return false;
    }
    Settings = DuplicateObject<UOrbitingFireBoltConfig>(Orbit->OrbitSettings, this);
    Binding = Cast<UAbilityOrbitContext>(Input.Context.Get());
    if (Binding)
    {
        Binding->Execution = this;
    }
    LifeContext = NewObject<UPrototypeCastContext>(this);
    LifeContext->Caster = Avatar;
    LifeContext->DamageParams = Input.DamageParams;
    if (!LifeContext->CaptureSourceLife(Error))
    {
        return false;
    }
    AttackHandle = ASC->OffensiveAttackTargeted.AddUObject(this, &ThisClass::HandleAttack);
    DeactivateHandle = ASC->DeactivatePassiveAbility.AddUObject(this, &ThisClass::HandleDeactivate);
    CastChecked<ICombatInterface>(Avatar)->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::OnOwnerDied);
    Avatar->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnOwnerDestroyed);
    auto& Timers = Input.SourceToken.World->GetTimerManager();
    Timers.SetTimer(SpawnTimer, this, &ThisClass::Replenish, Settings->SpawnInterval, true);
    Timers.SetTimer(ManaTimer, this, &ThisClass::PayUpkeep, 1.f, true);
    return true;
}

bool UAbilityOrbitNodeExecution::ValidateSource()
{
    AActor* Avatar = nullptr;
    if (bStopped || !GraphOwner.IsValid())
    {
        return false;
    }
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar))
    {
        GraphOwner->Cancel(EAbilityGraphEndReason::SourceInvalid);
        return false;
    }
    return true;
}

bool UAbilityOrbitNodeExecution::IsValidEnemy(AActor* Target) const
{
    AActor* Avatar = Input.SourceToken.Avatar.Get();
    return IsValid(Avatar) && IsValid(Target) && Target != Avatar && Settings &&
           Target->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Target) &&
           UMyAbilitySystemLibrary::IsNotFriend(Avatar, Target) &&
           UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target) &&
           FVector::DistSquared(Avatar->GetActorLocation(), Target->GetActorLocation()) <=
               FMath::Square(Settings->MaxTargetDistance);
}

void UAbilityOrbitNodeExecution::HandleAttack(UGameplayAbility* Ability, AActor* Target)
{
    if (!ValidateSource() || !Ability || (Binding && Binding->SourceAbility.Get() == Ability) ||
        !IsValidEnemy(Target) ||
        Ability->GetAssetTags().HasTag(FGameplayTag::RequestGameplayTag(TEXT("Abilities.Passive"))))
    {
        return;
    }
    FAbilityTargetToken Captured;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureTarget(Input.SourceToken, FPureMassCombatTarget::FromActor(Target), Captured,
                                                 Error))
    {
        return;
    }
    FVector Location;
    if (!AttackTargetLife.IsSet() || AttackTargetLife->Target.Actor.Get() != Target ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *AttackTargetLife, Location))
    {
        ClearTarget();
        AttackTargetLife = Captured;
        // 新攻击信号是新的目标选择；它可以选择同Actor的下一生命，旧飞行弹在ClearTarget中已撤回。
        LifeContext = NewObject<UPrototypeCastContext>(this);
        LifeContext->Caster = Input.SourceToken.Avatar;
        LifeContext->DamageParams = Input.DamageParams;
        if (!LifeContext->CaptureSourceLife(Error) ||
            !LifeContext->ResolveTargetLife(Captured.Target, LastTargetLocation))
        {
            ClearTarget();
            return;
        }
        for (auto& Bolt : Bolts)
        {
            if (IsValid(Bolt))
            {
                Bolt->SetCastContext(LifeContext);
            }
        }
        CastChecked<ICombatInterface>(Target)->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::OnTargetDied);
    }
    auto& Timers = Input.SourceToken.World->GetTimerManager();
    if (!Timers.IsTimerActive(LaunchTimer))
    {
        Timers.SetTimer(LaunchTimer, this, &ThisClass::LaunchNext, Settings->LaunchInterval, true, .001f);
    }
}

void UAbilityOrbitNodeExecution::Replenish()
{
    if (!ValidateSource())
    {
        return;
    }
    Bolts.RemoveAll(
        [](const auto& Bolt)
        {
            return !IsValid(Bolt);
        });
    if (Bolts.Num() >= Settings->MaxFireBolts)
    {
        return;
    }
    AActor* Avatar = Input.SourceToken.Avatar.Get();
    const FTransform Transform(FRotator::ZeroRotator,
                               Avatar->GetActorLocation() + FVector(Settings->OrbitRadius, 0.f, Settings->OrbitHeight));
    auto* Bolt = Cast<AMyOrbitingFireBolt>(AbilityTraditionalActions::CreateProjectileDeferred(
        Input.SourceToken, Input.DamageParams, Settings->FireBoltClass, Transform));
    if (!Bolt)
    {
        return;
    }
    Bolt->SetCastContext(LifeContext);
    Bolt->InitializeOrbit(Avatar, Settings, 0.f);
    Bolts.Add(Bolt);
    Bolt->DamageApplied.AddUObject(this, &ThisClass::BoltHit);
    Bolt->FinishSpawning(Transform);
    if (bStopped)
    {
        if (IsValid(Bolt))
        {
            Bolt->Destroy();
        }
        return;
    }
    ArrangeOrbit();
    GraphOwner->Publish(InvocationId, TEXT("Replenished"), Input);
}

void UAbilityOrbitNodeExecution::PayUpkeep()
{
    if (ValidateSource() &&
        !SpendMana(Input.SourceToken.ASC.Get(), Input.SourceToken.Avatar.Get(), Input.Level, Settings->ManaPerSecond) &&
        !bStopped)
    {
        GraphOwner->Cancel();
    }
}

void UAbilityOrbitNodeExecution::ArrangeOrbit()
{
    int32 Index = 0;
    const int32 Count = GetLiveCount() - GetLaunchedCount();
    for (const auto& Bolt : Bolts)
    {
        if (IsValid(Bolt) && !Bolt->IsLaunched())
        {
            Bolt->SetOrbitPhase(360.f * Index++ / FMath::Max(1, Count));
        }
    }
}

void UAbilityOrbitNodeExecution::LaunchNext()
{
    if (!ValidateSource())
    {
        return;
    }
    if (!AttackTargetLife.IsSet() || !IsValidEnemy(GetAttackTarget()) ||
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, *AttackTargetLife, LastTargetLocation))
    {
        ClearTarget();
        return;
    }
    for (const auto& Bolt : Bolts)
    {
        if (IsValid(Bolt) && !Bolt->IsLaunched())
        {
            Bolt->LaunchAt(GetAttackTarget());
            ArrangeOrbit();
            auto Payload = Input;
            Payload.LiveTargetToken = AttackTargetLife;
            Payload.Origin = LastTargetLocation;
            GraphOwner->Publish(InvocationId, TEXT("Launched"), Payload);
            break;
        }
    }
}

void UAbilityOrbitNodeExecution::BoltHit(AActor* Target)
{
    if (!ValidateSource() || !LifeContext)
    {
        return;
    }
    const auto* Life = LifeContext->FindTargetLife(FPureMassCombatTarget::FromActor(Target));
    if (!Life)
    {
        return;
    }
    auto Payload = Input;
    Payload.LiveTargetToken.Reset();
    Payload.Impact = FAbilityImpactSnapshot{LastTargetLocation, Input.Direction, *Life, ++HitSequence};
    GraphOwner->Publish(InvocationId, TEXT("OnHit"), Payload);
}

void UAbilityOrbitNodeExecution::ClearTarget()
{
    if (auto* Combat = Cast<ICombatInterface>(GetAttackTarget()))
    {
        Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnTargetDied);
    }
    AttackTargetLife.Reset();
    if (Input.SourceToken.World.IsValid())
    {
        Input.SourceToken.World->GetTimerManager().ClearTimer(LaunchTimer);
    }
    for (const auto& Bolt : Bolts)
    {
        if (IsValid(Bolt))
        {
            Bolt->ReturnToOrbit();
        }
    }
    ArrangeOrbit();
}

void UAbilityOrbitNodeExecution::OnTargetDied(AActor* Target)
{
    if (Target == GetAttackTarget())
    {
        ClearTarget();
    }
}

void UAbilityOrbitNodeExecution::OnOwnerDied(AActor*)
{
    if (!bStopped && GraphOwner.IsValid())
    {
        GraphOwner->Cancel(EAbilityGraphEndReason::SourceInvalid);
    }
}

void UAbilityOrbitNodeExecution::OnOwnerDestroyed(AActor* Avatar)
{
    OnOwnerDied(Avatar);
}

void UAbilityOrbitNodeExecution::HandleDeactivate(const FGameplayTag& Tag)
{
    if (ValidateSource() && Binding && Binding->AbilityTags.HasTagExact(Tag))
    {
        auto* Graph = GraphOwner.Get();
        Graph->Publish(InvocationId, TEXT("Completed"), Input);
        // 装备切换的同步调用者依赖立即结束；发布之后只排空图队列，不递归进入策略。
        Graph->Advance(0.f);
    }
}

void UAbilityOrbitNodeExecution::Stop(EAbilityGraphEndReason Reason)
{
    if (bStopped)
    {
        return;
    }
    Super::Stop(Reason);
    if (LifeContext)
    {
        LifeContext->bCancelled = true;
    }
    if (Binding)
    {
        Binding->Execution.Reset();
    }
    if (Input.SourceToken.World.IsValid())
    {
        auto& Timers = Input.SourceToken.World->GetTimerManager();
        Timers.ClearTimer(SpawnTimer);
        Timers.ClearTimer(ManaTimer);
    }
    ClearTarget();
    if (auto* ASC = Cast<UMyAbilitySystemComponent>(Input.SourceToken.ASC.Get()))
    {
        ASC->OffensiveAttackTargeted.Remove(AttackHandle);
        ASC->DeactivatePassiveAbility.Remove(DeactivateHandle);
    }
    if (auto* Avatar = Input.SourceToken.Avatar.Get())
    {
        if (auto* Combat = Cast<ICombatInterface>(Avatar))
        {
            Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::OnOwnerDied);
        }
        Avatar->OnDestroyed.RemoveDynamic(this, &ThisClass::OnOwnerDestroyed);
    }
    const auto Owned = Bolts;
    Bolts.Reset();
    for (AMyOrbitingFireBolt* Bolt : Owned)
    {
        if (IsValid(Bolt))
        {
            Bolt->DamageApplied.RemoveAll(this);
            Bolt->Destroy();
        }
    }
}

int32 UAbilityOrbitNodeExecution::GetLiveCount() const
{
    int32 Count = 0;
    for (const auto& Bolt : Bolts)
    {
        if (IsValid(Bolt))
        {
            ++Count;
        }
    }
    return Count;
}

int32 UAbilityOrbitNodeExecution::GetLaunchedCount() const
{
    int32 Count = 0;
    for (const auto& Bolt : Bolts)
    {
        if (IsValid(Bolt) && Bolt->IsLaunched())
        {
            ++Count;
        }
    }
    return Count;
}

AActor* UAbilityOrbitNodeExecution::GetAttackTarget() const
{
    return AttackTargetLife.IsSet() ? AttackTargetLife->Target.Actor.Get() : nullptr;
}

void AbilityOrbitNodes::Register(FAbilityNodeRegistry& Registry)
{
    if (Registry.Find(TEXT("Channel.OrbitingFireBolts")))
    {
        return;
    }
    FAbilityNodeDescriptor Descriptor;
    Descriptor.TypeId = TEXT("Channel.OrbitingFireBolts");
    Descriptor.ConfigClass = UAbilityOrbitNodeConfig::StaticClass();
    Descriptor.ExecutionClass = UAbilityOrbitNodeExecution::StaticClass();
    Descriptor.InputPayload = EAbilityPortPayload::Origin;
    Descriptor.bSupportsActorTargets = Descriptor.bSupportsSnapshotTargets = true;
    Descriptor.bIsAsync = true;
    Descriptor.Ports = {{TEXT("Replenished"), EAbilityPortPayload::Origin, false},
                        {TEXT("Launched"), EAbilityPortPayload::LiveTarget, false},
                        {TEXT("OnHit"), EAbilityPortPayload::ConfirmedImpact, false},
                        {TEXT("Completed"), EAbilityPortPayload::Origin, true},
                        {TEXT("Cancelled"), EAbilityPortPayload::Origin, true},
                        {TEXT("Failed"), EAbilityPortPayload::Origin, true}};
    FString Error;
    ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Orbit registration failed: %s"), *Error);
}
