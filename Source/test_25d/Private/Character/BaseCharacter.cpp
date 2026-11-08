// 主要链路：角色基类统一战斗接口、地形状态和死亡表现；派生角色负责绑定能力系统，伤害通知与死亡广播供外部系统订阅。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/BaseCharacter.h"
#include "AbilitySystem/Batch6/Batch6Slow.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "test_25d/test_25d.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/Passive/PassiveNiagaraComponent.h"
#include "Terrain/TerrainStatusComponent.h"

// Sets default values
// 初始化碰撞、武器挂点、地形与状态特效组件。
ABaseCharacter::ABaseCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    TerrainStatus = CreateDefaultSubobject<UTerrainStatusComponent>(TEXT("TerrainStatus"));
    const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();

    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetGenerateOverlapEvents(false);
    GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
    GetMesh()->SetGenerateOverlapEvents(true);

    Weapon = CreateDefaultSubobject<USkeletalMeshComponent>("Weapon");
    Weapon->SetupAttachment(GetMesh(), FName("WeaponHandSocket"));
    Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    BurnDebuffComponent = CreateDefaultSubobject<UDebuffNiagaraComponent>("BurnDebuffComponent");
    BurnDebuffComponent->SetupAttachment(GetRootComponent());
    BurnDebuffComponent->DebuffTag = GameplayTags.Debuff_Burn;

    StunDebuffComponent = CreateDefaultSubobject<UDebuffNiagaraComponent>("StunDebuffComponent");
    StunDebuffComponent->SetupAttachment(GetRootComponent());
    StunDebuffComponent->DebuffTag = GameplayTags.Debuff_Stun;

    EffectAttachComponent = CreateDefaultSubobject<USceneComponent>("EffectAttachPoint");
    EffectAttachComponent->SetupAttachment(GetRootComponent());
    HaloOfProtectionNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("HaloOfProtectionComponent");
    HaloOfProtectionNiagaraComponent->SetupAttachment(EffectAttachComponent);
    LifeSiphonNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("LifeSiphonNiagaraComponent");
    LifeSiphonNiagaraComponent->SetupAttachment(EffectAttachComponent);
    ManaSiphonNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("ManaSiphonNiagaraComponent");
    ManaSiphonNiagaraComponent->SetupAttachment(EffectAttachComponent);

    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
// 每帧将特效挂点保持世界零旋转，避免随角色转向。
void ABaseCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    EffectAttachComponent->SetWorldRotation(FRotator::ZeroRotator);
}

// 调用父类伤害入口并广播实际返回伤害。
float ABaseCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator,
                                 AActor* DamageCauser)
{
    const float DamageTaken = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    FOnDamage.Broadcast(DamageTaken);
    return DamageTaken;
}

// 返回当前角色或玩家状态持有的能力系统。
UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

// 返回受击动画配置。
UAnimMontage* ABaseCharacter::GetHitReactMontage_Implementation()
{
    return HitReactMontage;
}

// 返回受击血液特效配置。
UNiagaraSystem* ABaseCharacter::GetBloodEffect_Implementation()
{
    return BloodEffect;
}

// 按精确标签查找攻击动画，未命中返回空配置。
FTaggedMontage ABaseCharacter::GetTaggedMontageByTag_Implementation(const FGameplayTag& MontageTag)
{
    for (const FTaggedMontage& TaggedMontage : AttackMontage)
    {
        if (TaggedMontage.MontageTag == MontageTag)
        {
            return TaggedMontage;
        }
    }
    return FTaggedMontage();
}

// 读取当前召唤物计数。
int32 ABaseCharacter::GetMinionCount_Implementation()
{
    return MinionCount;
}

// 累加召唤物计数。
void ABaseCharacter::IncreaseMinionCount_Implementation(int32 Amount)
{
    MinionCount += Amount;
}

// 返回职业配置。
ECharacterClass ABaseCharacter::GetCharacterClass_Implementation()
{
    return CharacterClass;
}

