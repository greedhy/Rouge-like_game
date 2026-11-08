// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Extensions/UserWidgetExtension.h"

#include "Components/Button.h"

#include "Components/UniformGridPanel.h"
#include "BackpackWidget.generated.h"

class UMVVM_Backpack;

/**
 * 
 */

UCLASS()
class TEST_25D_API UBackpackWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Backpack")
    void SetBackpackViewModel(UMVVM_Backpack* InBackpackViewModel);

    UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Backpack")
    TObjectPtr<UMVVM_Backpack> BackpackViewModel;

    ////BackPackUI.h

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BackPackSytstem", meta = (BindWidget))
    //TObjectPtr<UUniformGridPanel> BackPackPanel;  // 统一网格面板
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BackPackSytstem", meta = (BindWidget))
    //TObjectPtr<UButton> CloseButton;  // 关闭背包按钮

    //int32 SlotCount = 16;  //背包允许存在格子数量（可以暴露给蓝图然后自己酌情修改）

    ////operate

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BackPackSytstem", meta = (BindWidget))
    //TObjectPtr<UButton> UseButton;
    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BackPackSytstem", meta = (BindWidget))
    //TObjectPtr<UButton> ThrowButton;

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BackPackSytstem")
    //int32 SlotIndex;  // 在使用物品时，通过这个SlotIndex去定位数组中的这个物品

    //UFUNCTION(BlueprintCallable, Category = "BackPackSystem")
    //void OnUseButtonClick();
    //UFUNCTION(BlueprintCallable, Category = "BackPackSystem")
    //void OnThrowButtonClick();
};
