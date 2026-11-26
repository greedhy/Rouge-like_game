// 调用链：人物第四页 -> 现有试装授予 -> 卡片预览及提交 -> 原按键试放。
// 页只显示所属组件的权威卡片投影，实例销毁沿原面板会话回收。
#include "UI/Widget/AbilityRuntimeRecipePanel.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/NativeWidgetHost.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UAbilityRuntimeRecipePanel::BuildCardPage(UVerticalBox* Column)
{
    CardHost = WidgetTree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("RecipeCards"));
    Column->AddChildToVerticalBox(CardHost)->SetPadding(FMargin(0, 8));
}

void UAbilityRuntimeRecipePanel::RefreshCardCatalog()
{
    DefinitionSelector->ClearOptions();
    CardDefinitionIds.Reset();
    for (const auto& Definition : Component->GetDefinitions())
    {
        const FText Label = Component->GetCardAbilityLabel(Definition.Base.AbilityTag);
        if (Label.IsEmpty() || !Definition.Base.bSelectable || !Definition.Base.bHasDefaultGraph)
        {
            continue;
        }
        FString Option = Label.ToString();
        if (GetPathNameSafe(Definition.Base.AbilityClass.Get()).StartsWith(TEXT("/Game/PureMass/")))
        {
            Option += TEXT(" · Mass");
        }
        if (DefinitionSelector->FindOptionIndex(Option) == INDEX_NONE)
        {
            DefinitionSelector->AddOption(Option);
            CardDefinitionIds.Add(Definition.DefinitionId);
        }
    }
    const auto& State = Component->GetDeveloperState();
    const int32 Index = CardDefinitionIds.IndexOfByKey(State.SelectedId);
    DefinitionSelector->SetSelectedIndex(Index == INDEX_NONE ? 0 : Index);
}

