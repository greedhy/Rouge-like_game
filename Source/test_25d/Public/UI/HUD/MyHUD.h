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

    // 属性、装备、技能和技能组装共用一个窗口，索引依次为 0..3。
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ShowCharacterMenu(int32 PageIndex);
    /** 读取当前 HUD 持有的唯一窗口，不隐式创建或修改预览。 */
    UFUNCTION(BlueprintPure, Category = "UI")
    UCharacterMenuWidget* GetCharacterMenu() const
    {
        return CharacterMenu;
    }

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