// 返回武器网格组件。
USkeletalMeshComponent* ABaseCharacter::GetWeapon_Implementation()
{
    return Weapon;
}

// 提供能力系统注册完成的订阅入口。
FOnASCRegistered& ABaseCharacter::GetOnASCRegisteredDelegate()
{
    return OnASCRegistered;
}
// 提供角色死亡事件订阅入口。
FOnDeathSignature& ABaseCharacter::GetOnDeathDelegate()
{
    return FOnDeath;
}

// 清理地形效果并按策略分离武器，随后广播死亡表现。
void ABaseCharacter::Die(const FVector& DeathImpulse)
{
    TerrainStatus->ClearTerrainEffects();
    if (DetachWeaponOnDeath())
        Weapon->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
    MulticastHandleDeath(DeathImpulse);
}

// 同步死亡音效、布娃娃与溶解表现，最后广播死亡。
void ABaseCharacter::MulticastHandleDeath_Implementation(const FVector& DeathImpulse)
{
    UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation(), GetActorRotation());

    if (DetachWeaponOnDeath())
    {
        Weapon->SetSimulatePhysics(true);
        Weapon->SetEnableGravity(true);
        Weapon->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
        Weapon->AddImpulse(DeathImpulse * 0.1f, NAME_None, true);
    }

    GetMesh()->SetSimulatePhysics(true);
    GetMesh()->SetEnableGravity(true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    GetMesh()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    GetMesh()->AddImpulse(DeathImpulse, NAME_None, true);

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Dissolve();

    bDead = true;
    BurnDebuffComponent->Deactivate();
    StunDebuffComponent->Deactivate();
    FOnDeath.Broadcast(this);
}

// 将眩晕标签计数转换为状态并重新计算移动速度。
void ABaseCharacter::StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    bIsStunned = NewCount > 0;
    RefreshMovementSpeed();
}

// 合并死亡、眩晕、受击、藤蔓定身与减速；定身时立即停止移动。
void ABaseCharacter::RefreshMovementSpeed()
{
    const bool bHitReact = AbilitySystemComponent &&
                           AbilitySystemComponent->HasMatchingGameplayTag(FMyGameplayTags::Get().Effects_HitReact);
    const bool bRooted = TerrainStatus && TerrainStatus->IsTerrainStatusActive(ETerrainSurfaceType::Vines);
    const float Multiplier = FMath::Min(TerrainStatus ? TerrainStatus->GetTerrainMovementMultiplier() : 1.f,
                                        UBatch6SlowComponent::GetMultiplier(this));
    GetCharacterMovement()->MaxWalkSpeed =
        (bDead || bIsStunned || bHitReact || bRooted) ? 0.f : BaseWalkSpeed * Multiplier;
    if (bRooted)
        GetCharacterMovement()->StopMovementImmediately();
}

// Called when the game starts or when spawned
// 进入游戏时调用父类初始化。
void ABaseCharacter::BeginPlay()
{
    Super::BeginPlay();
}

// 返回死亡状态，地下城倒地也映射到该状态。
bool ABaseCharacter::IsDead_Implementation() const
{
    return bDead;
}

// 将当前角色作为战斗化身返回。
AActor* ABaseCharacter::GetAvatar_Implementation()
{
    return this;
}

// 按武器、左右手或尾部标签取得攻击起点，未知标签返回零向量。
FVector ABaseCharacter::GetCombatSocketLocation_Implementation(const FGameplayTag& MontageTag)
{

    if (MontageTag.MatchesTagExact(FMyGameplayTags::Get().CombatSocket_Weapon) && IsValid(Weapon))
    {
        return Weapon->GetSocketLocation(CombatTipSocketName);
    }
    if (MontageTag.MatchesTagExact(FMyGameplayTags::Get().CombatSocket_LeftHand))
    {
        return GetMesh()->GetSocketLocation(LeftHandSocketName);
    }
    if (MontageTag.MatchesTagExact(FMyGameplayTags::Get().CombatSocket_RightHand))
    {
        return GetMesh()->GetSocketLocation(RightHandSocketName);
    }
    if (MontageTag.MatchesTagExact(FMyGameplayTags::Get().CombatSocket_Tail))
    {
        return GetMesh()->GetSocketLocation(TailSocketName);
    }
    return FVector();
}

