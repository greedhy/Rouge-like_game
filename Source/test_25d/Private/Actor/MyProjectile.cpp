// 调用链：共享动作捕获来源生命 -> Sphere接触弹反/闪避裁决 -> 转移返程或原伤害 -> 命中/生命终结清理。

#include "Actor/MyProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "test_25d/test_25d.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Defense/PerfectDodgeComponent.h"
#include "Interface/CombatInterface.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "Roguelike/DungeonRunDirector.h"
#include "GameFramework/Pawn.h"

AMyProjectile::AMyProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    //初始化碰撞体
    bReplicates = true;

    Sphere = CreateDefaultSubobject<USphereComponent>("Sphere");
    SetRootComponent(Sphere);                                             //设置其为根节点，
    Sphere->SetCollisionObjectType(ECC_Projectile);                       //设置其碰撞类型为Projectile
    Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);            //设置其只用作查询使用
    Sphere->SetCollisionResponseToChannels(ECR_Ignore);                   //设置其忽略所有碰撞检测
    Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap); //设置其与世界动态物体产生重叠事件
    Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);  //设置其与世界静态物体产生重叠事件
    Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);         //设置其与Pawn类型物体产生重叠事件

    //创建发射组件
    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovement");
    ProjectileMovement->InitialSpeed = 550.f;         //设置初始速度
    ProjectileMovement->MaxSpeed = 550.f;             //设置最大速度
    ProjectileMovement->ProjectileGravityScale = 0.f; //设置重力影响因子，0为不受影响
}
void AMyProjectile::BeginPlay()
{
    Super::BeginPlay();
    PreviousDodgeLocation = GetActorLocation();
    if (!DodgeAttackId.IsValid())
    {
        DodgeAttackId = FGuid::NewGuid();
    }
    SetLifeSpan(LifeSpan);
    SetReplicateMovement(true);
    Sphere->OnComponentBeginOverlap.AddDynamic(this, &AMyProjectile::OnSphereOverlap);
    LoopingSoundComponent = UGameplayStatics::SpawnSoundAttached(LoopingSound, GetRootComponent());
}

void AMyProjectile::OnHit()
{
    UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, GetActorLocation(),
                                          FRotator::ZeroRotator);                           //在当前Actor位置播放声音
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactEffect, GetActorLocation()); //在当前Actor位置生成特效
    if (LoopingSoundComponent)
    {
        LoopingSoundComponent->Stop();             //停止循环声音
        LoopingSoundComponent->DestroyComponent(); //销毁声音组件
    }
    bHit = true;
}

void AMyProjectile::Destroyed()
{
    if (ExecutionSource.IsSet())
    {
        if (auto* Combat = Cast<ICombatInterface>(ExecutionSource->Avatar.Get()))
        {
            Combat->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::DodgeSourceDied);
        }
    }
    if (!bHit && !HasAuthority() && ShouldPlayImpactOnDestroy())
    {
        OnHit();
    }
    if (LoopingSoundComponent)
    {
        LoopingSoundComponent->Stop();             //停止循环声音
        LoopingSoundComponent->DestroyComponent(); //销毁声音组件
    }
    Super::Destroyed();
}

void AMyProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                    const FHitResult& SweepResult)
{
    if (bFromSweep && InvalidatedParrySweep.IsSet() && SweepResult.TraceStart.Equals(InvalidatedParrySweep->Key) &&
        SweepResult.TraceEnd.Equals(InvalidatedParrySweep->Value))
    {
        // 引擎可能已排队本次Sweep后方的墙/目标；它们属于弹反前的旧方向，不能结算返程命中。
        return;
    }
    if (bHit || !IsValidOverlap(OtherActor))
    {
        return;
    }
    // 必须在终结表现和伤害之前转移；OnHit会锁定bHit并停止飞行音效，不能事后撤销。
    if (HasAuthority() && ExecutionSource.IsSet() &&
        UGuardParryComponent::CanParryProjectile(OtherActor, ExecutionSource.GetValue()))
    {
        const FAbilitySourceToken OriginalSource = ExecutionSource.GetValue();
        const FGuid OriginalAttack = DodgeAttackId;
        FAbilitySourceToken Defender;
        FString Error;
        if (AbilityExecutionIdentity::CaptureSource(OtherActor, Defender, Error) && TryReflectFrom(Defender))
        {
            if (bFromSweep && !SweepResult.Location.ContainsNaN())
            {
                if (!SweepResult.TraceStart.Equals(SweepResult.TraceEnd))
                {
                    InvalidatedParrySweep = TPair<FVector, FVector>(SweepResult.TraceStart, SweepResult.TraceEnd);
                }
                // MoveComponent可能已到本帧终点；回到实际接触点，不在原方向的墙后开始返程。
                SetActorLocation(SweepResult.Location, false, nullptr, ETeleportType::TeleportPhysics);
                PreviousDodgeLocation = GetActorLocation();
            }
            UGuardParryComponent::NotifyProjectileParried(OtherActor, OriginalSource, OriginalAttack);
            return;
        }
    }
    if (HasAuthority())
    {
        ObserveDodgeSweep(PreviousDodgeLocation, GetActorLocation());
    }
    if (!bHit)
    {
        OnHit();
    }

    if (HasAuthority())
    {
        if (UAbilitySystemComponent* TargetAsc = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor))
        {
            if (ExecutionSource.IsSet() &&
                UPerfectDodgeComponent::TryDodgeActorHit(OtherActor, ExecutionSource.GetValue(), DodgeAttackId))
            {
                // 当前身体接触仍终结原弹体；见证查询自身永远不消费弹体，也不发布伤害结果。
                Destroy();
                return;
            }
            DamageEffectParams.TargetAbilitySystemComponent = TargetAsc;
            //对目标ASC应用伤害效果

            const FVector DeathImpulse = GetActorForwardVector() * DamageEffectParams.DeathImpulseMagnitude;
            DamageEffectParams.DeathImpulse = DeathImpulse;
            const bool bKnockback = FMath::RandRange(0, 100) < DamageEffectParams.KnockbackChance;
            if (bKnockback)
            {
                FRotator Rotation = GetActorRotation();
                Rotation.Pitch = 45.f;

                const FVector KnockbackDirection = Rotation.Vector();
                const FVector KnockbackForce = KnockbackDirection * DamageEffectParams.KnockbackForceMagnitude;
                DamageEffectParams.KnockbackForce = KnockbackForce;
            }
            UMyAbilitySystemLibrary::ApplyDamageEffect(DamageEffectParams);
            DamageApplied.Broadcast(OtherActor);
        }
        Destroy(); //销毁当前Actor
    }
    else
    {
        bHit = true; //如果没有权限，设置bHit为true，等待服务器销毁
    }
}

void AMyProjectile::SetExecutionSourceToken(const FAbilitySourceToken& Source)
{
    if (ExecutionSource.IsSet())
    {
        if (auto* Previous = Cast<ICombatInterface>(ExecutionSource->Avatar.Get()))
        {
            Previous->GetOnDeathDelegate().RemoveDynamic(this, &ThisClass::DodgeSourceDied);
        }
    }
    ExecutionSource = Source;
    DodgeAttackId = FGuid::NewGuid();
    PreviousDodgeLocation = GetActorLocation();
    if (auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>())
    {
        PreviousDodgeSampleTime = Time->GetGameplayRealTime();
    }
    if (auto* Combat = Cast<ICombatInterface>(Source.Avatar.Get()))
    {
        Combat->GetOnDeathDelegate().AddUniqueDynamic(this, &ThisClass::DodgeSourceDied);
    }
}

bool AMyProjectile::ResolveExecutionSource(AActor*& Source) const
{
    Source = nullptr;
    return ExecutionSource.IsSet() && AbilityExecutionIdentity::ResolveSource(ExecutionSource.GetValue(), Source);
}

bool AMyProjectile::TryReflectFrom(const FAbilitySourceToken& Defender)
{
    AActor* PreviousSource = nullptr;
    AActor* NewSource = nullptr;
    if (!HasAuthority() || bHit || bParryReflected || IsActorBeingDestroyed() || !SupportsParryReflection() ||
        !Sphere || !ProjectileMovement || ProjectileMovement->Velocity.ContainsNaN() ||
        ProjectileMovement->Velocity.IsNearlyZero() || !ResolveExecutionSource(PreviousSource) ||
        !AbilityExecutionIdentity::ResolveSource(Defender, NewSource) || Defender.World != GetWorld() ||
        !UMyAbilitySystemLibrary::IsNotFriend(PreviousSource, NewSource) ||
        !ADungeonRunDirector::AllowsCombat(PreviousSource, NewSource) ||
        DamageEffectParams.SourceAbilitySystemComponent != ExecutionSource->ASC.Get())
    {
        return false;
    }
    FAbilityTargetToken OriginalTarget;
    FString Error;
    if (!AbilityExecutionIdentity::CaptureTarget(Defender, FPureMassCombatTarget::FromActor(PreviousSource),
                                                 OriginalTarget, Error))
    {
        return false;
    }
    const FVector ReturnVelocity = -ProjectileMovement->Velocity;
    // 先锁定一次转移并切断原施法回调，再改变归属；旧GA不能把返程伤害当成自己的命中。
    bParryReflected = true;
    ReflectedOriginalTarget = OriginalTarget;
    DamageApplied.Clear();
    OnParrySourceTransferred(PreviousSource);
    SetExecutionSourceToken(Defender);
    DamageEffectParams.WorldContextObject = NewSource;
    DamageEffectParams.SourceAbilitySystemComponent = Defender.ASC.Get();
    DamageEffectParams.TargetAbilitySystemComponent = nullptr;
    DamageEffectParams.DeathImpulse = FVector::ZeroVector;
    DamageEffectParams.KnockbackForce = FVector::ZeroVector;
    SetOwner(NewSource);
    SetInstigator(Cast<APawn>(NewSource));
    Sphere->IgnoreActorWhenMoving(PreviousSource, false);
    Sphere->IgnoreActorWhenMoving(NewSource, true);
    ProjectileMovement->HomingTargetComponent = nullptr;
    ProjectileMovement->bIsHomingProjectile = false;
    HomingTargetSceneComponent = nullptr;
    ProjectileMovement->Velocity = ReturnVelocity;
    SetActorRotation(ReturnVelocity.Rotation());
    // 已存在弹体不会再经过OnActorSpawned；立即登记新归属的可选慢速补偿。
    if (auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>())
    {
        Time->RegisterCompensatedActor(this, NewSource);
    }
    ForceNetUpdate();
    return true;
}

