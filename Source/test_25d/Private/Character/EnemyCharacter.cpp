// 调用链：出生/池唤醒初始化 GAS → 战斗死亡 → 共享掉落排队 → 死亡表现与池回收/销毁。

#include "Character/EnemyCharacter.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "UI/Widget/MyUserWidget.h"
#include "MyGameplayTags.h"
#include "AI/MyAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "Mass/MonsterManagementSubsystem.h"
#include "Roguelike/DungeonRunDirector.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "NiagaraComponent.h"
#include "Terrain/TerrainStatusComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AEnemyCharacter::AEnemyCharacter()
{
    GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    HealthBar = CreateDefaultSubobject<UWidgetComponent>("HealthBar");
    HealthBar->SetupAttachment(GetRootComponent()); //将血条附件到根节点上

    AbilitySystemComponent = CreateDefaultSubobject<UMyAbilitySystemComponent>("AbilitySystemComponent");
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
    AttributeSet = CreateDefaultSubobject<UMyAttributeSet>("AttributeSet");

    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;
    bUseControllerRotationYaw = false;
    BaseWalkSpeed = 250.f;

    GetCharacterMovement()->bUseControllerDesiredRotation = true;
    GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

    Weapon->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);
    GetMesh()->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);

    Weapon->MarkRenderStateDirty();
    GetMesh()->MarkRenderStateDirty();
}

void AEnemyCharacter::HighLightActor_Implementation()
{
    GetMesh()->SetRenderCustomDepth(true);
    Weapon->SetRenderCustomDepth(true);
}

void AEnemyCharacter::UnHighLightActor_Implementation()
{
    GetMesh()->SetRenderCustomDepth(false);
    Weapon->SetRenderCustomDepth(false);
}

void AEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();
    InitAbilityActorInfo();

    if (HasAuthority())
    {
        GrantEnemyAbilities();
        if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
            Manager->RegisterEnemy(this);
    }

    if (UMyUserWidget* UserWidget = Cast<UMyUserWidget>(HealthBar->GetUserWidgetObject()))
    {
        UserWidget->SetWidgetController(this);
    }

    if (const UMyAttributeSet* AS = Cast<UMyAttributeSet>(AttributeSet))
    {
        //监听血量变化
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AS->GetHealthAttribute())
            .AddLambda(
                [this](const FOnAttributeChangeData& Data)
                {
                    OnHealthChanged.Broadcast(Data.NewValue);
                    if (HasAuthority() && Data.NewValue < Data.OldValue)
                        if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
                            Manager->NotifyDamaged(this);
                });
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AS->GetMaxHealthAttribute())
            .AddLambda(
                [this](const FOnAttributeChangeData& Data)
                {
                    OnMaxHealthChanged.Broadcast(Data.NewValue);
                });

        AbilitySystemComponent
            ->RegisterGameplayTagEvent(FMyGameplayTags::Get().Effects_HitReact, EGameplayTagEventType::NewOrRemoved)
            .AddUObject(this, &AEnemyCharacter::HitReactTagChanged

            );

        //初始化血量
        OnHealthChanged.Broadcast(AS->GetHealth());
        OnMaxHealthChanged.Broadcast(AS->GetMaxHealth());
    }
}

void AEnemyCharacter::Die(const FVector& DeathImpulse)
{
    if (!HasAuthority() || bDead)
        return;
    // Mark terminal state before loot/ability callbacks can reenter Die.
    bDead = true;
    InvalidateEnemyAttack();
    // 在对象池提前返回之前快照奖励；bDead 已置位，重入不会再次排队。
    if (auto* Loot = GetWorld()->GetSubsystem<USharedLootSubsystem>())
    {
        Loot->QueueEnemyLoot(*this);
    }
    if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
        Manager->UnregisterEnemy(this);
    if (ActorHasTag(TEXT("DungeonPoolManaged")))
    {
        // Keep the weapon attached; use a reversible ragdoll without Blueprint dissolve.
        // GAS cleanup is deferred to the director's next tick, outside damage callbacks.
        DungeonPoolLife = -FMath::Max(1, FMath::Abs(DungeonPoolLife));
        bDungeonPoolCorpse = true;
        DungeonPoolDeathImpulse = DeathImpulse;
        PoolCorpseUntil = GetWorld()->GetTimeSeconds() + PoolCorpseSeconds;
        SetLifeSpan(0);
        OnRep_DungeonPoolLife();
        ForceNetUpdate();
        FOnDeath.Broadcast(this);
        return;
    }
    SetLifeSpan(LifeSpan);
    if (MyAIController && MyAIController->GetBlackboardComponent())
        MyAIController->GetBlackboardComponent()->SetValueAsBool(FName("Dead"), true);
    // 原 SpawnLoot 蓝图事件保留资产兼容，但死亡不再调用，避免和共享规则重复掉落。
    Super::Die(DeathImpulse);
}

void AEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
        Manager->UnregisterEnemy(this);
    Super::EndPlay(EndPlayReason);
}

// 对地图放置和导演生成的敌人应用同一分类表，不依赖特定 GameMode。
EEnemyLootRank AEnemyCharacter::GetLootRank() const
{
    return GetDefault<USharedLootSettings>()->ResolveRank(GetClass(), LootRank);
}

void AEnemyCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    if (!HasAuthority())
        return;
    MyAIController = Cast<AMyAIController>(NewController);
    if (!MyAIController || !BehaviorTree || !BehaviorTree->BlackboardAsset)
        return;
    MyAIController->GetBlackboardComponent()->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
    MyAIController->RunBehaviorTree(BehaviorTree);
    MyAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), false);
    MyAIController->GetBlackboardComponent()->SetValueAsBool(FName("RangeAttacker"),
                                                             CharacterClass != ECharacterClass::Warrior);
}

void AEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEnemyCharacter, DungeonPoolLife);
    DOREPLIFETIME(AEnemyCharacter, Level);
    DOREPLIFETIME(AEnemyCharacter, LootRank);
    DOREPLIFETIME(AEnemyCharacter, bDungeonEncounter);
    DOREPLIFETIME(AEnemyCharacter, bDungeonPoolCorpse);
    DOREPLIFETIME(AEnemyCharacter, DungeonPoolDeathImpulse);
}

void AEnemyCharacter::EnableDungeonPooling(float CorpseSeconds)
{
    if (!HasAuthority() || DungeonPoolLife != 0)
        return;
    Tags.AddUnique(TEXT("DungeonPoolManaged"));
    PoolCorpseSeconds = FMath::Clamp(CorpseSeconds, 0.f, 10.f);
    // A bounded pool stays relevant so remote peers observe sleep and the next lease.
    bAlwaysRelevant = true;
    DungeonPoolLife = 1;
    TArray<FGameplayAttribute> Attributes;
    AbilitySystemComponent->GetAllAttributes(Attributes);
    for (const auto& Attribute : Attributes)
        PoolAttributeBases.Add(Attribute, AbilitySystemComponent->GetNumericAttributeBase(Attribute));
    OnRep_DungeonPoolLife();
    ForceNetUpdate();
}

