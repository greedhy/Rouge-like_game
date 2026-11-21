// 调用链：Overlay 属性按钮 → MyHUD → 四页人物窗口 → 原工作台 RPC/临时 Spec → 隐藏试放或确认关闭。
#include "UI/Widget/CharacterMenuWidget.h"

#include "Backpack/BackpackComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "Player/MyPlayerState.h"
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "UI/Widget/EquipmentPanel.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

// 保留页面实例，切换页签时保持蓝图控件和技能选择的生命周期。
void UCharacterMenuWidget::InitializePages(TSubclassOf<UUserWidget> AttributeClass, TSubclassOf<UUserWidget> SpellClass)
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
    if (AMyPlayerState* State = Controller ? Controller->GetPlayerState<AMyPlayerState>() : nullptr)
    {
        UBackpackComponent* Bag = State->GetBackpackComponent();
        Bag->Initialize();
        Equipment->InitializeEquipment(State->GetEquipmentComponent(), Bag->InventoryVM, Bag->InventoryModel);
    }
}

// 修改页签时只切换 Slate 内容，窗口保持原来的输入所有权。
void UCharacterMenuWidget::ShowPage(int32 PageIndex)
{
    if (ActivePage == 2 && PageIndex != 2)
    {
        DeselectSkillSlot();
    }
    ActivePage = FMath::Clamp(PageIndex, 0, 3);
    if (ActivePage == 3)
    {
        EnsureRecipePage();
        if (RecipePanel)
        {
            RecipePanel->ShowEmbeddedPage();
        }
    }
    if (Pages)
    {
        Pages->SetActiveWidgetIndex(ActivePage);
    }
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UCharacterMenuWidget::DeselectSkillSlot()
{
    // 技能槽位选择是技能页临时状态，离开或关闭时取消等待。
    if (auto* Menu = Cast<UMyUserWidget>(Skills))
    {
        if (auto* Controller = Cast<USpellMenuWidgetController>(Menu->WidgetController))
        {
            Controller->GlobeDeselect();
        }
    }
}

void UCharacterMenuWidget::EnsureRecipePage()
{
    if (RecipePanel)
    {
        return;
    }
    const auto* Controller = GetOwningPlayer();
    const auto* ASC =
        Controller ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Controller->GetPawn()) : nullptr;
    AActor* Owner = ASC ? ASC->GetOwnerActor() : nullptr;
    auto* Developer = IsValid(Owner) ? Owner->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>() : nullptr;
    FString Error;
    if (!Developer || !Developer->InitializeCatalog(nullptr, Error))
    {
        if (RecipePageHost)
        {
            RecipePageHost->SetContent(SNew(STextBlock)
                                           .Text(NSLOCTEXT("CharacterMenu", "RecipeUnavailable",
                                                           "技能组装尚未就绪，请在角色出生后重新打开此页。")));
        }
        return;
    }
    RecipePanel = CreateWidget<UAbilityRuntimeRecipePanel>(GetOwningPlayer());
    if (!RecipePanel)
    {
        return;
    }
    RecipePanel->SetEmbeddedPage();
    RecipePanel->InitializePanel(Developer);
    RecipePanel->OnHiddenForCasting.AddUObject(this, &ThisClass::HideForRecipeCasting);
    RecipePanel->OnClosed.AddUObject(this, &ThisClass::RecipeClosed);
    if (RecipePageHost)
    {
        RecipePageHost->SetContent(RecipePanel->TakeWidget());
    }
}

void UCharacterMenuWidget::SetVisibility(ESlateVisibility InVisibility)
{
    if ((InVisibility == ESlateVisibility::Collapsed || InVisibility == ESlateVisibility::Hidden) && RecipePanel &&
        !RecipePanel->IsSessionClosed())
    {
        // 蓝图/其他入口隐藏整个窗口也必须走精确关闭，而不是留下不可见的临时 Spec。
        RequestCloseMenu();
        return;
    }
    Super::SetVisibility(InVisibility);
}

