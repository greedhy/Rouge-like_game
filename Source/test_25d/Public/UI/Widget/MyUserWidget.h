// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MyUserWidget.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMyUserWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    //蓝图调用方法，可以设置WidgetController
    UFUNCTION(BlueprintCallable)
    void SetWidgetController(UObject* InWidgetController);
    //WidgetController
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UObject> WidgetController;

protected:
    UFUNCTION(BlueprintImplementableEvent)
    void WidgetControllerSet();
};
