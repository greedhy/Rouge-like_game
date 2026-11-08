#include "Roguelike/DungeonRunWidget.h"
#include "Roguelike/DungeonRunController.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunProfile.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Roguelike/DungeonMapProjection.h"
#include "Camera/PlayerCameraManager.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
    FString RoomLabel(EDungeonRoomRole RoomRole)
    {
        switch (RoomRole)
        {
        case EDungeonRoomRole::Entrance:
            return TEXT("入口");
        case EDungeonRoomRole::Combat:
            return TEXT("战斗");
        case EDungeonRoomRole::Treasure:
            return TEXT("宝箱");
        case EDungeonRoomRole::Elite:
            return TEXT("精英");
        case EDungeonRoomRole::Merchant:
            return TEXT("商店");
        case EDungeonRoomRole::Rest:
            return TEXT("休整");
        case EDungeonRoomRole::Boss:
            return TEXT("首领");
        default:
            return TEXT("房间");
        }
    }
    FString PhaseLabel(EDungeonRunPhase RunPhase)
    {
        switch (RunPhase)
        {
        case EDungeonRunPhase::Loading:
            return TEXT("正在装配");
        case EDungeonRunPhase::Exploring:
            return TEXT("探索中");
        case EDungeonRunPhase::Encounter:
            return TEXT("战斗中");
        case EDungeonRunPhase::Victory:
            return TEXT("首领已击败 · 出口已开启");
        case EDungeonRunPhase::Finished:
            return TEXT("本局完成");
        case EDungeonRunPhase::Failed:
            return TEXT("本局结束");
        default:
            return TEXT("");
        }
    }
    UTextBlock* MakeRunText(UWidgetTree* Tree, UVerticalBox* Column, FName Name, int32 Size, FLinearColor Color)
    {
        auto* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        auto Font = Text->GetFont();
        Font.Size = Size;
        Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(true);
        Text->SetVisibility(ESlateVisibility::HitTestInvisible);
        Column->AddChildToVerticalBox(Text)->SetPadding(FMargin(0, 0, 0, 5));
        return Text;
    }
    FString DoorHints(const ADungeonRunDirector* Director, FName CurrentRoom)
    {
        const auto* Generation = Director ? Director->GetGeneration() : nullptr;
        if (!Generation)
            return FString();
        const auto& Layout = Generation->Manifest.Layout;
        const int32 Current = Layout.Rooms.IndexOfByPredicate(
            [CurrentRoom](const auto& Entry)
            {
                return Entry.RoomId == CurrentRoom;
            });
        if (!Layout.Rooms.IsValidIndex(Current))
            return FString();
        TArray<FString> Hints;
        for (int32 Index = 0; Index < Layout.Connections.Num(); ++Index)
        {
            const auto& Link = Layout.Connections[Index];
            const int32 Other = Link.From == Current ? Link.To : Link.To == Current ? Link.From : INDEX_NONE;
            if (!Layout.Rooms.IsValidIndex(Other))
                continue;
            const auto Offset = Layout.Rooms[Other].Cell - Layout.Rooms[Current].Cell;
            const TCHAR* Direction = Offset.X > 0   ? TEXT("东")
                                     : Offset.X < 0 ? TEXT("西")
                                     : Offset.Y > 0 ? TEXT("北")
                                                    : TEXT("南");
            const bool bOpen = Director->OpenDoors.IsValidIndex(Index) && Director->OpenDoors[Index];
            Hints.Add(FString::Printf(TEXT("%s:%s%s"), Direction, *RoomLabel(Layout.Rooms[Other].Role),
                                      bOpen ? TEXT("") : TEXT("[锁]")));
        }
        return FString::Join(Hints, TEXT("  "));
    }
} // namespace