void UCharacterMenuWidget::RequestCloseMenu()
{
    DeselectSkillSlot();
    if (RecipePanel && !RecipePanel->IsSessionClosed())
    {
        // 冲突失败会留在可操作的第四页，让玩家重试或显式放弃，不能隐藏错误。
        ShowPage(3);
        RecipePanel->ClosePanel();
        return;
    }
    Super::SetVisibility(ESlateVisibility::Collapsed);
}

void UCharacterMenuWidget::HideForRecipeCasting()
{
    // 唯一保留临时技能的隐藏路径。由父窗口解除菜单输入注册，恢复真实 T/鼠标瞄准。
    DeselectSkillSlot();
    Super::SetVisibility(ESlateVisibility::Collapsed);
}

void UCharacterMenuWidget::RecipeClosed()
{
    DeselectSkillSlot();
    Super::SetVisibility(ESlateVisibility::Collapsed);
}

void UCharacterMenuWidget::NativeDestruct()
{
    if (RecipePanel)
    {
        RecipePanel->OnHiddenForCasting.RemoveAll(this);
        RecipePanel->OnClosed.RemoveAll(this);
        RecipePanel->AbandonEmbeddedPage();
    }
    Super::NativeDestruct();
}

TSharedRef<SWidget> UCharacterMenuWidget::RebuildWidget()
{
    auto Tabs = SNew(SHorizontalBox);
    const FText Labels[] = {
        NSLOCTEXT("CharacterMenu", "Attributes", "属性"), NSLOCTEXT("CharacterMenu", "Equipment", "装备"),
        NSLOCTEXT("CharacterMenu", "Skills", "技能"), NSLOCTEXT("CharacterMenu", "Recipe", "技能组装")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
    {
        Tabs->AddSlot().AutoWidth().Padding(
            0, 0, 6, 0)[SNew(SButton)
                            .Tag(FName(*FString::Printf(TEXT("CharacterMenuPage%d"), Index)))
                            .ContentPadding(FMargin(16, 6))
                            .IsFocusable(false)
                            .ButtonColorAndOpacity_Lambda(
                                [this, Index]
                                {
                                    return ActivePage == Index ? FLinearColor(0.50f, 0.33f, 0.13f)
                                                               : FLinearColor(0.18f, 0.16f, 0.13f);
                                })
                            .Text(Labels[Index])
                            .OnClicked_Lambda(
                                [this, Index]
                                {
                                    ShowPage(Index);
                                    return FReply::Handled();
                                })];
    }
    Tabs->AddSlot().FillWidth(1.f)[SNullWidget::NullWidget];
    Tabs->AddSlot().AutoWidth()[SNew(SButton)
                                    .Tag(TEXT("CharacterMenuClose"))
                                    .IsFocusable(false)
                                    .Text(NSLOCTEXT("CharacterMenu", "Close", "关闭"))
                                    .OnClicked_Lambda(
                                        [this]
                                        {
                                            RequestCloseMenu();
                                            return FReply::Handled();
                                        })];

    return SNew(SBorder)
        .BorderBackgroundColor(FLinearColor(0.13f, 0.11f, 0.08f))
        .Padding(
            8)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Tabs] +
               SVerticalBox::Slot().AutoHeight()
                   [SAssignNew(Pages, SWidgetSwitcher).WidgetIndex(ActivePage) +
                    SWidgetSwitcher::Slot()[Attributes ? Attributes->TakeWidget() : SNullWidget::NullWidget] +
                    SWidgetSwitcher::Slot()[Equipment ? Equipment->TakeWidget() : SNullWidget::NullWidget] +
                    SWidgetSwitcher::Slot()[Skills ? Skills->TakeWidget() : SNullWidget::NullWidget] +
                    SWidgetSwitcher::Slot()[SAssignNew(
                        RecipePageHost, SBox)[RecipePanel ? RecipePanel->TakeWidget() : SNullWidget::NullWidget]]]];
}

void UCharacterMenuWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Pages.Reset();
    RecipePageHost.Reset();
}
