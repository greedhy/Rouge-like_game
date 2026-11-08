// 主要链路：服务器占有角色后绑定玩家状态中的能力系统并恢复进度；客户端在玩家状态复制到达后绑定界面，存档从同一能力与属性通路导出。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/PlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Equipment/EquipmentComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"
#include "Player/MyPlayerController.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "GameFramework/SpringArmComponent.h"

#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "Game/LoadScreenSaveGame.h"
#include "Game/MyGameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"

#include "UI/HUD/MyHUD.h"
// 创建俯视相机和升级特效，并配置平面移动。
APlayerCharacter::APlayerCharacter()
{
    LevelUpNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>("LevelUpNiagaraComponent");
    LevelUpNiagaraComponent->SetupAttachment(GetRootComponent());
    LevelUpNiagaraComponent->bAutoActivate = false;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>("CameraBoom");
    CameraBoom->SetupAttachment(GetRootComponent());
    CameraBoom->SetUsingAbsoluteRotation(true);
    CameraBoom->bDoCollisionTest = false;
    TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>("TopDownCameraComponent");
    TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    TopDownCameraComponent->bUsePawnControlRotation = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);
    GetCharacterMovement()->bConstrainToPlane = true;
    GetCharacterMovement()->bSnapToPlaneAtStart = true;

    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;
    bUseControllerRotationYaw = false;

    CharacterClass = ECharacterClass::Elementalist;
}

// 在蓝图默认值应用后启用胶囊重叠，使交互范围独立于外观物理资产。
void APlayerCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    // Interaction belongs to the player body, independent of a cosmetic mesh's physics asset.
    // Apply after Blueprint defaults so existing player variants inherit the same contract.
    GetCapsuleComponent()->SetGenerateOverlapEvents(true);
}

// 服务器占有后恢复能力与进度、对账装备效果，再恢复世界状态。
void APlayerCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    InitAbilityActorInfo();
    LoadProgress();
    if (auto* State = GetPlayerState<AMyPlayerState>())
        State->GetEquipmentComponent()->ReconcileEffects();
    //AddCharacterAbilities();

    if (AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this)))
    {
        MyGameMode->LoadWorldState(GetWorld());
    }
}

// 客户端收到玩家状态后补建能力系统与界面绑定。
void APlayerCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitAbilityActorInfo();
}

// 读取玩家状态等级。
int32 APlayerCharacter::GetPlayerLevel_Implementation()
{
    AMyPlayerState* Player_State = GetPlayerState<AMyPlayerState>();
    check(Player_State);
    return Player_State->GetPlayerLevel();
}

// 清理延迟死亡计时器后执行父类结束流程。
void APlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(DeathTimer);
    Super::EndPlay(Reason);
}

// 执行基础死亡表现，延迟通知游戏模式并分离相机。
void APlayerCharacter::Die(const FVector& DeathImpulse)
{
    Super::Die(DeathImpulse);

    FTimerDelegate DeathTimerDelegate;
    DeathTimerDelegate.BindWeakLambda(this,
                                      [this]()
                                      {
                                          AMyGameModeBase* MyGM =
                                              Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
                                          if (MyGM)
                                          {
                                              MyGM->PlayerDied(this);
                                          }
                                      });
    GetWorldTimerManager().SetTimer(DeathTimer, DeathTimerDelegate, DeathTime, false);
    TopDownCameraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
}

