// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"

#include "UI/WidgetController/MyWidgetController.h"
#include "AbilitySystem/Data/AttributeInfo.h"
#include "AttributeMenuWidgetController.generated.h"

class UAttributeInfo;
struct FGameplayTag;
struct FMyAttributeInfo;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMyAttributeInfoSignature, const FMyAttributeInfo&, Info);
/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class TEST_25D_API UAttributeMenuWidgetController : public UMyWidgetController
{
    GENERATED_BODY()

public:
    virtual void BroadcastInitialValues() override;
    virtual void BindCallbacksToDependencies() override;

    UPROPERTY(BlueprintAssignable, Category = "GAS|Attributes")
    FMyAttributeInfoSignature AttributeInfoDelegate;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
    TObjectPtr<UAttributeInfo> AttributeInfo;

    UPROPERTY(BlueprintAssignable, Category = "GAS|Attributes")
    FOnPlayerStatChangedSignature AttributePointsChangedDelegate;

    UFUNCTION(BlueprintCallable)
    void UpgradeAttribute(const FGameplayTag& AttributeTag);

protected:
private:
    void BroadcastAttributeInfo(const FGameplayTag& AttributeTag, const FGameplayAttribute& Attribute) const;
};