void AEnemyCharacter::OnRep_DungeonPoolLife()
{
    if (!DungeonPoolLife)
        return;
    if (!bPoolTickCaptured)
    {
        bPoolTickCaptured = true;
        bPoolActorTick = IsActorTickEnabled();
        bPoolActorCollision = GetClass()->GetDefaultObject<AEnemyCharacter>()->GetActorEnableCollision();
        PoolMeshTransform = GetMesh()->GetRelativeTransform();
        PoolMeshResponses = GetMesh()->GetCollisionResponseToChannels();
        PoolMeshCollision = GetMesh()->GetCollisionEnabled();
        PoolCapsuleCollision = GetCapsuleComponent()->GetCollisionEnabled();
        TInlineComponentArray<UActorComponent*> Components(this);
        for (auto* Component : Components)
            PoolComponentTicks.Add(Component, Component->IsComponentTickEnabled());
    }
    const bool Sleeping = DungeonPoolLife < 0;
    bDead = Sleeping;
    if (Sleeping)
        InvalidateEnemyAttack();
    const bool Corpse = Sleeping && bDungeonPoolCorpse;
    SetActorHiddenInGame(Sleeping && !Corpse);
    SetActorEnableCollision((!Sleeping || Corpse) && bPoolActorCollision);
    SetActorTickEnabled(!Sleeping && bPoolActorTick);
    for (const auto& Entry : PoolComponentTicks)
        if (auto* Component = Entry.Key.Get())
        {
            Component->SetComponentTickEnabled(!Sleeping && Entry.Value);
            if (Sleeping)
                if (auto* Niagara = Cast<UNiagaraComponent>(Component))
                    Niagara->DeactivateImmediate();
        }
    if (Sleeping)
    {
        if (Corpse)
        {
            GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            GetMesh()->SetComponentTickEnabled(true);
            if (!bPoolRagdollApplied)
            {
                bPoolRagdollApplied = true;
                GetMesh()->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
                GetMesh()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
                if (GetMesh()->GetPhysicsAsset())
                {
                    GetMesh()->SetSimulatePhysics(true);
                    GetMesh()->AddImpulse(DungeonPoolDeathImpulse, NAME_None, true);
                }
            }
        }
        else
            GetMesh()->SetSimulatePhysics(false);
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        if (auto* Anim = GetMesh()->GetAnimInstance())
            Anim->StopAllMontages(0.f);
        if (MyAIController)
        {
            MyAIController->StopMovement();
            MyAIController->ClearFocus(EAIFocusPriority::Gameplay);
            if (auto* Brain = MyAIController->GetBrainComponent())
                Brain->StopLogic(TEXT("Dungeon pool"));
            MyAIController->SetActorTickEnabled(false);
        }
    }
    else
    {
        if (bPoolRagdollApplied)
        {
            GetMesh()->SetSimulatePhysics(false);
            GetMesh()->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            GetMesh()->SetRelativeTransform(PoolMeshTransform);
            GetMesh()->SetCollisionEnabled(PoolMeshCollision);
            GetMesh()->SetCollisionResponseToChannels(PoolMeshResponses);
            GetMesh()->InitAnim(true);
            bPoolRagdollApplied = false;
        }
        GetCapsuleComponent()->SetCollisionEnabled(PoolCapsuleCollision);
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        RefreshMovementSpeed();
    }
    ReconcileEnemyPresentation();
}

void AEnemyCharacter::SleepForDungeonPool(bool bKeepCorpse)
{
    if (!HasAuthority() || !ActorHasTag(TEXT("DungeonPoolManaged")))
        return;
    InvalidateEnemyAttack();
    if (!bKeepCorpse)
        bDungeonPoolCorpse = false;
    if (bPoolResetDone)
    {
        OnRep_DungeonPoolLife();
        ForceNetUpdate();
        return;
    }
    bPoolResetDone = true;
    DungeonPoolLife = -FMath::Max(1, FMath::Abs(DungeonPoolLife));
    bDead = true;
    SetLifeSpan(0);
    if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
        Manager->UnregisterEnemy(this);
    TerrainStatus->ClearTerrainEffects();
    AbilitySystemComponent->CancelAllAbilities();
    AbilitySystemComponent->ClearAllAbilities();
    AbilitySystemComponent->RemoveActiveEffects(FGameplayEffectQuery());
    AbilitySystemComponent->RemoveAllGameplayCues();
    FGameplayTagContainer TagsToClear;
    AbilitySystemComponent->GetOwnedGameplayTags(TagsToClear);
    for (const auto& Tag : TagsToClear)
        AbilitySystemComponent->SetLooseGameplayTagCount(Tag, 0);
    GetWorldTimerManager().ClearAllTimersForObject(this);
    GetWorld()->GetLatentActionManager().RemoveActionsForObject(this);
    CombatTarget = nullptr;
    bHitReacting = bIsStunned = bIsBurned = bIsBeingShocked = false;
    MinionCount = 0;
    UnHighLightActor_Implementation();
    OnRep_DungeonPoolLife();
    ForceNetUpdate();
}