// 以玩家状态为所有者、角色为化身绑定能力系统，刷新装备外观并初始化界面。
void APlayerCharacter::InitAbilityActorInfo()
{
    AMyPlayerState* Player_State = GetPlayerState<AMyPlayerState>();
    check(Player_State);
    Player_State->GetAbilitySystemComponent()->InitAbilityActorInfo(Player_State, this);
    Player_State->GetEquipmentComponent()->RefreshWeaponVisuals();
    UMyAbilitySystemComponent* UmyASC = Cast<UMyAbilitySystemComponent>(Player_State->GetAbilitySystemComponent());
    if (UmyASC)
    {
        UmyASC->AbilityActorInfoSet();
    }

    AbilitySystemComponent = Player_State->GetAbilitySystemComponent();
    AttributeSet = Player_State->GetAttributeSet();
    OnASCRegistered.Broadcast(AbilitySystemComponent);

    AbilitySystemComponent
        ->RegisterGameplayTagEvent(FMyGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved)
        .AddUObject(this, &ABaseCharacter::StunTagChanged);

    AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController());
    if (MyPlayerController)
    {
        AMyHUD* MyHUD = Cast<AMyHUD>(MyPlayerController->GetHUD());
        if (MyHUD)
        {
            MyHUD->InitOverlay(MyPlayerController, Player_State, AbilitySystemComponent, AttributeSet);
        }
    }
}
// 优先读取旅行进度，否则读取存档；恢复技能与基础属性，再对账装备并恢复旅行资源。
void APlayerCharacter::LoadProgress()
{
    AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
    if (MyGameMode)
    {
        AMyPlayerState* TravelState = GetPlayerState<AMyPlayerState>();
        const bool bFromTravel = TravelState && TravelState->GetTravelProgress();
        ULoadScreenSaveGame* SaveData =
            bFromTravel ? TravelState->GetTravelProgress() : MyGameMode->RetrieveInGameSaveData();
        if (SaveData == nullptr)
            return;

        // 首次加载授予默认能力；已有进度按存档重建，再补发现新增能力。
        if (SaveData->bFirstTimeLoadIn)
        {

            InitlizeDefaultAttributes();
            AddCharacterAbilities();
            // Discover learnable level-one abilities on a new save as well as an old save.
            if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
            {
                MyASC->UpdateAbilityStatuses(GetPlayerLevel_Implementation());
            }
        }
        else
        {
            if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
            {
                MyASC->AddCharacterAbilitiesFromSaveData(SaveData);
            }

            if (AMyPlayerState* MyPlayerState = Cast<AMyPlayerState>(GetPlayerState()))
            {
                MyPlayerState->SetLevel(SaveData->PlayerLevel);
                MyPlayerState->SetXP(SaveData->XP);
                MyPlayerState->SetAttributePoints(SaveData->AttributePoints);
                MyPlayerState->SetSpellPoints(SaveData->SpellPoints);
            }

            UMyAbilitySystemLibrary::InitializeDefaultAttributesFromSaveData(this, AbilitySystemComponent, SaveData);
            // Discover abilities added to the catalog after this save was created.
            // Existing specs (including their rank and equipped slot) remain unchanged.
            if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
            {
                MyASC->UpdateAbilityStatuses(SaveData->PlayerLevel);
            }
        }
        if (TravelState)
            TravelState->GetEquipmentComponent()->ReconcileEffects();
        // 装备属性恢复完成后再还原旅行资源，随后消费临时旅行快照。
        if (bFromTravel)
        {
            AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(),
                                                            TravelState->GetTravelHealth());
            AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(),
                                                            TravelState->GetTravelMana());
            TravelState->ClearTravelProgress();
        }
    }
}
// 累加经验并通过玩家状态通知订阅者。
void APlayerCharacter::AddToXP_Implementation(int32 InXP)
{
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    MyPlayerState->AddToXP(InXP);
}
// 通过等级数据表将经验转换为等级。
int32 APlayerCharacter::FindLevelForXP_Implementation(int32 InXP) const
{
    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->LevelUpInfo->FindLevelForXP(InXP);
}
// 触发全端升级粒子表现。
void APlayerCharacter::LevelUp_Implementation()
{
    MulticastLevelUpParticles();
}

