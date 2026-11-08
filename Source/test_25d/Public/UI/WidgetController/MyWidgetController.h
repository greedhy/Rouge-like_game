// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MyWidgetController.generated.h"

class UAbilityInfo;
class UMyAttributeSet;
class UMyAbilitySystemComponent;
class AMyPlayerState;
class AMyPlayerController;
class UAbilitySystemComponent;
class UAttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStatChangedSignature, int32, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityInfoSignature, const FMyAbilityInfo&, Info);

USTRUCT(BlueprintType)
struct FWidgetControllerParams
{
    GENERATED_BODY()
    FWidgetControllerParams() {};
    FWidgetControllerParams(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS)
        : PlayerController(PC), PlayerState(PS), AbilitySystemComponent(ASC), AttributeSet(AS) {};

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<APlayerController> PlayerController = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<APlayerState> PlayerState = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UAttributeSet> AttributeSet = nullptr;
};

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyWidgetController : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void SetWidgetControllerParams(const FWidgetControllerParams& WCParams);

    UFUNCTION(BlueprintCallable)
    virtual void BroadcastInitialValues();
    virtual void BindCallbacksToDependencies();
    virtual void UnbindCallbacksFromDependencies()
    {
    }
    virtual void BeginDestroy() override;

    UPROPERTY(BlueprintAssignable, Category = "GAS|Messages")
    FAbilityInfoSignature AbilityInfoDelegate;

    void BroadcastAbilityInfo();

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Widget Data")
    TObjectPtr<UAbilityInfo> AbilityInfo;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<APlayerController> PlayerController;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<APlayerState> PlayerState;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<UAbilitySystemComponent> ASC;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<UAttributeSet> AttributeSet;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<AMyPlayerController> MyPlayerController;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<AMyPlayerState> MyPlayerState;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<UMyAbilitySystemComponent> MyASC;

    UPROPERTY(BlueprintReadOnly, Category = "WidgetController")
    TObjectPtr<UMyAttributeSet> MyAttributeSet;

    AMyPlayerController* GetMyPC();
    AMyPlayerState* GetMyPS();
    UMyAbilitySystemComponent* GetMyASC();
    UMyAttributeSet* GetMyAS();
};