void UAbilityRuntimeRecipePanel::RefreshCardPage(const FAbilityRuntimeDeveloperState& State)
{
    if (!CardHost)
    {
        return;
    }
    const bool bOwned = State.bCardPanelSession && State.bPreviewActive && RequestedSessionId.IsValid() &&
                        State.PreviewSessionId == RequestedSessionId;
    bOwnSessionWasActive |= bOwned;
    const bool bCanEdit = bOwned && !PendingCloseRequest.IsValid();
    DefinitionSelector->SetIsEnabled(!bOwned);
    StatusText->SetVisibility(State.Error.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    StatusText->SetText(State.Error.IsEmpty()
                            ? FText::GetEmpty()
                            : FText::FromString(State.Error[0] <= 127 ? TEXT("改装暂时无法应用，请重新预览或重开试装。")
                                                                      : State.Error));
    auto Body = SNew(SVerticalBox);
    const auto BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 18);
    const auto HeadingFont = FCoreStyle::GetDefaultFontStyle("Bold", 19);
    // 布局在 Slate 排版时读取实际分配宽度，窗口改变尺寸后无需重建会话或等待服务器刷新。
    const auto CardWidth = TAttribute<FOptionalSize>::CreateWeakLambda(
        this,
        [this]
        {
            const float Width = FMath::Max(240.f, CardHost->GetCachedGeometry().GetLocalSize().X - 12.f);
            const int32 Columns = FMath::Clamp(FMath::FloorToInt(Width / 280.f), 1, 3);
            return FOptionalSize((Width - 12.f * (Columns - 1)) / Columns);
        });
    CardHost->SetContent(Body);
    if (!bOwned)
    {
        Body->AddSlot().AutoHeight()[SNew(STextBlock)
                                         .Text(NSLOCTEXT("SkillCards", "StartHint",
                                                         "选一个技能开始免费试装。应用改装后，隐藏面板并按 T 试放。"))
                                         .Font(BodyFont)
                                         .AutoWrapText(true)];
        return;
    }

    const auto* Draft = State.Cards.FindByPredicate(
        [&State](const FAbilityRuntimeWorkshopCardView& Card)
        {
            return Card.Id == State.DraftCard;
        });
    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SNew(STextBlock)
                                                          .Text(NSLOCTEXT("SkillCards", "Installed", "当前效果"))
                                                          .Font(HeadingFont)
                                                          .ColorAndOpacity(FLinearColor(.55f, .83f, .96f))];
    bool bHasInstalled = false;
    for (const auto& Card : State.Cards)
    {
        if (!Card.bInstalled)
        {
            continue;
        }
        bHasInstalled = true;
        Body->AddSlot().AutoHeight().Padding(
            0, 0, 0, 6)[SNew(STextBlock)
                            .Text(FText::FromString(Card.Label + TEXT("  ·  ") + Card.Description))
                            .Font(BodyFont)
                            .AutoWrapText(true)];
    }
    if (!bHasInstalled)
    {
        Body->AddSlot().AutoHeight()[SNew(STextBlock)
                                         .Text(NSLOCTEXT("SkillCards", "Original", "基础技能，尚未安装改装。"))
                                         .Font(BodyFont)
                                         .AutoWrapText(true)];
    }
    if (Draft && State.DraftToken.IsValid())
    {
        const auto* Replaced = State.Cards.FindByPredicate(
            [Draft](const FAbilityRuntimeWorkshopCardView& Card)
            {
                return Card.bInstalled && Card.Slot == Draft->Slot && Card.Id != Draft->Id;
            });
        const FString Action = Replaced ? TEXT("替换 ") + Replaced->Label + TEXT(" → ") : TEXT("安装 ");
        Body->AddSlot().AutoHeight().Padding(
            0, 10, 0, 4)[SNew(STextBlock)
                             .Text(FText::FromString(Action + Draft->Label + TEXT("\n") + Draft->Description))
                             .Font(BodyFont)
                             .ColorAndOpacity(FLinearColor(.54f, .9f, .67f))
                             .AutoWrapText(true)];
        Body->AddSlot().AutoHeight()
            [SNew(SHorizontalBox) +
             SHorizontalBox::Slot().AutoWidth().Padding(
                 0, 0, 8, 0)[SNew(SButton)
                                 .Text(NSLOCTEXT("SkillCards", "Apply", "应用改装"))
                                 .ContentPadding(FMargin(18.f, 12.f))
                                 .ContentScale(FVector2D(1.5f))
                                 .ButtonColorAndOpacity(FLinearColor(.42f, .72f, .82f))
                                 .IsEnabled(bCanEdit)
                                 .OnClicked(FOnClicked::CreateWeakLambda(
                                     this,
                                     [this, Token = State.DraftToken, Revision = State.Revision]
                                     {
                                         Component->ServerApplyGraphDraft(Token, Revision);
                                         return FReply::Handled();
                                     }))] +
             SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
                                                    .Text(NSLOCTEXT("SkillCards", "Discard", "取消预览"))
                                                    .ContentPadding(FMargin(18.f, 12.f))
                                                    .ContentScale(FVector2D(1.5f))
                                                    .IsEnabled(bCanEdit)
                                                    .OnClicked(
                                                        FOnClicked::CreateWeakLambda(this,
                                                                                     [this, Token = State.DraftToken]
                                                                                     {
                                                                                         Component
                                                                                             ->ServerDiscardGraphDraft(
                                                                                                 Token);
                                                                                         return FReply::Handled();
                                                                                     }))]];
    }

    FString LastSlot;
    TSharedPtr<SWrapBox> Row;
    for (const auto& Card : State.Cards)
    {
        if (Card.Slot != LastSlot)
        {
            LastSlot = Card.Slot;
            Body->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[SNew(STextBlock)
                                                                  .Text(FText::FromString(Card.Slot))
                                                                  .Font(HeadingFont)
                                                                  .ColorAndOpacity(FLinearColor(.65f, .87f, 1.f))];
            Body->AddSlot()
                .AutoHeight()[SAssignNew(Row, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12.f, 10.f))];
        }
        Row->AddSlot()[SNew(SBox).WidthOverride(CardWidth).MinDesiredHeight(
            58.f)[SNew(SButton)
                      .ContentPadding(FMargin(18.f, 12.f))
                      .HAlign(HAlign_Fill)
                      .VAlign(VAlign_Center)
                      .ButtonColorAndOpacity(Card.bInstalled ? FLinearColor(.36f, .65f, .78f)
                                                             : FLinearColor(.58f, .68f, .78f))
                      .ToolTipText(FText::FromString(Card.Description))
                      .IsEnabled(bCanEdit)
                      .OnClicked(FOnClicked::CreateWeakLambda(
                          this,
                          [this, Id = Card.Id, bRemove = Card.bInstalled, Session = State.PreviewSessionId,
                           Revision = State.Revision]
                          {
                              if (bRemove)
                              {
                                  Component->ServerRemoveCard(Id, Session, Revision);
                              }
                              else
                              {
                                  Component->ServerPreviewCard(Id, Session, Revision);
                              }
                              return FReply::Handled();
                          }))[SNew(SScaleBox)
                                  .Stretch(EStretch::ScaleToFit)
                                  .StretchDirection(EStretchDirection::DownOnly)
                                  .HAlign(HAlign_Center)
                                  .VAlign(VAlign_Center)[SNew(STextBlock)
                                                             .Text(FText::FromString(Card.bInstalled
                                                                                         ? Card.Label + TEXT(" · 拆除")
                                                                                         : Card.Label))
                                                             .Font(BodyFont)
                                                             .ColorAndOpacity(FLinearColor::White)
                                                             .Justification(ETextJustify::Center)
                                                             // 名称使用按钮完整内容宽度。超长名称只缩小，始终保持一行。
                                                             .AutoWrapText(false)]]]];
    }
    Body->AddSlot().AutoHeight().Padding(
        0, 12, 0,
        0)[SNew(SHorizontalBox) +
           SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
                                                  .Text(NSLOCTEXT("SkillCards", "Reset", "重置改装"))
                                                  .ContentPadding(FMargin(18.f, 12.f))
                                                  .ContentScale(FVector2D(1.5f))
                                                  .IsEnabled(bCanEdit && (bHasInstalled || Draft))
                                                  .OnClicked(FOnClicked::CreateWeakLambda(this,
                                                                                          [this]
                                                                                          {
                                                                                              ResetClicked();
                                                                                              return FReply::Handled();
                                                                                          }))]];
}
