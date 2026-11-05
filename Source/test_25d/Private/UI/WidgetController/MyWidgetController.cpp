// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/WidgetController/MyWidgetController.h"
#include "Player/MyPlayerController.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
void UMyWidgetController::SetWidgetControllerParams(const FWidgetControllerParams& WCParams)
{
    UnbindCallbacksFromDependencies();
    MyPlayerController=nullptr; MyPlayerState=nullptr; MyASC=nullptr; MyAttributeSet=nullptr;
    PlayerController = WCParams.PlayerController;
    PlayerState = WCParams.PlayerState;
    ASC = WCParams.AbilitySystemComponent;
    AttributeSet = WCParams.AttributeSet;

}

void UMyWidgetController::BroadcastInitialValues()
{
}

void UMyWidgetController::BindCallbacksToDependencies()
{
}
void UMyWidgetController::BroadcastAbilityInfo()
{
	if (!GetMyASC()->bStartupAbilityGiven) return;

	FForEachAbility BroadcastDelegate;
	BroadcastDelegate.BindLambda([this](const FGameplayAbilitySpec& AbilitySpec)
		{
			FMyAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(MyASC->GetAbilityTagFromSpec(AbilitySpec));
			Info.InputTag = MyASC->GetInputTagFromSpec(AbilitySpec);
			Info.StatusTag = MyASC->GetStatusFromSpec(AbilitySpec);
			AbilityInfoDelegate.Broadcast(Info);
		});
	GetMyASC()->ForEachAbility(BroadcastDelegate);
}

AMyPlayerController* UMyWidgetController::GetMyPC()
{
	if (MyPlayerController == nullptr)
	{
		MyPlayerController = Cast<AMyPlayerController>(PlayerController);
	}
	return MyPlayerController;
}

AMyPlayerState* UMyWidgetController::GetMyPS()
{
	if (MyPlayerState == nullptr)
	{
		MyPlayerState = Cast<AMyPlayerState>(PlayerState);
	}
	return MyPlayerState;
}

UMyAbilitySystemComponent* UMyWidgetController::GetMyASC()
{
	if (MyASC == nullptr)
	{
		MyASC = Cast<UMyAbilitySystemComponent>(ASC);
	}
	return MyASC;
}

UMyAttributeSet* UMyWidgetController::GetMyAS()
{
	if (MyAttributeSet == nullptr)
	{
		MyAttributeSet = Cast<UMyAttributeSet>(AttributeSet);
	}
	return MyAttributeSet;
}

void UMyWidgetController::BeginDestroy()
{ UnbindCallbacksFromDependencies(); Super::BeginDestroy(); }