// 预留派生角色绑定能力系统的入口。
void ABaseCharacter::InitAbilityActorInfo()
{
}

// 使用自身作为来源创建效果规格，并施加到自己的能力系统。
void ABaseCharacter::ApplyEffectToSelf(TSubclassOf<UGameplayEffect> GameplayEffectClass, float Level) const
{

    check(IsValid(GetAbilitySystemComponent()));
    check(GameplayEffectClass)

        FGameplayEffectContextHandle EffectContextHandle = AbilitySystemComponent->MakeEffectContext();

    EffectContextHandle.AddSourceObject(this);

    const FGameplayEffectSpecHandle SpecHandle =
        AbilitySystemComponent->MakeOutgoingSpec(GameplayEffectClass, Level, EffectContextHandle);
    GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), GetAbilitySystemComponent());
}

// 依次应用主属性、次属性和生命资源初始效果。
void ABaseCharacter::InitlizeDefaultAttributes() const
{
    ApplyEffectToSelf(DefaultPrimaryAttributes, 1.f);
    ApplyEffectToSelf(DefaultSecondaryAttributes, 1.f);
    ApplyEffectToSelf(DefaultVitalAttributes, 1.f);
}

// 仅服务器授予启动主动与被动能力。
void ABaseCharacter::AddCharacterAbilities()
{

    if (!HasAuthority())
        return;
    UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(GetAbilitySystemComponent());
    MyASC->AddCharacterAbilities(StartupAbility);
    MyASC->AddCharacterPassiveAbilities(StartupPassiveAbility);
}

// 为角色和武器创建动态材质，再交由蓝图时间线推进溶解。
void ABaseCharacter::Dissolve()
{
    if (IsValid(DissolveMeterialInstance))
    {
        UMaterialInstanceDynamic* DynamicMaterialInstance =
            UMaterialInstanceDynamic::Create(DissolveMeterialInstance, this);
        GetMesh()->SetMaterial(0, DynamicMaterialInstance);
        StartDissolveTimeline(DynamicMaterialInstance);
    }
    if (IsValid(WeaponDissolveMeterialInstance))
    {
        UMaterialInstanceDynamic* DynamicMaterialInstance =
            UMaterialInstanceDynamic::Create(WeaponDissolveMeterialInstance, this);
        Weapon->SetMaterial(0, DynamicMaterialInstance);
        StartWeaponDissolveTimeline(DynamicMaterialInstance);
    }
}

// 提供伤害事件订阅入口。
FOnDamageSignature& ABaseCharacter::GetOnDamageSignature()
{
    return FOnDamage;
}

// 返回全部攻击动画配置。
TArray<FTaggedMontage> ABaseCharacter::GetAttackMontages_Implementation()
{
    return AttackMontage;
}
// 注册本类需要复制的状态，同时保留父类复制字段。
void ABaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ABaseCharacter, bIsStunned);
    DOREPLIFETIME(ABaseCharacter, bIsBurned);
    DOREPLIFETIME(ABaseCharacter, bIsBeingShocked);
}

// 更新电击表现状态。
void ABaseCharacter::SetIsBeingShocked_Implementation(bool bInShock)
{
    bIsBeingShocked = bInShock;
}

// 读取电击表现状态。
bool ABaseCharacter::IsBeingShocked_Implementation() const
{
    return bIsBeingShocked;
}
// 复制眩晕状态后刷新移动速度。
void ABaseCharacter::OnRep_Stunned()
{
    RefreshMovementSpeed();
}

// 保留燃烧复制回调供派生类实现表现。
void ABaseCharacter::OnRep_Burned()
{
}
