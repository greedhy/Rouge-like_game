#include "UI/Widget/RoomMenuWidget.h"
#include "Online/RoomSubsystem.h"
#include "Online/RoomLobby.h"
#include "UI/HUD/LoadScreenHUD.h"
#include "UI/ViewModel/MVVM_LoadScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Image.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

URoomMenuWidget::URoomMenuWidget(const FObjectInitializer& Init) : Super(Init)
{
    SetIsFocusable(true);
    static ConstructorHelpers::FObjectFinder<UTexture2D> Normal(TEXT("/Game/Assets/UI/Wide_Button_Red/WideButton"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> Hover(
        TEXT("/Game/Assets/UI/Wide_Button_Red/WideButton_Highlighted"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> Backdrop(TEXT("/Game/resources/Background_Grass"));
    ButtonNormal = Normal.Object;
    ButtonHover = Hover.Object;
    Background = Backdrop.Object;
}
URoomSubsystem* URoomMenuWidget::Rooms() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<URoomSubsystem>() : nullptr;
}
TSharedRef<SWidget> URoomMenuWidget::RebuildWidget()
{
    if (!Pages)
        BuildTree();
    return Super::RebuildWidget();
}
UTextBlock* URoomMenuWidget::AddText(UVerticalBox* Parent, const FString& Text, int32 Size)
{
    auto* W = WidgetTree->ConstructWidget<UTextBlock>();
    W->SetText(FText::FromString(Text));
    W->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size));
    W->SetColorAndOpacity(FLinearColor(.93f, .88f, .76f));
    W->SetJustification(ETextJustify::Center);
    W->SetAutoWrapText(true);
    auto* TextSlot = Parent->AddChildToVerticalBox(W);
    TextSlot->SetPadding(FMargin(12, 10));
    return W;
}
UButton* URoomMenuWidget::AddButton(UVerticalBox* Parent, FName Name, const FString& Text)
{
    auto* W = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
    FButtonStyle Style = W->GetStyle();
    if (ButtonNormal)
    {
        Style.Normal.SetResourceObject(ButtonNormal);
        Style.Normal.DrawAs = ESlateBrushDrawType::Box;
        Style.Normal.Margin = FMargin(.2f);
    }
    Style.Hovered = Style.Normal;
    if (ButtonHover)
        Style.Hovered.SetResourceObject(ButtonHover);
    Style.Pressed = Style.Hovered;
    Style.Disabled = Style.Normal;
    Style.Disabled.TintColor = FLinearColor(.4f, .4f, .4f, .75f);
    W->SetStyle(Style);
    auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text));
    Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 23));
    Label->SetColorAndOpacity(FLinearColor(1, .94f, .8f));
    Label->SetJustification(ETextJustify::Center);
    W->AddChild(Label);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetHeightOverride(58);
    Size->AddChild(W);
    Parent->AddChildToVerticalBox(Size)->SetPadding(FMargin(50, 8));
    ActionButtons.Add(W);
    return W;
}
UEditableTextBox* URoomMenuWidget::AddPassword(UVerticalBox* Parent, FName Name)
{
    auto* W = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
    W->SetHintText(FText::FromString(TEXT("输入 6–12 位字母或数字")));
    W->WidgetStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 25));
    W->WidgetStyle.ForegroundColor = FLinearColor(.06f, .06f, .06f);
    W->SetJustification(ETextJustify::Center);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetHeightOverride(52);
    Size->AddChild(W);
    Parent->AddChildToVerticalBox(Size)->SetPadding(FMargin(50, 10));
    return W;
}
UVerticalBox* URoomMenuWidget::AddPage(const FString& Title, const FString& Subtitle)
{
    auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
    Pages->AddChild(Box);
    AddText(Box, Title, 38);
    AddText(Box, Subtitle, 18);
    return Box;
}
void URoomMenuWidget::BuildTree()
{
    auto* Root = WidgetTree->ConstructWidget<UOverlay>();
    WidgetTree->RootWidget = Root;
    auto* Back = WidgetTree->ConstructWidget<UImage>();
    Back->SetBrushFromTexture(Background);
    Back->SetColorAndOpacity(FLinearColor(.13f, .15f, .17f, 1));
    auto* BackSlot = Root->AddChildToOverlay(Back);
    BackSlot->SetHorizontalAlignment(HAlign_Fill);
    BackSlot->SetVerticalAlignment(VAlign_Fill);
    auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
    Scale->SetStretch(EStretch::ScaleToFit);
    auto* ScaleSlot = Root->AddChildToOverlay(Scale);
    ScaleSlot->SetHorizontalAlignment(HAlign_Center);
    ScaleSlot->SetVerticalAlignment(VAlign_Center);
    auto* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RoomFrame"));
    Size->SetWidthOverride(760);
    Size->SetHeightOverride(560);
    Scale->AddChild(Size);
    auto* Panel = WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrushColor(FLinearColor(.035f, .035f, .04f, .96f));
    Panel->SetPadding(FMargin(28));
    Size->AddChild(Panel);
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->AddChild(Column);
    Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("RoomPages"));
    Column->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    StatusText = AddText(Column, TEXT(""), 17);
    auto* Mode = AddPage(TEXT("选择游戏模式"), TEXT("已选择存档 · 选择本次冒险的方式"));
    AddButton(Mode, TEXT("SinglePlayerButton"), TEXT("单人模式"))
        ->OnClicked.AddDynamic(this, &ThisClass::SingleClicked);
    AddButton(Mode, TEXT("SwordShieldTrialButton"), TEXT("单人剑盾试炼"))
        ->OnClicked.AddDynamic(this, &ThisClass::SwordTrialClicked);
    AddButton(Mode, TEXT("MultiplayerButton"), TEXT("多人模式"))
        ->OnClicked.AddDynamic(this, &ThisClass::MultiplayerClicked);
    AddButton(Mode, TEXT("ReturnToSaveButton"), TEXT("返回存档选择"))
        ->OnClicked.AddDynamic(this, &ThisClass::BackClicked);
    auto* Multi = AddPage(TEXT("多人模式"), TEXT("与同一局域网内的朋友一起冒险"));
    AddButton(Multi, TEXT("CreateRoomButton"), TEXT("创建房间"))->OnClicked.AddDynamic(this, &ThisClass::CreateClicked);
    AddButton(Multi, TEXT("OpenJoinButton"), TEXT("加入房间"))->OnClicked.AddDynamic(this, &ThisClass::JoinPageClicked);
    AddButton(Multi, TEXT("ReturnToModeButton"), TEXT("返回"))->OnClicked.AddDynamic(this, &ThisClass::BackClicked);
    auto* Join = AddPage(TEXT("加入房间"), TEXT("向房主获取房间密码，输入后进行匹配"));
    JoinPassword = AddPassword(Join, TEXT("JoinPassword"));
    AddButton(Join, TEXT("JoinRoomButton"), TEXT("匹配并加入"))->OnClicked.AddDynamic(this, &ThisClass::JoinClicked);
    AddButton(Join, TEXT("ReturnToMultiplayerButton"), TEXT("返回"))
        ->OnClicked.AddDynamic(this, &ThisClass::BackClicked);
    auto* Lobby = AddPage(TEXT("房间大厅"), TEXT("将房间密码分享给同一局域网内的朋友"));
    PasswordText = AddText(Lobby, TEXT("房间密码：正在读取…"), 30);
    MembersText = AddText(Lobby, TEXT("正在读取玩家列表…"), 21);
    NewPassword = AddPassword(Lobby, TEXT("NewRoomPassword"));
    ChangeButton = AddButton(Lobby, TEXT("ChangePasswordButton"), TEXT("设置密码"));
    ChangeButton->OnClicked.AddDynamic(this, &ThisClass::ChangeClicked);
    StartButton = AddButton(Lobby, TEXT("StartRoomButton"), TEXT("开始游戏"));
    StartButton->OnClicked.AddDynamic(this, &ThisClass::StartClicked);
    AddButton(Lobby, TEXT("LeaveRoomButton"), TEXT("离开房间"))->OnClicked.AddDynamic(this, &ThisClass::LeaveClicked);
    LobbyHint = AddText(Lobby, TEXT("等待房主开始游戏"), 16);
    ProfessionPage = AddPage(TEXT("选择职业"), TEXT("职业随存档固定，适用于单人和多人模式。"));
    AddButton(ProfessionPage, TEXT("MageProfessionButton"), TEXT("魔法师"))
        ->OnClicked.AddDynamic(this, &ThisClass::MageProfessionClicked);
    AddButton(ProfessionPage, TEXT("StanderProfessionButton"), TEXT("Standard"))
        ->OnClicked.AddDynamic(this, &ThisClass::StanderProfessionClicked);
    AddButton(ProfessionPage, TEXT("CancelProfessionButton"), TEXT("返回存档选择"))
        ->OnClicked.AddDynamic(this, &ThisClass::BackClicked);
    ProfessionError = AddText(ProfessionPage, TEXT(""), 17);
    Modal = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RoomErrorDialog"));
    Modal->SetBrushColor(FLinearColor(.025f, .025f, .03f, .99f));
    Modal->SetPadding(FMargin(32));
    auto* ModalSize = WidgetTree->ConstructWidget<USizeBox>();
    ModalSize->SetWidthOverride(620);
    ModalSize->AddChild(Modal);
    auto* ModalSlot = Root->AddChildToOverlay(ModalSize);
    ModalSlot->SetHorizontalAlignment(HAlign_Center);
    ModalSlot->SetVerticalAlignment(VAlign_Center);
    auto* ModalColumn = WidgetTree->ConstructWidget<UVerticalBox>();
    Modal->AddChild(ModalColumn);
    AddText(ModalColumn, TEXT("提示"), 30);
    ModalText = AddText(ModalColumn, TEXT(""), 22);
    auto* Dismiss = AddButton(ModalColumn, TEXT("DismissRoomErrorButton"), TEXT("确定"));
    ActionButtons.Remove(Dismiss);
    Dismiss->OnClicked.AddDynamic(this, &ThisClass::DismissClicked);
    Modal->SetVisibility(ESlateVisibility::Collapsed);
}
void URoomMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (auto* R = Rooms())
        ChangeHandle = R->OnChanged.AddUObject(this, &ThisClass::Refresh);
    Refresh();
}
void URoomMenuWidget::NativeDestruct()
{
    if (auto* R = Rooms())
        R->OnChanged.Remove(ChangeHandle);
    Super::NativeDestruct();
}
void URoomMenuWidget::NativeTick(const FGeometry& G, float Delta)
{
    Super::NativeTick(G, Delta);
    RefreshElapsed += Delta;
    if (RefreshElapsed > .25f)
    {
        RefreshElapsed = 0;
        Refresh();
    }
}
void URoomMenuWidget::ShowModeSelection()
{
    bLobby = false;
    if (Pages)
        Pages->SetActiveWidgetIndex(0);
    Refresh();
}
void URoomMenuWidget::ShowProfessionSelection()
{
    bLobby = false;
    ProfessionError->SetText(FText::GetEmpty());
    Pages->SetActiveWidget(ProfessionPage);
    Refresh();
}

