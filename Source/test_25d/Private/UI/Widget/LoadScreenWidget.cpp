// 调用链：HUD 初始化存档界面 → 绑定现有 WidgetTree 和 ViewModel → 按钮转发存档操作 → 委托更新视图。

#include "UI/Widget/LoadScreenWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MVVMSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "View/MVVMView.h"

namespace
{
    // 宽按钮蓝图提供样式和实际接收点击的原生 Button。
    UButton* GetWideButton(UUserWidget* Widget)
    {
        return Widget ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("Button"))) : nullptr;
    }
}

void ULoadScreenDeleteConfirmation::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (UButton* Button = GetWideButton(Button_Delete))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Confirm);
    }
    if (UButton* Button = GetWideButton(Button_Cancel))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Cancel);
    }
}

float ULoadScreenDeleteConfirmation::GetFrameWidth() const
{
    return SIzeBox_Root ? SIzeBox_Root->GetWidthOverride() : 0.0f;
}

void ULoadScreenDeleteConfirmation::Confirm()
{
    OnConfirmed.ExecuteIfBound();
}

void ULoadScreenDeleteConfirmation::Cancel()
{
    OnCancelled.ExecuteIfBound();
}

// 设计器控件随实例初始化一次，避免重建 Slate 时重复注册按钮回调。
void ULoadScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    // 通过设计器名称解析共享父类的各类控件，保留 UMG 生成的原有蓝图属性类型。
    PlayButtonWidget = Cast<UUserWidget>(GetWidgetFromName(TEXT("Button_Play")));
    DeleteButtonWidget = Cast<UUserWidget>(GetWidgetFromName(TEXT("Button_Delet")));
    ReturnButtonWidget = Cast<UUserWidget>(GetWidgetFromName(TEXT("Button_Quit")));
    FirstSwitcher = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("Switcher_0")));
    SecondSwitcher = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("Switcher_1")));
    ThirdSwitcher = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("Switcher_2")));
    SlotSwitcher = Cast<UWidgetSwitcher>(GetWidgetFromName(TEXT("WidgetSwitcher_33")));
    VacantPage = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("WBP_LoadSlot_Vacant")));
    EnterNamePage = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("WBP_LoadSlot_EnterName")));
    TakenPage = Cast<ULoadScreenWidget>(GetWidgetFromName(TEXT("WBP_LoadSlot_Taken")));
    NewGameButton = Cast<UButton>(GetWidgetFromName(TEXT("Button_NewGame")));
    NewSlotButtonWidget = Cast<UUserWidget>(GetWidgetFromName(TEXT("Button_NewSlot")));
    SelectSlotButtonWidget = Cast<UUserWidget>(GetWidgetFromName(TEXT("Button_SelectSlot")));
    NameEntry = Cast<UEditableText>(GetWidgetFromName(TEXT("Text_EnterName")));
    // 使用内置中文回退字体并保留描边。窄卡片限制字号，给全宽字形留出位置。
    if (NameEntry)
    {
        const FSlateFontInfo OriginalFont = NameEntry->GetFont();
        NameEntry->SetFont(
            FCoreStyle::GetDefaultFontStyle("Regular", FMath::Min(OriginalFont.Size, 18.f), OriginalFont.OutlineSettings));
    }
    if (auto* NameDisplay = Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_PlayerName"))))
    {
        const FSlateFontInfo OriginalFont = NameDisplay->GetFont();
        NameDisplay->SetFont(
            FCoreStyle::GetDefaultFontStyle("Regular", FMath::Min(OriginalFont.Size, 18.f), OriginalFont.OutlineSettings));
        NameDisplay->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    }

    if (UButton* Button = GetWideButton(PlayButtonWidget))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::PlaySelectedSlot);
    }
    if (UButton* Button = GetWideButton(DeleteButtonWidget))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::AskToDelete);
    }
    if (UButton* Button = GetWideButton(ReturnButtonWidget))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::ReturnToMainMenu);
    }
    if (NewGameButton)
    {
        NewGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::StartNewSlot);
    }
    if (UButton* Button = GetWideButton(NewSlotButtonWidget))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitName);
    }
    if (UButton* Button = GetWideButton(SelectSlotButtonWidget))
    {
        Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot);
    }
    UpdateScreenButtons();
}

void ULoadScreenWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BindModelDelegates();
}

void ULoadScreenWidget::NativeDestruct()
{
    CloseConfirmation();
    UnbindModelDelegates();
    Super::NativeDestruct();
}

// 先连接视图，再由 HUD 加载数据并触发槽位状态和 FieldNotify。
void ULoadScreenWidget::InitializeSaveScreen(UMVVM_LoadScreen* InScreenModel)
{
    UnbindModelDelegates();
    ScreenModel = InScreenModel;
    bHasSelection = false;
    BindModelDelegates();
    UpdateScreenButtons();

    ULoadScreenWidget* Switchers[] = {FirstSwitcher, SecondSwitcher, ThirdSwitcher};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Switchers); ++Index)
    {
        if (Switchers[Index] && ScreenModel && Index < ScreenModel->GetNumLoadSlots())
        {
            Switchers[Index]->InitializeSlot(ScreenModel, ScreenModel->GetLoadSlotViewModelByIndex(Index));
        }
    }
    if (APlayerController* Player = GetOwningPlayer())
    {
        Player->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(TakeWidget()));
        Player->bShowMouseCursor = true;
    }
}

