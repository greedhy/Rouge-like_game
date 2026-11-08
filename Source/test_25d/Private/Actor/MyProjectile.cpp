// Fill out your copyright notice in the Description page of Project Settings.

#include "Actor/MyProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "test_25d/test_25d.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"

AMyProjectile::AMyProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
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
    if (bHit || !IsValidOverlap(OtherActor))
        return;
    if (!bHit)
        OnHit();

    if (HasAuthority())
    {
        if (UAbilitySystemComponent* TargetAsc = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor))
        {
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
        bHit = true; //如果没有权限，设置bHit为true，等待服务器销毁
}

bool AMyProjectile::IsValidOverlap(AActor* OtherActor)
{
    if (DamageEffectParams.SourceAbilitySystemComponent == nullptr)
        return false;
    AActor* SourceAvatarActor = DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor();
    if (SourceAvatarActor == OtherActor)
        return false;
    if (!UMyAbilitySystemLibrary::IsNotFriend(SourceAvatarActor, OtherActor))
        return false;

    return true;
}