void URoomMenuWidget::ShowLobby()
{
    bLobby = true;
    if (Pages)
        Pages->SetActiveWidgetIndex(3);
    Refresh();
}
void URoomMenuWidget::Refresh()
{
    auto* R = Rooms();
    if (!R || !Pages)
        return;
    CastChecked<USizeBox>(GetWidgetFromName(TEXT("RoomFrame")))->SetHeightOverride(bLobby ? 840.f : 560.f);
    const bool Busy = R->IsBusy();
    const bool HasError = !R->LastError.IsEmpty();
    for (const auto& B : ActionButtons)
        if (B)
            B->SetIsEnabled(!Busy && !HasError);
    JoinPassword->SetIsEnabled(!Busy && !HasError);
    FString Status;
    switch (R->State)
    {
    case ERoomFlowState::Creating:
        Status = TEXT("正在创建房间…");
        break;
    case ERoomFlowState::Searching:
        Status = TEXT("正在搜索局域网房间…");
        break;
    case ERoomFlowState::Joining:
        Status = TEXT("正在连接房间…");
        break;
    case ERoomFlowState::Updating:
        Status = TEXT("正在更新房间密码…");
        break;
    case ERoomFlowState::Starting:
        Status = TEXT("正在进入游戏…");
        break;
    case ERoomFlowState::Leaving:
        Status = TEXT("正在离开房间…");
        break;
    default:
        Status = TEXT("当前存档：") + R->SelectedPlayerName;
    }
    StatusText->SetText(Pages->GetActiveWidget() == ProfessionPage ? FText::GetEmpty() : FText::FromString(Status));
    if (HasError)
    {
        ModalText->SetText(R->LastError);
        Modal->SetVisibility(ESlateVisibility::Visible);
    }
    else
        Modal->SetVisibility(ESlateVisibility::Collapsed);
    if (!bLobby)
        return;
    const auto* GS = GetWorld()->GetGameState<ARoomLobbyGameState>();
    if (!GS)
        return;
    PasswordText->SetText(FText::FromString(TEXT("房间密码：") + GS->RoomPassword));
    FString Members = FString::Printf(TEXT("房间玩家  %d / %d\n"), GS->PlayerArray.Num(), GS->Capacity);
    for (const auto& P : GS->PlayerArray)
        if (P)
            Members += P->GetPlayerName() + (P == GS->RoomOwner ? TEXT("  · 房主") : TEXT("  · 已加入")) + TEXT("\n");
    MembersText->SetText(FText::FromString(Members.TrimEnd()));
    const bool Host = GS->RoomOwner && GetOwningPlayer() && GS->RoomOwner == GetOwningPlayer()->PlayerState;
    NewPassword->SetVisibility(Host ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    NewPassword->SetIsEnabled(!Busy && !HasError);
    ChangeButton->SetIsEnabled(Host && !Busy && !HasError && !GS->bStarting);
    StartButton->SetIsEnabled(Host && !Busy && !HasError && !GS->bStarting);
    ChangeButton->SetVisibility(Host ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    StartButton->SetVisibility(Host ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    LobbyHint->SetText(FText::FromString(GS->bStarting ? TEXT("房主已开始游戏，正在加载地图…")
                                         : Host        ? TEXT("开始后所有玩家一起进入默认地图")
                                                       : TEXT("等待房主开始游戏")));
}
void URoomMenuWidget::SingleClicked()
{
    if (Rooms())
        Rooms()->StartSinglePlayer();
}
void URoomMenuWidget::SwordTrialClicked()
{
    if (Rooms())
        Rooms()->StartSwordShieldTrial();
}
void URoomMenuWidget::MageProfessionClicked()
{
    ConfirmProfession(EPlayerProfession::Mage);
}

void URoomMenuWidget::StanderProfessionClicked()
{
    ConfirmProfession(EPlayerProfession::Stander);
}

void URoomMenuWidget::ConfirmProfession(EPlayerProfession Profession)
{
    auto* HUD = Cast<ALoadScreenHUD>(GetOwningPlayer()->GetHUD());
    if (!HUD || !HUD->LoadScreenViewModel || !HUD->LoadScreenViewModel->ConfirmNewSlotProfession(Profession))
    {
        ProfessionError->SetText(FText::FromString(TEXT("无法创建存档，请确认存档槽位可用后重试。")));
    }
}
void URoomMenuWidget::MultiplayerClicked()
{
    Pages->SetActiveWidgetIndex(1);
}
void URoomMenuWidget::CreateClicked()
{
    if (Rooms())
        Rooms()->CreateRoom();
}
void URoomMenuWidget::JoinPageClicked()
{
    Pages->SetActiveWidgetIndex(2);
    JoinPassword->SetKeyboardFocus();
}
void URoomMenuWidget::JoinClicked()
{
    if (Rooms())
        Rooms()->JoinRoom(JoinPassword->GetText().ToString());
}
void URoomMenuWidget::BackClicked()
{
    const int32 Index = Pages->GetActiveWidgetIndex();
    if (Index == 0 || Pages->GetActiveWidget() == ProfessionPage)
    {
        if (auto* HUD = Cast<ALoadScreenHUD>(GetOwningPlayer()->GetHUD()))
            HUD->ShowSaveSelection();
    }
    else
        Pages->SetActiveWidgetIndex(Index == 2 ? 1 : 0);
}
void URoomMenuWidget::LeaveClicked()
{
    if (Rooms())
        Rooms()->LeaveRoom();
}
void URoomMenuWidget::ChangeClicked()
{
    if (auto* PC = Cast<ARoomLobbyController>(GetOwningPlayer()))
        PC->ServerChangePassword(NewPassword->GetText().ToString());
}
void URoomMenuWidget::StartClicked()
{
    if (auto* PC = Cast<ARoomLobbyController>(GetOwningPlayer()))
        PC->ServerStartGame();
}
void URoomMenuWidget::DismissClicked()
{
    if (Rooms())
        Rooms()->ClearError();
}
