// 调用链：MyHUD 创建人物窗口 → Slate 页签切换三个页面 → 隐藏窗口时解除输入注册。
#include "UI/Widget/CharacterMenuWidget.h"

#include "Backpack/BackpackComponent.h"
#include "Player/MyPlayerState.h"
#include "UI/Widget/EquipmentPanel.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

// 保留页面实例，切换页签时保持蓝图控件和技能选择的生命周期。
void UCharacterMenuWidget::InitializePages(TSubclassOf<UUserWidget> AttributeClass,
                                           TSubclassOf<UUserWidget> SpellClass)
{
    APlayerController* Controller = GetOwningPlayer();
    Attributes = CreateWidget<UUserWidget>(Controller, AttributeClass);
    Skills = CreateWidget<UUserWidget>(Controller, SpellClass);
    Equipment = CreateWidget<UEquipmentPanel>(Controller);

    // 窗口有统一的关闭按钮，内嵌页面继续执行其原有数据绑定。
    for (UUserWidget* Page : {Attributes.Get(), Skills.Get()})
    {
        if (auto* Menu = Cast<UGameplayMenuWidget>(Page))
        {
            Menu->SetEmbeddedPage();
        }
        if (UWidget* CloseButton = Page ? Page->GetWidgetFromName(TEXT("CloseButton")) : nullptr)
        {
            CloseButton->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if (AMyPlayerState* State = Controller->GetPlayerState<AMyPlayerState>())
    {
        UBackpackComponent* Bag = State->GetBackpackComponent();
        Bag->Initialize();
        Equipment->InitializeEquipment(State->GetEquipmentComponent(), Bag->InventoryVM, Bag->InventoryModel);
    }
}

// 修改页签时只切换 Slate 内容，窗口保持原来的输入所有权。
void UCharacterMenuWidget::ShowPage(int32 PageIndex)
{
    if (Pages && ActivePage == 2 && PageIndex != 2)
    {
        // 技能槽位选择是技能页临时状态，离开页面时取消等待。
        if (auto* Menu = Cast<UMyUserWidget>(Skills))
        {
            if (auto* Controller = Cast<USpellMenuWidgetController>(Menu->WidgetController))
            {
                Controller->GlobeDeselect();
            }
        }
    }
    ActivePage = FMath::Clamp(PageIndex, 0, 2);
    if (Pages)
    {
        Pages->SetActiveWidgetIndex(ActivePage);
    }
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TSharedRef<SWidget> UCharacterMenuWidget::RebuildWidget()
{
    auto Tabs = SNew(SHorizontalBox);
    const FText Labels[] = {
        NSLOCTEXT("CharacterMenu", "Attributes", "属性"),
        NSLOCTEXT("CharacterMenu", "Equipment", "装备"),
        NSLOCTEXT("CharacterMenu", "Skills", "技能")
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
    {
        Tabs->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
            [SNew(SButton)
                .ContentPadding(FMargin(16, 6))
                .IsFocusable(false)
                .ButtonColorAndOpacity_Lambda([this, Index]
                {
                    return ActivePage == Index ? FLinearColor(0.50f, 0.33f, 0.13f)
                                               : FLinearColor(0.18f, 0.16f, 0.13f);
                })
                .Text(Labels[Index])
                .OnClicked_Lambda([this, Index]
                {
                    ShowPage(Index);
                    return FReply::Handled();
                })];
    }
    Tabs->AddSlot().FillWidth(1.f)[SNullWidget::NullWidget];
    Tabs->AddSlot().AutoWidth()
        [SNew(SButton)
            .IsFocusable(false)
            .Text(NSLOCTEXT("CharacterMenu", "Close", "关闭"))
            .OnClicked_Lambda([this]
            {
                if (ActivePage == 2)
                {
                    ShowPage(0);
                }
                SetVisibility(ESlateVisibility::Collapsed);
                return FReply::Handled();
            })];

    return SNew(SBorder)
        .BorderBackgroundColor(FLinearColor(0.13f, 0.11f, 0.08f))
        .Padding(8)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Tabs]
            + SVerticalBox::Slot().AutoHeight()
                [SAssignNew(Pages, SWidgetSwitcher).WidgetIndex(ActivePage)
                    + SWidgetSwitcher::Slot()[Attributes ? Attributes->TakeWidget() : SNullWidget::NullWidget]
                    + SWidgetSwitcher::Slot()[Equipment ? Equipment->TakeWidget() : SNullWidget::NullWidget]
                    + SWidgetSwitcher::Slot()[Skills ? Skills->TakeWidget() : SNullWidget::NullWidget]]];
}

void UCharacterMenuWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Pages.Reset();
}
