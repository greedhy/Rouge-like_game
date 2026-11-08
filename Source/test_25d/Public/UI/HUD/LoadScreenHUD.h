// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LoadScreenHUD.generated.h"

class UMVVM_LoadScreen;
class ULoadScreenWidget;
class URoomMenuWidget;
/**
 * 
 */
UCLASS()
class TEST_25D_API ALoadScreenHUD : public AHUD
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void ShowModeSelection();
    UFUNCTION(BlueprintCallable)
    void ShowSaveSelection();
    UPROPERTY(EditDefaultsOnly, Category = "Rooms")
    TSubclassOf<URoomMenuWidget> RoomMenuWidgetClass;
    UPROPERTY(BlueprintReadOnly, Category = "Rooms")
    TObjectPtr<URoomMenuWidget> RoomMenuWidget;

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UUserWidget> LoadScreenWidgetClass;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<ULoadScreenWidget> LoadScreenWidget;

    UPROPERTY(EditDefaultsOnly)
    TSubclassOf<UMVVM_LoadScreen> LoadScreenViewModelClass;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UMVVM_LoadScreen> LoadScreenViewModel;

protected:
    virtual void BeginPlay() override;
};
