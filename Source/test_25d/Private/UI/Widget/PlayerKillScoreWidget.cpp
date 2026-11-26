// 调用链：构建 HUD 小组件 → 从所属控制器取得 PlayerState → 用快照刷新四个读数。
#include "UI/Widget/PlayerKillScoreWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Player/MyPlayerState.h"

#define LOCTEXT_NAMESPACE "PlayerKillScore"

namespace
{
    const FLinearColor LabelColor(.71f, .79f, .87f);
    const FLinearColor NumberColor(.91f, .95f, 1.f);
    const FLinearColor EliteColor(1.f, .74f, .34f);

    // 首列左对齐，两个计数栏使用等宽空间并右对齐，避免位数变化推挤标题。
    UTextBlock* AddCell(UWidgetTree& Tree, UHorizontalBox& Row, FName Name, FText Label, int32 Size,
                        FLinearColor Color, EHorizontalAlignment Alignment)
    {
        auto* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        auto Font = Text->GetFont();
        Font.Size = Size;
        Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetText(Label);
        Text->SetJustification(Alignment == HAlign_Left ? ETextJustify::Left : ETextJustify::Right);
        auto* Slot = Row.AddChildToHorizontalBox(Text);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Slot->SetHorizontalAlignment(Alignment);
        return Text;
    }
}

TSharedRef<SWidget> UPlayerKillScoreWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("KillScoreTree"));
    }
    if (!WidgetTree->RootWidget)
    {
        auto* Border = WidgetTree->ConstructWidget<UBorder>();
        Border->SetBrushColor(FLinearColor(.045f, .065f, .095f, .93f));
        Border->SetPadding(FMargin(12.f, 7.f));
        Border->SetVisibility(ESlateVisibility::HitTestInvisible);
        SetVisibility(ESlateVisibility::HitTestInvisible);
        WidgetTree->RootWidget = Border;

        auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>();
        Border->SetContent(Rows);
        auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
        Rows->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
        AddCell(*WidgetTree, *Header, TEXT("KillScoreTitle"), LOCTEXT("Title", "击杀统计"), 14, NumberColor, HAlign_Left);
        AddCell(*WidgetTree, *Header, TEXT("KillScoreNormal"), LOCTEXT("Normal", "普通怪"), 11, LabelColor, HAlign_Right);
        AddCell(*WidgetTree, *Header, TEXT("KillScoreElite"), LOCTEXT("Elite", "精英怪"), 11, EliteColor, HAlign_Right);

        auto* WorldRow = WidgetTree->ConstructWidget<UHorizontalBox>();
        Rows->AddChildToVerticalBox(WorldRow);
        AddCell(*WidgetTree, *WorldRow, TEXT("KillScoreWorldLabel"), LOCTEXT("World", "世界累计"), 12, LabelColor,
                HAlign_Left);
        Values.Add(AddCell(*WidgetTree, *WorldRow, TEXT("WorldNormalKills"), FText::AsNumber(0), 13, NumberColor,
                           HAlign_Right));
        Values.Add(AddCell(*WidgetTree, *WorldRow, TEXT("WorldEliteKills"), FText::AsNumber(0), 13, EliteColor,
                           HAlign_Right));

        auto* RoomRow = WidgetTree->ConstructWidget<UHorizontalBox>();
        Rows->AddChildToVerticalBox(RoomRow);
        AddCell(*WidgetTree, *RoomRow, TEXT("KillScoreRoomLabel"), LOCTEXT("Room", "当前房间"), 12, LabelColor,
                HAlign_Left);
        Values.Add(AddCell(*WidgetTree, *RoomRow, TEXT("RoomNormalKills"), FText::GetEmpty(), 13, NumberColor,
                           HAlign_Right));
        Values.Add(AddCell(*WidgetTree, *RoomRow, TEXT("RoomEliteKills"), FText::GetEmpty(), 13, EliteColor,
                           HAlign_Right));
    }
    return Super::RebuildWidget();
}

void UPlayerKillScoreWidget::NativeConstruct()
{
    Super::NativeConstruct();
    Displayed.Reset();
    Refresh();
}

void UPlayerKillScoreWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    Refresh();
}

// PlayerState 较界面晚复制到达时显示零值；走廊和无房间地图的房间列显示破折号。
void UPlayerKillScoreWidget::Refresh()
{
    const auto* PC = GetOwningPlayer();
    const auto* Player = PC ? PC->GetPlayerState<AMyPlayerState>() : nullptr;
    const FPlayerKillScore Score = Player ? Player->GetKillScore() : FPlayerKillScore{};
    if (Values.Num() != 4 || (Displayed.IsSet() && Displayed.GetValue() == Score))
    {
        return;
    }
    Displayed = Score;
    Values[0]->SetText(FText::AsNumber(Score.WorldNormal));
    Values[1]->SetText(FText::AsNumber(Score.WorldElite));
    const FText Outside = LOCTEXT("OutsideRoom", "—");
    Values[2]->SetText(Score.Room.IsNone() ? Outside : FText::AsNumber(Score.RoomNormal));
    Values[3]->SetText(Score.Room.IsNone() ? Outside : FText::AsNumber(Score.RoomElite));
}

#undef LOCTEXT_NAMESPACE
