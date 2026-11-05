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
#include"AbilitySystem/Data/LevelUpInfo.h"
#include"NiagaraComponent.h"
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
APlayerCharacter::APlayerCharacter()
{
	LevelUpNiagaraComponent=CreateDefaultSubobject<UNiagaraComponent>("LevelUpNiagaraComponent");
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

void APlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Interaction belongs to the player body, independent of a cosmetic mesh's physics asset.
	// Apply after Blueprint defaults so existing player variants inherit the same contract.
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);
}

void APlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitAbilityActorInfo();
	LoadProgress();
	if (auto* State = GetPlayerState<AMyPlayerState>()) State->GetEquipmentComponent()->ReconcileEffects();
	//AddCharacterAbilities();

	if (AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this)))
	{
		MyGameMode->LoadWorldState(GetWorld());
	}
}

void APlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilityActorInfo();

}

int32 APlayerCharacter::GetPlayerLevel_Implementation()
{
	AMyPlayerState* Player_State = GetPlayerState<AMyPlayerState>();
	check(Player_State);
	return Player_State->GetPlayerLevel();
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(DeathTimer);
	Super::EndPlay(Reason);
}

void APlayerCharacter::Die(const FVector& DeathImpulse)
{
	Super::Die(DeathImpulse);

	FTimerDelegate DeathTimerDelegate;
	DeathTimerDelegate.BindWeakLambda(this, [this]()
		{
			AMyGameModeBase* MyGM = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
			if (MyGM)
			{
				MyGM->PlayerDied(this);
			}
		});
	GetWorldTimerManager().SetTimer(DeathTimer, DeathTimerDelegate, DeathTime, false);
	TopDownCameraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
}

void APlayerCharacter::InitAbilityActorInfo()
{	
	AMyPlayerState* Player_State = GetPlayerState<AMyPlayerState>();
	check(Player_State);
	Player_State->GetAbilitySystemComponent()->InitAbilityActorInfo(Player_State, this);
 Player_State->GetEquipmentComponent()->RefreshWeaponVisuals();
	UMyAbilitySystemComponent * UmyASC = Cast<UMyAbilitySystemComponent>(Player_State->GetAbilitySystemComponent());
	if (UmyASC)
	{
		UmyASC->AbilityActorInfoSet();
	}

	AbilitySystemComponent = Player_State->GetAbilitySystemComponent();
	AttributeSet = Player_State->GetAttributeSet();
	OnASCRegistered.Broadcast(AbilitySystemComponent);

	AbilitySystemComponent->RegisterGameplayTagEvent(FMyGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved).
	AddUObject(this, &ABaseCharacter::StunTagChanged);



	AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController());
	if (MyPlayerController) {
		AMyHUD* MyHUD = Cast<AMyHUD>(MyPlayerController->GetHUD());
		if (MyHUD) {
			MyHUD->InitOverlay(MyPlayerController, Player_State, AbilitySystemComponent, AttributeSet);
		}
	}

}
void APlayerCharacter::LoadProgress()
{
	AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (MyGameMode)
	{
		AMyPlayerState* TravelState = GetPlayerState<AMyPlayerState>();
		const bool bFromTravel = TravelState && TravelState->GetTravelProgress();
		ULoadScreenSaveGame* SaveData = bFromTravel ? TravelState->GetTravelProgress() : MyGameMode->RetrieveInGameSaveData();
		if (SaveData == nullptr) return;

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
		if (TravelState) TravelState->GetEquipmentComponent()->ReconcileEffects();
		if (bFromTravel)
		{
			AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), TravelState->GetTravelHealth());
			AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), TravelState->GetTravelMana());
			TravelState->ClearTravelProgress();
		}
	}
}
void APlayerCharacter::AddToXP_Implementation(int32 InXP)
{
	AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	MyPlayerState->AddToXP(InXP);
}
int32 APlayerCharacter::FindLevelForXP_Implementation(int32 InXP) const
{
	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->LevelUpInfo->FindLevelForXP(InXP);
}
void APlayerCharacter::LevelUp_Implementation()
{
	MulticastLevelUpParticles();

}

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
int32 APlayerCharacter::GetXP_Implementation() const
{
	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->GetXP();
}
int32 APlayerCharacter::GetAttributePointsReward_Implementation(int32 Level) const
{

	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->LevelUpInfo->LevelUpInformation[Level].AttributePointAward;
}

int32 APlayerCharacter::GetSpellPointsReward_Implementation(int32 Level) const
{

	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->LevelUpInfo->LevelUpInformation[Level].SpellPointAward;
}
	
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

void APlayerCharacter::AddToAttributePoints_Implementation(int32 InAttributePoints)
{
	AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	MyPlayerState->AddToAttributePoints(InAttributePoints);
}

