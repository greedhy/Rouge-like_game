// 调用链：LoadScreenHUD 注入存档 VM → 主视图连接三个槽位 → 槽位视图接收状态并转发按钮操作。

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Widget/MyUserWidget.h"
#include "LoadScreenWidget.generated.h"

class UButton;
class UEditableText;
class USizeBox;
class UWidgetSwitcher;
class UMVVM_LoadScreen;
class UMVVM_LoadSlot;

/** 接收确认框按钮并广播结果，布局来自现有 Widget Blueprint。 */
UCLASS()
class TEST_25D_API ULoadScreenDeleteConfirmation : public UMyUserWidget
{
    GENERATED_BODY()

public:
    FSimpleDelegate OnConfirmed;
    FSimpleDelegate OnCancelled;

    float GetFrameWidth() const;

protected:
    virtual void NativeOnInitialized() override;

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UUserWidget> Button_Delete;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UUserWidget> Button_Cancel;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<USizeBox> SIzeBox_Root;

    UFUNCTION()
    void Confirm();

    UFUNCTION()
    void Cancel();
};

/** 现有蓝图共享同一父类，设计器控件决定该实例承担主视图、切换器或槽位页的角色。 */
UCLASS()
class TEST_25D_API ULoadScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 在 HUD 添加主视图后、加载存档数据前连接所有视图。 */
    void InitializeSaveScreen(UMVVM_LoadScreen* InScreenModel);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void InitializeSlot(UMVVM_LoadScreen* InScreenModel, UMVVM_LoadSlot* InSlotModel);
    void BindModelDelegates();
    void UnbindModelDelegates();
    void UpdateScreenButtons();
    void CloseConfirmation();
    void ConfirmDelete();
    void CancelDelete();

    UFUNCTION()
    void OnSlotSelected();

    UFUNCTION()
    void SetSlotPage(int32 Index);

    UFUNCTION()
    void SetSelectButtonEnabled(bool bEnabled);

    UFUNCTION()
    void StartNewSlot();

    UFUNCTION()
    void SubmitName();

    UFUNCTION()
    void SelectSlot();

    UFUNCTION()
    void PlaySelectedSlot();

    UFUNCTION()
    void AskToDelete();

    UFUNCTION()
    void ReturnToMainMenu();

    UPROPERTY(EditDefaultsOnly, Category = "Save Screen")
    TSubclassOf<ULoadScreenDeleteConfirmation> DeleteConfirmationClass;

    UPROPERTY(EditDefaultsOnly, Category = "Save Screen")
    TSoftObjectPtr<UWorld> ReturnMenuLevel;

    UPROPERTY(Transient)
    TObjectPtr<UMVVM_LoadScreen> ScreenModel;

    UPROPERTY(Transient)
    TObjectPtr<UMVVM_LoadSlot> SlotModel;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenDeleteConfirmation> Confirmation;

    bool bHasSelection = false;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> PlayButtonWidget;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> DeleteButtonWidget;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> ReturnButtonWidget;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> FirstSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> SecondSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> ThirdSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> SlotSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> VacantPage;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> EnterNamePage;

    UPROPERTY(Transient)
    TObjectPtr<ULoadScreenWidget> TakenPage;

    UPROPERTY(Transient)
    TObjectPtr<UButton> NewGameButton;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> NewSlotButtonWidget;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> SelectSlotButtonWidget;

    UPROPERTY(Transient)
    TObjectPtr<UEditableText> NameEntry;
};