TSharedRef<SWidget> UDungeonRunWidget::RebuildWidget()
{
    // NativeConstruct runs after Slate caches the root, so install the native tree first.
    if (!WidgetTree)
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("RunWidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RunRoot"));
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        WidgetTree->RootWidget = Root;
        auto* Border = WidgetTree->ConstructWidget<UBorder>();
        Border->SetBrushColor(FLinearColor(.025f, .035f, .055f, .9f));
        Border->SetPadding(FMargin(12));
        Border->SetVisibility(ESlateVisibility::Visible);
        // Disabled offers cannot handle pointer events. Consume their bubble at
        // the panel so clicking sold-out stock cannot start a world move/attack.
        Border->OnMouseButtonDownEvent.BindDynamic(this, &ThisClass::ConsumePanelPointer);
        Border->OnMouseButtonUpEvent.BindDynamic(this, &ThisClass::ConsumePanelPointer);
        Border->OnMouseDoubleClickEvent.BindDynamic(this, &ThisClass::ConsumePanelPointer);
        auto* PanelSlot = Root->AddChildToCanvas(Border);
        PanelSlot->SetAnchors(FAnchors(1.f, 0.f));
        PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
        PanelSlot->SetPosition(FVector2D(-18.f, 18.f));
        PanelSlot->SetAutoSize(true);
        auto* Width = WidgetTree->ConstructWidget<USizeBox>();
        Width->SetWidthOverride(330);
        Border->SetContent(Width);
        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        Width->SetContent(Column);
        Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        StatusText = MakeRunText(WidgetTree, Column, TEXT("RunStatus"), 15, FLinearColor(.95f, .85f, .55f));
        MapText = MakeRunText(WidgetTree, Column, TEXT("RunMapLabel"), 11, FLinearColor(.65f, .78f, .85f));
        auto* MapSize = WidgetTree->ConstructWidget<USizeBox>();
        MapSize->SetHeightOverride(150);
        Column->AddChildToVerticalBox(MapSize)->SetPadding(FMargin(0, 2, 0, 6));
        MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
        MapCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
        MapSize->SetContent(MapCanvas);
        HintText = MakeRunText(WidgetTree, Column, TEXT("RunHint"), 12, FLinearColor(.85f, .9f, .95f));
        MessageText = MakeRunText(WidgetTree, Column, TEXT("RunFeedback"), 12, FLinearColor(.6f, .95f, .72f));
        OfferButtons.Reset();
        OfferLabels.Reset();
        for (int32 Index = 0; Index < 3; ++Index)
        {
            auto* Button = WidgetTree->ConstructWidget<UButton>();
            Button->SetBackgroundColor(FLinearColor(.12f, .22f, .3f));
            Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 3));
            auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
            auto Font = Label->GetFont();
            Font.Size = 12;
            Label->SetFont(Font);
            Label->SetAutoWrapText(true);
            Button->SetContent(Label);
            OfferButtons.Add(Button);
            OfferLabels.Add(Label);
        }
        OfferButtons[0]->OnClicked.AddDynamic(this, &ThisClass::BuyFirst);
        OfferButtons[1]->OnClicked.AddDynamic(this, &ThisClass::BuySecond);
        OfferButtons[2]->OnClicked.AddDynamic(this, &ThisClass::BuyThird);
        ReturnButton = WidgetTree->ConstructWidget<UButton>();
        auto* ReturnLabel = WidgetTree->ConstructWidget<UTextBlock>();
        ReturnLabel->SetText(FText::FromString(TEXT("返回菜单")));
        ReturnButton->SetContent(ReturnLabel);
        Column->AddChildToVerticalBox(ReturnButton)->SetPadding(FMargin(0, 6, 0, 0));
        ReturnButton->OnClicked.AddDynamic(this, &ThisClass::LeaveRun);
    }
    return Super::RebuildWidget();
}

void UDungeonRunWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Refresh();
}

FEventReply UDungeonRunWidget::ConsumePanelPointer(FGeometry Geometry, const FPointerEvent& MouseEvent)
{
    return UWidgetBlueprintLibrary::Handled();
}

void UDungeonRunWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    RefreshRemaining -= InDeltaTime;
    if (RefreshRemaining <= 0.f)
    {
        RefreshRemaining = .15f;
        Refresh();
    }
}

