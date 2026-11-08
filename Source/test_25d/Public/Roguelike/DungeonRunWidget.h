#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DungeonRunWidget.generated.h"

class UTextBlock;
class UButton;
class UCanvasPanel;
class ADungeonRunDirector;

/** Native run HUD, with geometry limited to shared visited rooms. */
UCLASS()
class TEST_25D_API UDungeonRunWidget : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void Refresh();
    void RefreshMap(const ADungeonRunDirector* Director, FName CurrentRoom);
    UFUNCTION()
    void BuyFirst();
    UFUNCTION()
    void BuySecond();
    UFUNCTION()
    void BuyThird();
    UFUNCTION()
    void LeaveRun();
    void Purchase(int32 Index);
    UFUNCTION()
    FEventReply ConsumePanelPointer(FGeometry Geometry, const FPointerEvent& MouseEvent);
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> MapText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> MessageText;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UButton>> OfferButtons;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> OfferLabels;
    UPROPERTY(Transient)
    TObjectPtr<UButton> ReturnButton;
    UPROPERTY(Transient)
    TObjectPtr<UCanvasPanel> MapCanvas;
    FString MapSignature;
    float RefreshRemaining = 0.f;
};