void AMyProjectile::OnParrySourceTransferred(AActor*)
{
    // 普通弹道没有额外施法寿命；专属子类在此解除旧生命的独立委托。
}

void AMyProjectile::DodgeSourceDied(AActor*)
{
    if (HasAuthority())
    {
        Destroy();
    }
}

void AMyProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ObserveTraveledDodgeSegment();
}

void AMyProjectile::ObserveTraveledDodgeSegment()
{
    if (HasAuthority() && !bHit)
    {
        ObserveDodgeSweep(PreviousDodgeLocation, GetActorLocation());
        PreviousDodgeLocation = GetActorLocation();
        if (auto* Time = GetWorld()->GetSubsystem<UAbilityWorldTimeSubsystem>())
        {
            PreviousDodgeSampleTime = Time->GetGameplayRealTime();
        }
    }
}

void AMyProjectile::ObserveDodgeSweep(const FVector& Start, const FVector& End)
{
    if (!ExecutionSource.IsSet() || !DodgeAttackId.IsValid() || !Sphere)
    {
        return;
    }
    const float ProjectileRadius = Sphere->GetScaledSphereRadius();
    UPerfectDodgeComponent::ObserveActorAttack(ExecutionSource.GetValue(), DodgeAttackId, Start,
        [&](const FVector& Center, float Radius, float HalfHeight)
        {
            const float Axis = FMath::Max(0.f, HalfHeight - Radius);
            FVector OnShot;
            FVector OnCapsule;
            FMath::SegmentDistToSegmentSafe(Start, End, Center - FVector(0, 0, Axis),
                Center + FVector(0, 0, Axis), OnShot, OnCapsule);
            return FVector::DistSquared(OnShot, OnCapsule) <= FMath::Square(ProjectileRadius + Radius);
        }, nullptr, PreviousDodgeSampleTime);
}

bool AMyProjectile::IsValidOverlap(AActor* OtherActor)
{
    AActor* CapturedAvatar = nullptr;
    if (HasAuthority() && ExecutionSource.IsSet() &&
        !AbilityExecutionIdentity::ResolveSource(ExecutionSource.GetValue(), CapturedAvatar))
    {
        return false;
    }
    if (DamageEffectParams.SourceAbilitySystemComponent == nullptr)
    {
        return false;
    }
    AActor* SourceAvatarActor = DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor();
    if (!IsValid(SourceAvatarActor) || !IsValid(OtherActor))
    {
        return false;
    }
    if (SourceAvatarActor == OtherActor)
    {
        return false;
    }
    if (!UMyAbilitySystemLibrary::IsNotFriend(SourceAvatarActor, OtherActor))
    {
        return false;
    }
    if (bParryReflected && OtherActor->Implements<UCombatInterface>())
    {
        if (ICombatInterface::Execute_IsDead(OtherActor) ||
            !ADungeonRunDirector::AllowsCombat(SourceAvatarActor, OtherActor))
        {
            return false;
        }
        if (ReflectedOriginalTarget.IsSet() && ReflectedOriginalTarget->Target.Actor.Get() == OtherActor)
        {
            FVector Location;
            if (!AbilityExecutionIdentity::ResolveTarget(ExecutionSource.GetValue(), ReflectedOriginalTarget.GetValue(),
                                                         Location))
            {
                return false;
            }
        }
    }

    return true;
}