// 令升级粒子朝向摄像机后重新激活。
void APlayerCharacter::MulticastLevelUpParticles_Implementation() const
{
    if (IsValid(LevelUpNiagaraComponent))
    {
        const FVector CameraLocation = TopDownCameraComponent->GetComponentLocation();
        const FVector NiagaraSystemLocation = LevelUpNiagaraComponent->GetComponentLocation();
        const FRotator ToCameraRotation = (CameraLocation - NiagaraSystemLocation).Rotation();
        LevelUpNiagaraComponent->SetWorldRotation(ToCameraRotation);
        LevelUpNiagaraComponent->Activate(true);
    }
}
// 读取累计经验。
int32 APlayerCharacter::GetXP_Implementation() const
{
    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->GetXP();
}
// 查表取得指定等级的属性点奖励。
int32 APlayerCharacter::GetAttributePointsReward_Implementation(int32 Level) const
{

    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->LevelUpInfo->LevelUpInformation[Level].AttributePointAward;
}

// 查表取得指定等级的技能点奖励。
int32 APlayerCharacter::GetSpellPointsReward_Implementation(int32 Level) const
{

    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->LevelUpInfo->LevelUpInformation[Level].SpellPointAward;
}

// 累加玩家等级，并刷新当前等级可学习的能力状态。
void APlayerCharacter::AddToPlayerLevel_Implementation(int32 InPlayerLevel)
{
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    MyPlayerState->AddToLevel(InPlayerLevel);

    if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(GetAbilitySystemComponent()))
    {
        MyASC->UpdateAbilityStatuses(MyPlayerState->GetPlayerLevel());
    }
}

// 累加属性点并通知订阅者。
void APlayerCharacter::AddToAttributePoints_Implementation(int32 InAttributePoints)
{
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    MyPlayerState->AddToAttributePoints(InAttributePoints);
}

// 累加技能点并通知订阅者。
void APlayerCharacter::AddToSpellPoints_Implementation(int32 InSpellPoints)
{
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    MyPlayerState->AddToSpellPoints(InSpellPoints);
}

// 读取可用属性点。
int32 APlayerCharacter::GetAttributePoints_Implementation() const
{

    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->GetAttributePoints();
}

// 读取可用技能点。
int32 APlayerCharacter::GetSpellPoints_Implementation() const
{

    const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    check(MyPlayerState);
    return MyPlayerState->GetSpellPoints();
}

// 同步眩晕输入封锁标签与特效，并调用基类刷新速度。
void APlayerCharacter::OnRep_Stunned()
{
    Super::OnRep_Stunned();
    if (UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent))
    {
        const FMyGameplayTags& GameplayTags = FMyGameplayTags::Get();
        FGameplayTagContainer BlockedTags;
        BlockedTags.AddTag(GameplayTags.Player_Block_CursorTrace);
        BlockedTags.AddTag(GameplayTags.Player_Block_InputHeld);
        BlockedTags.AddTag(GameplayTags.Player_Block_InputPressed);
        BlockedTags.AddTag(GameplayTags.Player_Block_InputReleased);
        if (bIsStunned)
        {
            MyASC->AddLooseGameplayTags(BlockedTags);
            StunDebuffComponent->Activate();
        }
        else
        {
            MyASC->RemoveLooseGameplayTags(BlockedTags);
            StunDebuffComponent->Deactivate();
        }
    }
}

// 依据复制燃烧状态启停燃烧特效。
void APlayerCharacter::OnRep_Burned()
{
    if (bIsBurned)
    {
        BurnDebuffComponent->Activate();
    }
    else
    {
        BurnDebuffComponent->Deactivate();
    }
}

// 创建或显示技能范围指示器，使用可选贴花材质。
void APlayerCharacter::ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial)
{
    if (AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController()))
    {
        MyPlayerController->ShowMagicCircle(DecalMaterial);
        MyPlayerController->bShowMouseCursor = false;
    }
}

