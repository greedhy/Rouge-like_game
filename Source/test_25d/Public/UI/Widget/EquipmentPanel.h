#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Styling/SlateBrush.h"
#include "EquipmentPanel.generated.h"
class UEquipmentComponent;
class UMVVM_Backpack;
class UInventoryModel;
class UTexture2D;
class SVerticalBox;

/** Native companion to the backpack; occupies only its own viewport rectangle. */
UCLASS()
class TEST_25D_API UEquipmentPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeEquipment(UEquipmentComponent* InEquipment, UMVVM_Backpack* InBackpack, UInventoryModel* InModel);
    static bool CanEquipToSlot(const UInventoryModel* Model, int32 Index, FGameplayTag Slot);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    UPROPERTY()
    TObjectPtr<UEquipmentComponent> Equipment;
    UPROPERTY()
    TObjectPtr<UMVVM_Backpack> Backpack;
    UPROPERTY()
    TObjectPtr<UInventoryModel> Model;
    UPROPERTY()
    TObjectPtr<UTexture2D> BackgroundTexture;
    UPROPERTY()
    TArray<TObjectPtr<UTexture2D>> IconTextures;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<SVerticalBox> ElementRows;
    TArray<TSharedPtr<FSlateBrush>> IconBrushes;
    FSlateBrush BackgroundBrush;
    FText Status;
    UFUNCTION()
    void RefreshEquipment();
    UFUNCTION()
    void OnResult(bool bSucceeded);
    void Bind();
    void Unbind();
};
