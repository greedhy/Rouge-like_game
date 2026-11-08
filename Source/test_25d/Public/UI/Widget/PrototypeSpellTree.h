#pragma once
#include "CoreMinimal.h"
#include "UI/Widget/MyUserWidget.h"
#include "GameplayTagContainer.h"
#include "PrototypeSpellTree.generated.h"

class UPrototypeSpellTree;
class UWidget;
class UTexture2D;

UCLASS()
class TEST_25D_API UPrototypeSpellEntry : public UUserWidget
{
    GENERATED_BODY()
public:
    FGameplayTag AbilityTag;
    FText Label;
    UPROPERTY()
    TObjectPtr<UTexture2D> Icon;
    UPROPERTY()
    TObjectPtr<UPrototypeSpellTree> Tree;

protected:
    virtual void NativeOnInitialized() override;
    UFUNCTION()
    void Select();
};

/** Adds a compact second page to the existing offensive tree without changing its Blueprint wiring. */
UCLASS()
class TEST_25D_API UPrototypeSpellTree : public UMyUserWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UPROPERTY()
    TObjectPtr<UWidget> OriginalPage;
    UPROPERTY()
    TObjectPtr<UWidget> PrototypePage;
    UFUNCTION()
    void ShowOriginal();
    UFUNCTION()
    void ShowPrototypes();
    UFUNCTION()
    void ShowWeapons();
    void RefreshWeapons();
    UPROPERTY()
    TObjectPtr<UWidget> WeaponPage;
    UPROPERTY()
    TObjectPtr<class UUniformGridPanel> WeaponGrid;
    UPROPERTY()
    TObjectPtr<class UWeaponSetConfig> ListedWeaponSet;
    UPROPERTY()
    TObjectPtr<UObject> ListedController;
    float WeaponRefreshTime = 0.f;
};