// 移除技能范围指示器。
void APlayerCharacter::HideMagicCircle_Implementation()
{
    if (AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController()))
    {
        MyPlayerController->HideMagicCircle();
        MyPlayerController->bShowMouseCursor = true;
    }
}
// 仅服务器写入检查点进度，并交给游戏模式持久化。
void APlayerCharacter::SaveProgress_Implementation(const FName& CheckpointTag)
{
    if (!HasAuthority())
        return;
    AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
    if (MyGameMode)
    {
        ULoadScreenSaveGame* SaveData = MyGameMode->RetrieveInGameSaveData();
        if (SaveData == nullptr)
            return;
        WriteProgressToSaveData(SaveData, CheckpointTag);
        MyGameMode->SaveInGameProgressData(SaveData);
    }
}

// 保存基础属性与可保存技能；排除装备授予能力，保留装备接管前的技能槽位。
void APlayerCharacter::WriteProgressToSaveData(ULoadScreenSaveGame* SaveData, const FName& CheckpointTag)
{
    if (!HasAuthority() || !SaveData)
        return;

    SaveData->PlayerStartTag = CheckpointTag;

    if (AMyPlayerState* MyPlayerState = Cast<AMyPlayerState>(GetPlayerState()))
    {
        SaveData->PlayerLevel = MyPlayerState->GetPlayerLevel();
        SaveData->XP = MyPlayerState->GetXP();
        SaveData->AttributePoints = MyPlayerState->GetAttributePoints();
        SaveData->SpellPoints = MyPlayerState->GetSpellPoints();
    }
    SaveData->Strength = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetStrengthAttribute());
    SaveData->Intelligence =
        AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetIntelligenceAttribute());
    SaveData->Resilience = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute());
    SaveData->Vigor = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetVigorAttribute());

    SaveData->bFirstTimeLoadIn = false;

    if (!HasAuthority())
        return;

    UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent);
    FForEachAbility SaveAbilityDelegate;
    SaveData->SavedAbilities.Empty();
    // 技能存档记录持久化配置；装备专属能力由装备恢复流程重新授予。
    SaveAbilityDelegate.BindLambda(
        [this, MyASC, SaveData](const FGameplayAbilitySpec& AbilitySpec)
        {
            auto* EquipmentState = GetPlayerState<AMyPlayerState>();
            auto* Equipment = EquipmentState ? EquipmentState->GetEquipmentComponent() : nullptr;
            if (Equipment && Equipment->IsWeaponAbility(AbilitySpec.Handle))
                return;
            if (const auto* Ability = Cast<UMyGameplayAbility>(AbilitySpec.Ability);
                Ability && Ability->IsEquipmentGrantedOnly())
                return;
            const FGameplayTag AbilityTag = MyASC->GetAbilityTagFromSpec(AbilitySpec);
            UAbilityInfo* AbilityInfo = UMyAbilitySystemLibrary::GetAbilityInfo(this);
            FMyAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);

            FSavedAbility SavedAbility;
            SavedAbility.GameplayAbility = Info.Ability;
            SavedAbility.AbilityLevel = AbilitySpec.Level;
            SavedAbility.AbilitySlot =
                Equipment ? Equipment->GetSavedInputSlot(AbilitySpec) : MyASC->GetSlotFromAbilityTag(AbilityTag);
            SavedAbility.AbilityStatus = SavedAbility.AbilitySlot.IsValid()
                                             ? FMyGameplayTags::Get().Abilities_Status_Equipped
                                             : MyASC->GetStatusFromAbilityTag(AbilityTag);
            SavedAbility.AbilityTag = AbilityTag;
            SavedAbility.AbilityType = Info.AbilityType;

            SaveData->SavedAbilities.AddUnique(SavedAbility);
        });
    MyASC->ForEachAbility(SaveAbilityDelegate);
}

// 取得玩家状态拥有的背包组件。
UBackpackComponent* APlayerCharacter::GetBackpackComponent() const
{
    if (AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>())
    {
        return MyPlayerState->GetBackpackComponent();
    }
    return nullptr;
}