void APlayerCharacter::AddToSpellPoints_Implementation(int32 InSpellPoints)
{
	AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	MyPlayerState->AddToSpellPoints(InSpellPoints);
}

int32 APlayerCharacter::GetAttributePoints_Implementation() const
{
	
	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->GetAttributePoints();
}

int32 APlayerCharacter::GetSpellPoints_Implementation() const
{
	
	const AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
	check(MyPlayerState);
	return MyPlayerState->GetSpellPoints();
}

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

void APlayerCharacter::ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial)
{
	if (AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController()))
	{
		MyPlayerController->ShowMagicCircle(DecalMaterial);
		MyPlayerController->bShowMouseCursor = false;
	}
}

void APlayerCharacter::HideMagicCircle_Implementation()
{
	if (AMyPlayerController* MyPlayerController = Cast<AMyPlayerController>(GetController()))
	{
		MyPlayerController->HideMagicCircle();
		MyPlayerController->bShowMouseCursor = true;
	}
}
void APlayerCharacter::SaveProgress_Implementation(const FName& CheckpointTag)
{
	if (!HasAuthority()) return;
	AMyGameModeBase* MyGameMode = Cast<AMyGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (MyGameMode)
	{
		ULoadScreenSaveGame* SaveData = MyGameMode->RetrieveInGameSaveData();
		if (SaveData == nullptr) return;
		WriteProgressToSaveData(SaveData, CheckpointTag);
		MyGameMode->SaveInGameProgressData(SaveData);
	}
}

void APlayerCharacter::WriteProgressToSaveData(ULoadScreenSaveGame* SaveData, const FName& CheckpointTag)
{
	if (!HasAuthority() || !SaveData) return;

		SaveData->PlayerStartTag = CheckpointTag;

		if (AMyPlayerState* MyPlayerState = Cast<AMyPlayerState>(GetPlayerState()))
		{
			SaveData->PlayerLevel = MyPlayerState->GetPlayerLevel();
			SaveData->XP = MyPlayerState->GetXP();
			SaveData->AttributePoints = MyPlayerState->GetAttributePoints();
			SaveData->SpellPoints = MyPlayerState->GetSpellPoints();
		}
		SaveData->Strength = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetStrengthAttribute());
		SaveData->Intelligence = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetIntelligenceAttribute());
		SaveData->Resilience = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetResilienceAttribute());
		SaveData->Vigor = AbilitySystemComponent->GetNumericAttributeBase(UMyAttributeSet::GetVigorAttribute());

		SaveData->bFirstTimeLoadIn = false;

		if (!HasAuthority()) return;

		UMyAbilitySystemComponent* MyASC = Cast<UMyAbilitySystemComponent>(AbilitySystemComponent);
		FForEachAbility SaveAbilityDelegate;
		SaveData->SavedAbilities.Empty();
		SaveAbilityDelegate.BindLambda([this, MyASC, SaveData](const FGameplayAbilitySpec& AbilitySpec)
			{
                auto* EquipmentState=GetPlayerState<AMyPlayerState>();
                auto* Equipment=EquipmentState?EquipmentState->GetEquipmentComponent():nullptr;
                if(Equipment && Equipment->IsWeaponAbility(AbilitySpec.Handle)) return;
                if(const auto* Ability=Cast<UMyGameplayAbility>(AbilitySpec.Ability);Ability && Ability->IsEquipmentGrantedOnly()) return;
				const FGameplayTag AbilityTag = MyASC->GetAbilityTagFromSpec(AbilitySpec);
				UAbilityInfo* AbilityInfo = UMyAbilitySystemLibrary::GetAbilityInfo(this);
				FMyAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);

				FSavedAbility SavedAbility;
				SavedAbility.GameplayAbility = Info.Ability;
				SavedAbility.AbilityLevel = AbilitySpec.Level;
				SavedAbility.AbilitySlot = Equipment?Equipment->GetSavedInputSlot(AbilitySpec):MyASC->GetSlotFromAbilityTag(AbilityTag);
				SavedAbility.AbilityStatus = SavedAbility.AbilitySlot.IsValid()?FMyGameplayTags::Get().Abilities_Status_Equipped:MyASC->GetStatusFromAbilityTag(AbilityTag);
				SavedAbility.AbilityTag = AbilityTag;
				SavedAbility.AbilityType = Info.AbilityType;

				SaveData->SavedAbilities.AddUnique(SavedAbility);

			});
		MyASC->ForEachAbility(SaveAbilityDelegate);

}

UBackpackComponent* APlayerCharacter::GetBackpackComponent() const
{
	if (AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>())
	{
		return MyPlayerState->GetBackpackComponent();
	}
	return nullptr;
}