void UDungeonRunWidget::Refresh()
{
    auto* Controller = Cast<ADungeonRunController>(GetOwningPlayer());
    const auto* Director = ADungeonRunDirector::Find(GetWorld());
    if (!Controller || !StatusText)
        return;
    const auto& View = Controller->RunView;
    const auto* Room = Director ? Director->Rooms.FindByPredicate(
                                      [&View](const auto& Entry)
                                      {
                                          return Entry.RoomId == View.CurrentRoom;
                                      })
                                : nullptr;
    const auto* Pawn = Cast<ADungeonPlayerCharacter>(Controller->GetPawn());
    const bool bDowned = Pawn && Pawn->IsDungeonDowned();
    FString Status =
        FString::Printf(TEXT("%s  |  金币 %d\n%s  %s"), Director ? *PhaseLabel(Director->Phase) : TEXT("正在装配"),
                        View.Coins, *View.CurrentRoom.ToString(), Room ? *RoomLabel(Room->Role) : TEXT(""));
    if (Room && Room->Phase == EDungeonRoomPhase::Active)
        Status += FString::Printf(TEXT("\n第 %d 波 · 待出现 %d 波 · 剩余目标 %d"), Room->Wave, Room->PendingWaves,
                                  Room->RemainingRequired);
    else if (Room && Room->Phase == EDungeonRoomPhase::Gathering)
        Status += TEXT("\n等待存活队员进入安全区域");
    if (bDowned)
        Status += TEXT("\n已倒地 · 队伍清房后复活");
    StatusText->SetText(FText::FromString(Status));
    FString Hint;
    if (!bDowned && Room)
    {
        switch (Room->Role)
        {
        case EDungeonRoomRole::Treasure:
            Hint = View.ChestRemaining == 0 ? TEXT("宝箱已领取")
                   : View.bCanInteract      ? TEXT("F 领取个人宝箱")
                                            : TEXT("靠近房间中央领取宝箱");
            if (View.ChestRemaining > 0)
                Hint += FString::Printf(TEXT(" · 待领 %d"), View.ChestRemaining);
            break;
        case EDungeonRoomRole::Merchant:
            Hint = View.bCanInteract ? TEXT("个人商店 · 每项限购一次") : TEXT("靠近房间中央查看商品");
            break;
        case EDungeonRoomRole::Rest:
            Hint = View.bRested        ? TEXT("本房休整已使用")
                   : View.bCanInteract ? TEXT("F 休整 · 恢复 30% 生命与法力")
                                       : TEXT("靠近房间中央休整");
            break;
        case EDungeonRoomRole::Boss:
            if (Director->Phase == EDungeonRunPhase::Victory)
                Hint = TEXT("F 使用中央出口，或返回菜单");
            break;
        default:
            break;
        }
    }
    const FString Doors = DoorHints(Director, View.CurrentRoom);
    if (!Doors.IsEmpty())
    {
        if (!Hint.IsEmpty())
            Hint += TEXT("\n");
        Hint += Doors;
    }
    HintText->SetText(FText::FromString(Hint));
    MessageText->SetText(FText::FromString(
        Director && Director->Phase == EDungeonRunPhase::Failed ? Director->FailureReason : View.Message));
    const bool bShowShop = Room && Room->Role == EDungeonRoomRole::Merchant && View.bCanInteract && !bDowned;
    for (int32 Index = 0; Index < OfferButtons.Num(); ++Index)
    {
        const auto* Profile = Director ? Director->Profile.Get() : nullptr;
        const bool bOfferExists = Profile && Profile->Offers.IsValidIndex(Index);
        const bool bSold = View.Purchased.IsValidIndex(Index) && View.Purchased[Index];
        OfferButtons[Index]->SetVisibility(bShowShop ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        OfferButtons[Index]->SetIsEnabled(bShowShop && bOfferExists && !bSold &&
                                          View.Coins >= Profile->Offers[Index].Price);
        if (bOfferExists)
        {
            const auto& Offer = Profile->Offers[Index];
            const FString ItemName =
                Offer.Name.IsEmpty() ? Offer.ItemId.PrimaryAssetName.ToString() : Offer.Name.ToString();
            OfferLabels[Index]->SetText(
                FText::FromString(FString::Printf(TEXT("%s · %d 金币%s"), *ItemName, Offer.Price,
                                                  bSold                      ? TEXT(" · 售罄")
                                                  : View.Coins < Offer.Price ? TEXT(" · 金币不足")
                                                                             : TEXT(" · 购买"))));
        }
        else
            OfferLabels[Index]->SetText(FText::FromString(TEXT("商品不可用")));
    }
    const bool bTerminal =
        Director && (Director->Phase == EDungeonRunPhase::Victory || Director->Phase == EDungeonRunPhase::Finished ||
                     Director->Phase == EDungeonRunPhase::Failed);
    ReturnButton->SetVisibility(bTerminal ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RefreshMap(Director, View.CurrentRoom);
}

void UDungeonRunWidget::RefreshMap(const ADungeonRunDirector* Director, FName CurrentRoom)
{
    const auto* Generation = Director ? Director->GetGeneration() : nullptr;
    if (!Generation || !MapCanvas)
        return;
    const auto& Layout = Generation->Manifest.Layout;
    const auto* Controller = GetOwningPlayer();
    const double CameraYaw =
        Controller && Controller->PlayerCameraManager
            ? FMath::RoundToDouble(Controller->PlayerCameraManager->GetCameraRotation().Yaw * 10.) / 10.
            : 0.;
    auto ProjectCell = [&](const FIntPoint& Cell)
    {
        return DungeonMapProjection::Project(FVector2D(Cell.X, Cell.Y), CameraYaw);
    };
    FString Signature =
        Director->RunId.ToString() + CurrentRoom.ToString() + FString::Printf(TEXT("|yaw=%.1f|"), CameraYaw);
    TArray<int32> Visited;
    FVector2D MinCell(DBL_MAX, DBL_MAX), MaxCell(-DBL_MAX, -DBL_MAX);
    for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
    {
        const auto& Placement = Layout.Rooms[Index];
        const auto* Progress = Director->Rooms.FindByPredicate(
            [&Placement](const auto& Entry)
            {
                return Entry.RoomId == Placement.RoomId;
            });
        if (!Progress || !Progress->bVisited)
            continue;
        Visited.Add(Index);
        Signature += Placement.RoomId.ToString() + FString::FromInt(static_cast<int32>(Progress->Phase));
        const auto Projected = ProjectCell(Placement.Cell);
        MinCell.X = FMath::Min(MinCell.X, Projected.X);
        MinCell.Y = FMath::Min(MinCell.Y, Projected.Y);
        MaxCell.X = FMath::Max(MaxCell.X, Projected.X);
        MaxCell.Y = FMath::Max(MaxCell.Y, Projected.Y);
    }
    for (bool bOpen : Director->OpenDoors)
        Signature += bOpen ? TEXT("1") : TEXT("0");
    if (Signature == MapSignature)
        return;
    MapSignature = Signature;
    MapCanvas->ClearChildren();
    MapText->SetText(
        FText::FromString(FString::Printf(TEXT("共享探索 · 已访问 %d 房 · 亮色为当前位置"), Visited.Num())));
    if (Visited.IsEmpty())
        return;
    const double Step =
        FMath::Min(40., FMath::Min(320. / (MaxCell.X - MinCell.X + 1), 146. / (MaxCell.Y - MinCell.Y + 1)));
    auto Position = [&](int32 Index)
    {
        return (ProjectCell(Layout.Rooms[Index].Cell) - MinCell) * Step + FVector2D(Step * .5);
    };
    // Never draw an unvisited cell or a line leading into unexplored geometry.
    for (int32 Index = 0; Index < Layout.Connections.Num(); ++Index)
    {
        const auto& Link = Layout.Connections[Index];
        if (!Visited.Contains(Link.From) || !Visited.Contains(Link.To))
            continue;
        const auto A = Position(Link.From), B = Position(Link.To);
        auto* Line = WidgetTree->ConstructWidget<UBorder>();
        const bool bOpen = Director->OpenDoors.IsValidIndex(Index) && Director->OpenDoors[Index];
        Line->SetBrushColor(bOpen ? FLinearColor(.25f, .6f, .5f) : FLinearColor(.75f, .25f, .2f));
        auto* LinkSlot = MapCanvas->AddChildToCanvas(Line);
        const auto Delta = B - A;
        LinkSlot->SetPosition(A - FVector2D(0, 1));
        LinkSlot->SetSize(FVector2D(FMath::Max(2., Delta.Size()), 2));
        Line->SetRenderTransformPivot(FVector2D(0, .5));
        Line->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
    }
    for (int32 Index : Visited)
    {
        const auto& Placement = Layout.Rooms[Index];
        auto* Cell = WidgetTree->ConstructWidget<UBorder>();
        Cell->SetPadding(FMargin(0));
        Cell->SetBrushColor(Placement.RoomId == CurrentRoom ? FLinearColor(.8f, .6f, .18f)
                                                            : FLinearColor(.13f, .3f, .4f));
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(RoomLabel(Placement.Role).Left(1)));
        auto Font = Label->GetFont();
        Font.Size = FMath::Clamp(FMath::RoundToInt(Step * .36f), 7, 12);
        Label->SetFont(Font);
        Label->SetJustification(ETextJustify::Center);
        Cell->SetContent(Label);
        auto* CellSlot = MapCanvas->AddChildToCanvas(Cell);
        CellSlot->SetPosition(Position(Index) - FVector2D(Step * .34f));
        CellSlot->SetSize(FVector2D(Step * .68f));
    }
}
void UDungeonRunWidget::BuyFirst()
{
    Purchase(0);
}
void UDungeonRunWidget::BuySecond()
{
    Purchase(1);
}
void UDungeonRunWidget::BuyThird()
{
    Purchase(2);
}
void UDungeonRunWidget::LeaveRun()
{
    if (auto* Controller = Cast<ADungeonRunController>(GetOwningPlayer()))
        Controller->ReturnToMenu();
}
void UDungeonRunWidget::Purchase(int32 Index)
{
    if (auto* Controller = Cast<ADungeonRunController>(GetOwningPlayer()))
        Controller->RequestRoomInteraction(Controller->RunView.CurrentRoom, EDungeonInteraction::Purchase, Index);
}
