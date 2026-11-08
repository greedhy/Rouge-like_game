// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/WidgetController/AttributeMenuWidgetController.h"
#include "MyGameplayTags.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"

#include "AbilitySystem/MyAttributeSet.h"
#include "Player/MyPlayerState.h"

void UAttributeMenuWidgetController::BroadcastInitialValues()
{
    check(AttributeInfo);
    for (auto& pair : GetMyAS()->TagsToAttributes)
    {
        BroadcastAttributeInfo(pair.Key, pair.Value());
    }
    AttributePointsChangedDelegate.Broadcast(GetMyPS()->GetAttributePoints());
}

void UAttributeMenuWidgetController::BindCallbacksToDependencies()
{
    check(AttributeInfo);

    for (auto& pair : GetMyAS()->TagsToAttributes)
    {
        UAbilitySystemComponent* AbilitySystemComponent = GetMyAS()->GetOwningAbilitySystemComponent();
        AbilitySystemComponent->UAbilitySystemComponent::GetGameplayAttributeValueChangeDelegate(pair.Value())
            .AddLambda(
                [this, pair](const FOnAttributeChangeData& Data)
                {
                    BroadcastAttributeInfo(pair.Key, pair.Value());
                });
    }

    GetMyPS()->OnAttributePointsChangedDelegate.AddLambda(
        [this](int32 Points)
        {
            AttributePointsChangedDelegate.Broadcast(Points);
        });
}

void UAttributeMenuWidgetController::BroadcastAttributeInfo(const FGameplayTag& AttributeTag,
                                                            const FGameplayAttribute& Attribute) const
{
    FMyAttributeInfo Info = AttributeInfo->FindAttributeInfoForTag(AttributeTag);
    Info.AttributeValue = Attribute.GetNumericValue(AttributeSet);
    AttributeInfoDelegate.Broadcast(Info);
}

void UAttributeMenuWidgetController::UpgradeAttribute(const FGameplayTag& AttributeTag)
{
    UMyAbilitySystemComponent* AuraASC = CastChecked<UMyAbilitySystemComponent>(ASC);
    AuraASC->UpgradeAttribute(AttributeTag);
}