// 切换器和三种页面共享槽位 VM。页面保留原有声明式绑定和转换。
void ULoadScreenWidget::InitializeSlot(UMVVM_LoadScreen* InScreenModel, UMVVM_LoadSlot* InSlotModel)
{
    UnbindModelDelegates();
    ScreenModel = InScreenModel;
    SlotModel = InSlotModel;
    if (UMVVMView* View = UMVVMSubsystem::GetViewFromUserWidget(this); View && SlotModel)
    {
        View->SetViewModel(TEXT("BP_LoadSlotViewModel"), SlotModel);
    }
    BindModelDelegates();

    ULoadScreenWidget* Pages[] = {VacantPage, EnterNamePage, TakenPage};
    for (ULoadScreenWidget* Page : Pages)
    {
        if (Page)
        {
            Page->InitializeSlot(InScreenModel, InSlotModel);
        }
    }
}

void ULoadScreenWidget::BindModelDelegates()
{
    if (ScreenModel && !SlotModel && PlayButtonWidget)
    {
        ScreenModel->SlotSelected.AddUniqueDynamic(this, &ThisClass::OnSlotSelected);
    }
    if (SlotModel && SlotSwitcher)
    {
        SlotModel->SetWidgetSwitcherIndex.AddUniqueDynamic(this, &ThisClass::SetSlotPage);
    }
    if (SlotModel && SelectSlotButtonWidget)
    {
        SlotModel->EnableSelectSlotButton.AddUniqueDynamic(this, &ThisClass::SetSelectButtonEnabled);
    }
}

void ULoadScreenWidget::UnbindModelDelegates()
{
    if (ScreenModel)
    {
        ScreenModel->SlotSelected.RemoveDynamic(this, &ThisClass::OnSlotSelected);
    }
    if (SlotModel)
    {
        SlotModel->SetWidgetSwitcherIndex.RemoveDynamic(this, &ThisClass::SetSlotPage);
        SlotModel->EnableSelectSlotButton.RemoveDynamic(this, &ThisClass::SetSelectButtonEnabled);
    }
}

void ULoadScreenWidget::UpdateScreenButtons()
{
    const bool bEnabled = bHasSelection && !Confirmation;
    if (UButton* Button = GetWideButton(PlayButtonWidget))
    {
        Button->SetIsEnabled(bEnabled);
    }
    if (UButton* Button = GetWideButton(DeleteButtonWidget))
    {
        Button->SetIsEnabled(bEnabled);
    }
}

void ULoadScreenWidget::OnSlotSelected()
{
    bHasSelection = true;
    UpdateScreenButtons();
}

void ULoadScreenWidget::SetSlotPage(int32 Index)
{
    SlotSwitcher->SetActiveWidgetIndex(Index);
}

void ULoadScreenWidget::SetSelectButtonEnabled(bool bEnabled)
{
    if (UButton* Button = GetWideButton(SelectSlotButtonWidget))
    {
        Button->SetIsEnabled(bEnabled);
    }
}

void ULoadScreenWidget::StartNewSlot()
{
    if (ScreenModel && SlotModel)
    {
        ScreenModel->NewGameButtonPressed(SlotModel->SlotIndex);
    }
}

void ULoadScreenWidget::SubmitName()
{
    if (ScreenModel && SlotModel && NameEntry)
    {
        ScreenModel->NewSlotButtonPressed(SlotModel->SlotIndex, NameEntry->GetText().ToString());
        NameEntry->SetText(FText::GetEmpty());
    }
}

void ULoadScreenWidget::SelectSlot()
{
    if (ScreenModel && SlotModel)
    {
        ScreenModel->SelectSlotButtonPressed(SlotModel->SlotIndex);
    }
}

void ULoadScreenWidget::PlaySelectedSlot()
{
    if (ScreenModel && bHasSelection && !Confirmation)
    {
        ScreenModel->PlayButtonPressed();
    }
}

// 保持原有弹窗位置，并在弹窗显示时禁用主界面的读取和删除按钮。
void ULoadScreenWidget::AskToDelete()
{
    if (!ScreenModel || !bHasSelection || Confirmation || !DeleteConfirmationClass)
    {
        return;
    }
    Confirmation = CreateWidget<ULoadScreenDeleteConfirmation>(GetOwningPlayer(), DeleteConfirmationClass);
    if (!Confirmation)
    {
        return;
    }
    Confirmation->OnConfirmed.BindUObject(this, &ThisClass::ConfirmDelete);
    Confirmation->OnCancelled.BindUObject(this, &ThisClass::CancelDelete);
    UpdateScreenButtons();
    Confirmation->AddToViewport();
    const float ViewportWidth = UWidgetLayoutLibrary::GetViewportSize(this).X;
    Confirmation->SetPositionInViewport(FVector2D((ViewportWidth - Confirmation->GetFrameWidth()) * 0.5f, 100.0f));
}

void ULoadScreenWidget::CloseConfirmation()
{
    if (Confirmation)
    {
        Confirmation->RemoveFromParent();
        Confirmation = nullptr;
    }
}

void ULoadScreenWidget::ConfirmDelete()
{
    CloseConfirmation();
    ScreenModel->DeleteButtonPressed();
    bHasSelection = false;
    UpdateScreenButtons();
}

void ULoadScreenWidget::CancelDelete()
{
    CloseConfirmation();
    UpdateScreenButtons();
}

void ULoadScreenWidget::ReturnToMainMenu()
{
    if (!ReturnMenuLevel.IsNull())
    {
        UGameplayStatics::OpenLevelBySoftObjectPtr(this, ReturnMenuLevel, false);
    }
}
