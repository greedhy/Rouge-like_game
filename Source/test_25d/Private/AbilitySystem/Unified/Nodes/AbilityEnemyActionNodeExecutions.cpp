// 调用链：节点/原GA接触通知 -> 生命验证 -> 原投石弹体或Shaman角色Summon；GA仍只拥有自己的取消资源。
#include "AbilitySystem/Unified/Nodes/AbilityEnemyActionNodeExecutions.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "AbilitySystemComponent.h"
#include "Actor/EnemySlingProjectile.h"
#include "Character/ShamanEnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AEnemySlingProjectile* AbilityEnemyActions::FireSling(const FAbilitySourceToken& Source,
                                                      const USlingAttackConfig& Config, const FVector& Aim,
                                                      const FDamageEffectParams& Damage,
                                                      const TFunction<void(AEnemySlingProjectile*)>& OnPrepared)
{
    AActor* Avatar = nullptr;
    if (!Config.ValidateProjectile() || Aim.ContainsNaN() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return nullptr;
    }
    auto* Enemy = Cast<AEnemyCharacter>(Avatar);
    if (!Enemy)
    {
        return nullptr;
    }
    const FVector Start = Enemy->GetMesh()->GetSocketLocation(Config.MuzzleSocket);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SlingMuzzle), false, Enemy);
    FHitResult Hit;
    if (Source.World->LineTraceSingleByObjectType(Hit, Enemy->GetActorLocation(), Start,
                                                  FCollisionObjectQueryParams(ECC_WorldStatic), Query))
    {
        return nullptr;
    }
    const FTransform Transform((Aim - Start).Rotation(), Start);
    auto* Shot = Source.World->SpawnActorDeferred<AEnemySlingProjectile>(
        Config.ProjectileClass, Transform, Enemy, Enemy, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Shot)
    {
        return nullptr;
    }
    Shot->InitializeSource(Enemy);
    Shot->SetExecutionSourceToken(Source);
    Shot->DamageEffectParams = Damage;
    Shot->DamageEffectParams.SourceAbilitySystemComponent = Source.ASC.Get();
    Shot->DamageEffectParams.DebuffChance = 0.f;
    Shot->DamageEffectParams.bIsRadialDamage = false;
    Shot->ProjectileMovement->InitialSpeed = Config.Speed;
    Shot->ProjectileMovement->MaxSpeed = Config.Speed;
    Shot->ProjectileMovement->bInitialVelocityInLocalSpace = false;
    Shot->ProjectileMovement->Velocity = Transform.GetRotation().GetForwardVector() * Config.Speed;
    Shot->Sphere->IgnoreActorWhenMoving(Enemy, true);
    if (OnPrepared)
    {
        // 原GA在FinishSpawning前持有Launched，保留BeginPlay同步取消时可销毁弹体的顺序。
        OnPrepared(Shot);
    }
    if (!IsValid(Shot))
    {
        return nullptr;
    }
    UGameplayStatics::FinishSpawningActor(Shot, Transform);
    if (!IsValid(Shot))
    {
        return nullptr;
    }
    Shot->SetLifeSpan(Config.Lifetime);
    return Shot;
}

int32 AbilityEnemyActions::SummonShamanMinions(const FAbilitySourceToken& Source, const UShamanSummonConfig& Config)
{
    AActor* Avatar = nullptr;
    if (!Config.ValidateSummon() || !AbilityExecutionIdentity::ResolveSource(Source, Avatar))
    {
        return 0;
    }
    auto* Shaman = Cast<AShamanEnemyCharacter>(Avatar);
    return Shaman ? Shaman->Summon(&Config, Source.EnemyLease.Get(0)) : 0;
}

bool UAbilityEnemySlingNodeConfig::Validate(FString& Error) const
{
    Error = Settings && Settings->ValidateProjectile()
                ? FString()
                : TEXT("Enemy sling requires valid original projectile settings.");
    return Error.IsEmpty();
}
bool UAbilityShamanSummonNodeConfig::Validate(FString& Error) const
{
    Error = Settings && Settings->ValidateSummon() ? FString()
                                                   : TEXT("Shaman summon requires valid original minion settings.");
    return Error.IsEmpty();
}

bool UAbilityEnemySlingNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                            const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                            FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityEnemySlingNodeConfig>(&Config);
    FVector Aim = Input.EventCenter;
    if (Input.LiveTargetToken.IsSet() &&
        !AbilityExecutionIdentity::ResolveTarget(Input.SourceToken, Input.LiveTargetToken.GetValue(), Aim))
    {
        Error = TEXT("Sling target life is no longer current.");
        return false;
    }
    Projectile = AbilityEnemyActions::FireSling(Input.SourceToken, *Settings.Settings, Aim, Input.DamageParams);
    if (bStopped || Owner.IsFinished())
    {
        if (Projectile.IsValid())
        {
            Projectile->Destroy();
        }
        Projectile.Reset();
        return true;
    }
    if (!Projectile.IsValid())
    {
        Error = TEXT("Sling source or muzzle spawn is invalid.");
        return false;
    }
    Projectile->OnDestroyed.AddDynamic(this, &ThisClass::ProjectileDestroyed);
    return Owner.Publish(Id, TEXT("Spawned"), Input);
}
void UAbilityEnemySlingNodeExecution::ProjectileDestroyed(AActor* Actor)
{
    Projectile.Reset();
    if (!bStopped)
    {
        if (auto* Owner = GraphOwner.Get())
        {
            Owner->Publish(InvocationId, TEXT("Completed"), Input);
        }
    }
}
void UAbilityEnemySlingNodeExecution::Stop(EAbilityGraphEndReason Reason)
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
bool UAbilityShamanSummonNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                              const UAbilityNodeConfig& Config, const FAbilityNodeInput& InInput,
                                              FString& Error)
{
    if (!Super::Start(Owner, Id, Config, InInput, Error))
    {
        return false;
    }
    const auto& Settings = *CastChecked<UAbilityShamanSummonNodeConfig>(&Config);
    AActor* Avatar = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Input.SourceToken, Avatar) || !Cast<AShamanEnemyCharacter>(Avatar))
    {
        Error = TEXT("Shaman summon requires the existing Shaman ownership host.");
        return false;
    }
    AbilityEnemyActions::SummonShamanMinions(Input.SourceToken, *Settings.Settings);
    return Owner.Publish(Id, TEXT("Completed"), Input);
}
void AbilityEnemyActionNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Sling;
    Sling.TypeId = TEXT("Delivery.EnemySling");
    Sling.ConfigClass = UAbilityEnemySlingNodeConfig::StaticClass();
    Sling.ExecutionClass = UAbilityEnemySlingNodeExecution::StaticClass();
    Sling.InputPayload = EAbilityPortPayload::Origin;
    Sling.bSupportsActorTargets = Sling.bSupportsSnapshotTargets = true;
    Sling.bIsAsync = true;
    Sling.Ports = {{TEXT("Spawned"), EAbilityPortPayload::Origin, false},
                   {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    auto Summon = Sling;
    Summon.TypeId = TEXT("Spawn.ShamanMinions");
    Summon.ConfigClass = UAbilityShamanSummonNodeConfig::StaticClass();
    Summon.ExecutionClass = UAbilityShamanSummonNodeExecution::StaticClass();
    Summon.bIsAsync = false;
    Summon.Ports = {{TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Sling, Summon})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Enemy action registration: %s"), *Error);
        }
    }
}