bool AEnemyCharacter::WakeFromDungeonPool(const FTransform& Transform, FName Room)
{
    if (!HasAuthority() || !IsDungeonPoolSleeping() || bDungeonPoolCorpse || !bPoolResetDone || IsActorBeingDestroyed())
        return false;
    SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
    MonsterRoomId = Room;
    for (const auto& Entry : PoolAttributeBases)
        AbilitySystemComponent->SetNumericAttributeBase(Entry.Key, Entry.Value);
    if (UMyAbilitySystemLibrary::GetCharacterClassInfo(this))
        InitlizeDefaultAttributes();
    GrantEnemyAbilities();
    bPoolResetDone = false;
    DungeonPoolLife = FMath::Abs(DungeonPoolLife) + 1;
    OnRep_DungeonPoolLife();
    if (MyAIController)
    {
        MyAIController->SetActorTickEnabled(true);
        if (BehaviorTree && BehaviorTree->BlackboardAsset)
        {
            MyAIController->GetBlackboardComponent()->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
            auto* Blackboard = MyAIController->GetBlackboardComponent();
            // InitializeBlackboard is a no-op for the same asset. Clear inherited keys too.
            for (int32 Key = 0; Key < Blackboard->GetNumKeys(); ++Key)
                Blackboard->ClearValue(FBlackboard::FKey(Key));
            MyAIController->GetBlackboardComponent()->SetValueAsBool(TEXT("Dead"), false);
            MyAIController->GetBlackboardComponent()->SetValueAsBool(TEXT("HitReacting"), false);
            MyAIController->GetBlackboardComponent()->SetValueAsBool(TEXT("Stun"), false);
            MyAIController->GetBlackboardComponent()->SetValueAsBool(TEXT("RangeAttacker"),
                                                                     CharacterClass != ECharacterClass::Warrior);
            MyAIController->RunBehaviorTree(BehaviorTree);
            if (auto* Brain = MyAIController->GetBrainComponent())
                Brain->RestartLogic();
        }
    }
    if (auto* Manager = GetWorld()->GetSubsystem<UMonsterManagementSubsystem>())
        Manager->RegisterEnemy(this);
    ForceNetUpdate();
    return true;
}

void AEnemyCharacter::StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    Super::StunTagChanged(CallbackTag, NewCount);
    if (MyAIController && MyAIController->GetBlackboardComponent())
        MyAIController->GetBlackboardComponent()->SetValueAsBool(FName("Stun"), bIsStunned);
}

void AEnemyCharacter::GrantEnemyAbilities()
{
    UMyAbilitySystemLibrary::GiveStartupAbilities(this, AbilitySystemComponent, CharacterClass);
}

int32 AEnemyCharacter::GetPlayerLevel_Implementation()
{
    return Level;
}

void AEnemyCharacter::InitlizeDefaultAttributes() const
{
    UMyAbilitySystemLibrary::InitializeDefaultAttributes(this, CharacterClass, Level, AbilitySystemComponent);
}

void AEnemyCharacter::HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    bHitReacting = NewCount > 0;
    RefreshMovementSpeed();
    if (MyAIController && MyAIController->GetBlackboardComponent())
        MyAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), bHitReacting);
}

void AEnemyCharacter::InitAbilityActorInfo()
{
    AbilitySystemComponent->InitAbilityActorInfo(this, this);
    UMyAbilitySystemComponent* UmyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent);
    UmyASC->AbilityActorInfoSet();

    if (HasAuthority())
    {
        InitlizeDefaultAttributes();
    }
    OnASCRegistered.Broadcast(AbilitySystemComponent);

    AbilitySystemComponent
        ->RegisterGameplayTagEvent(FMyGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved)
        .AddUObject(this, &ABaseCharacter::StunTagChanged);
}
AActor* AEnemyCharacter::GetCombatTarget_Implementation() const
{
    return CombatTarget;
}

void AEnemyCharacter::SetCombatTarget_Implementation(AActor* InCombatTarget)
{
    CombatTarget = IsValid(InCombatTarget) &&
                           (!InCombatTarget->Implements<UCombatInterface>() ||
                            !ICombatInterface::Execute_IsDead(InCombatTarget)) &&
                           ADungeonRunDirector::AllowsCombat(this, InCombatTarget)
                       ? InCombatTarget
                       : nullptr;
}

void AEnemyCharacter::SetMoveToLocation_Implementation(FVector& OutDestination)
{
    // Do not change OutDestination
}

bool AEnemyCharacter::CanFinishPoolReturn() const
{
    return !bDungeonPoolCorpse || GetWorld()->GetTimeSeconds() >= PoolCorpseUntil;
}

void AEnemyCharacter::MulticastHandleDeath_Implementation(const FVector& DeathImpulse)
{
    Super::MulticastHandleDeath_Implementation(DeathImpulse);
    InvalidateEnemyAttack();
    SetActorTickEnabled(false);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    if (MyAIController)
    {
        MyAIController->StopMovement();
        if (auto* Brain = MyAIController->GetBrainComponent())
            Brain->StopLogic(TEXT("Death"));
    }
}
