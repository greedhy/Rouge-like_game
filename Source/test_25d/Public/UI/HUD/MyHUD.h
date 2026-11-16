// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UI/Widget/MyUserWidget.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "UI/WidgetController/AttributeMenuWidgetController.h"
#include "MyHUD.generated.h"

class USpellMenuWidgetController;
class UCharacterMenuWidget;
struct FWidgetControllerParams;
/**
 * 
 */
UCLASS()
class TEST_25D_API AMyHUD : public AHUD
{
    GENERATED_BODY()

public:
    AMyHUD();

    UPROPERTY()
    TObjectPtr<UMyUserWidget> OverlayWidget;

    UOverlayWidgetController* GetOverlayWidgetController(const FWidgetControllerParams& WCParams);

    UAttributeMenuWidgetController* GetAttributeMenuWidgetController(const FWidgetControllerParams& WCParams);

    USpellMenuWidgetController* GetSpellMenuWidgetController(const FWidgetControllerParams& WCParams);

    void InitOverlay(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS);

    // 属性、装备和技能共用一个窗口，调用方只指定要显示的页面。
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ShowCharacterMenu(int32 PageIndex);

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void ShowAttributes();

    UFUNCTION()
    void ShowSkills();

    UPROPERTY(Transient)
    TObjectPtr<UCharacterMenuWidget> CharacterMenu;

    UPROPERTY()
    TSubclassOf<UUserWidget> AttributePageClass;

    UPROPERTY()
    TSubclassOf<UUserWidget> SkillPageClass;

    UPROPERTY(EditAnywhere, Category = "Widgets")
    TSubclassOf<UMyUserWidget> OverlayWidgetClass;

    UPROPERTY()
    TObjectPtr<UOverlayWidgetController> OverlayWidgetController;

    UPROPERTY(EditAnywhere)
    TSubclassOf<UOverlayWidgetController> OverlayWidgetControllerClass;

    UPROPERTY()
    TObjectPtr<UAttributeMenuWidgetController> AttributeMenuWidgetController;

    UPROPERTY(EditAnywhere)
    TSubclassOf<UAttributeMenuWidgetController> AttributeMenuWidgetControllerClass;

    UPROPERTY()
    TObjectPtr<USpellMenuWidgetController> SpellMenuWidgetController;

    UPROPERTY(EditAnywhere)
    TSubclassOf<USpellMenuWidgetController> SpellMenuWidgetControllerClass;
};
