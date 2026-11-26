// 调用链：人物窗口第四页/本地调试入口 -> 服务器图回执 -> 画布选择 -> 精确草稿/撤销 RPC；原 ASC 和 GA 负责施法。
#include "UI/Widget/AbilityRuntimeRecipePanel.h"
#include "UI/Widget/AbilityRuntimeGraphCanvas.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/Nodes/AbilityGameplayRuleNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SViewport.h"
#include "GameFramework/PlayerController.h"

namespace
{
    const FName InsertRegistered(TEXT("Insert.Registered"));
    const FName AppendRegistered(TEXT("Append.Registered"));
    const FName ReplaceBranch(TEXT("Replace.RegisteredBranch"));

    /** 查找服务器实际投影的值；不能用基础下拉选项翻译新插入节点。 */
    const FAbilityRuntimeGraphNodeView* FindGraphNode(const FAbilityRuntimeGraphView& Graph, FGuid NodeId)
    {
        return Graph.Nodes.FindByPredicate(
            [NodeId](const FAbilityRuntimeGraphNodeView& Node)
            {
                return Node.NodeId == NodeId;
            });
    }

    const FAbilityRuntimeGraphPortView* FindGraphPort(const FAbilityRuntimeGraphNodeView* Node, FName PortId)
    {
        return Node ? Node->Ports.FindByPredicate(
                          [PortId](const FAbilityRuntimeGraphPortView& Port)
                          {
                              return Port.Name == PortId;
                          })
                    : nullptr;
    }

    bool UsesRegisteredFragment(FName Modifier)
    {
        return Modifier == AppendRegistered || Modifier == ReplaceBranch || Modifier == InsertRegistered;
    }

    /** 客户端按显式端口种类筛选片段；配置、保护边和持续预算由服务器验证。 */
    bool SupportsFragmentPayload(FName Fragment, EAbilityPortPayload Payload)
    {
        return AbilityRuntimeModifierTemplates::SupportsFragmentPayload(Fragment, Payload);
    }
} // namespace

bool UAbilityRuntimeRecipePanel::HasOwnerViewportFocus() const
{
    const auto* Controller = GetOwningPlayer();
    const auto* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
    const auto* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
    const auto User = LocalPlayer ? LocalPlayer->GetSlateUser() : nullptr;
    const auto Widget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
    return FSlateApplication::IsInitialized() && User && Widget &&
           User->GetWeakFocusPath().ContainsWidget(Widget.Get());
}

void UAbilityRuntimeRecipePanel::InitializePanel(UAbilityRuntimeRecipeDeveloperComponent* InComponent)
{
    if (Component)
    {
        Component->OnDeveloperStateChanged.Remove(StateHandle);
        Component->OnPreviewCloseAcknowledged.Remove(CloseHandle);
    }
    Component = InComponent;
    StateHandle.Reset();
    CloseHandle.Reset();
    RequestedSessionId.Invalidate();
    bOwnSessionWasActive = false;
    BindComponent();
    RefreshCatalog();
    RefreshState();
}

void UAbilityRuntimeRecipePanel::ClosePanel()
{
    RequestClose(false);
}

void UAbilityRuntimeRecipePanel::SetEmbeddedPage()
{
    bEmbeddedPage = true;
}

void UAbilityRuntimeRecipePanel::ShowEmbeddedPage()
{
    if (bAcknowledgedClose)
    {
        // 只有上一轮已收到准确关闭确认，才允许把本地页面状态恢复为新的会话入口。
        InitializePanel(Component);
        bAcknowledgedClose = false;
        ClearGraphSelection();
    }
    SetVisibility(ESlateVisibility::Visible);
}

void UAbilityRuntimeRecipePanel::AbandonEmbeddedPage()
{
    if (!bAcknowledgedClose)
    {
        // 销毁不等待普通关闭的基础冲突；准确 nonce 防止误撤另一个面板的预览。
        if (Component && RequestedSessionId.IsValid())
        {
            Component->ServerCloseGraphPreview(FGuid::NewGuid(), RequestedSessionId, true);
        }
        else if (Component && !Component->GetDeveloperState().PreviewSessionId.IsValid())
        {
            Component->ServerClosePreview(FGuid::NewGuid(), true);
        }
        bAcknowledgedClose = true;
    }
}

void UAbilityRuntimeRecipePanel::CompleteClose()
{
    bAcknowledgedClose = true;
    PendingCloseRequest.Invalidate();
    if (bEmbeddedPage)
    {
        SetVisibility(ESlateVisibility::Collapsed);
        OnClosed.Broadcast();
    }
    else
    {
        RemoveFromParent();
    }
}

void UAbilityRuntimeRecipePanel::RequestClose(bool bAbandon)
{
    if (!Component)
    {
        CompleteClose();
        return;
    }
    if (PendingCloseRequest.IsValid())
    {
        return;
    }
    BindComponent();
    // 先保存请求身份，再发 RPC：Standalone 回执可同步到达，远端 Begin 也可能仍在途中。
    PendingCloseRequest = FGuid::NewGuid();
    LastCloseRequestId = PendingCloseRequest;
    if (RequestedSessionId.IsValid())
    {
        Component->ServerCloseGraphPreview(PendingCloseRequest, RequestedSessionId, bAbandon);
    }
    else if (!Component->GetDeveloperState().PreviewSessionId.IsValid())
    {
        Component->ServerClosePreview(PendingCloseRequest, bAbandon);
    }
    else
    {
        // 从另一个已绑定工作台打开的观察面板不持有它的租约。
        CompleteClose();
    }
}

void UAbilityRuntimeRecipePanel::HideForCasting()
{
    SetVisibility(ESlateVisibility::Collapsed);
    OnHiddenForCasting.Broadcast();
}

TSharedRef<SWidget> UAbilityRuntimeRecipePanel::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("RecipePanelTree"));
    }
    if (!WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return Super::RebuildWidget();
}

void UAbilityRuntimeRecipePanel::NativeConstruct()
{
    Super::NativeConstruct();
    bAcknowledgedClose = false;
    BindComponent();
    RefreshCatalog();
    RefreshState();
}

void UAbilityRuntimeRecipePanel::NativeDestruct()
{
    if (Component)
    {
        Component->OnDeveloperStateChanged.Remove(StateHandle);
        Component->OnPreviewCloseAcknowledged.Remove(CloseHandle);
        if (!bAcknowledgedClose)
        {
            if (RequestedSessionId.IsValid())
            {
                // 销毁后不能再处理普通关闭的基础冲突回执。准确会话的兜底关闭保留外部基础变更，
                // 即使 Begin 或普通关闭仍在可靠通道途中，也只会清理这个面板自己的资源。
                Component->ServerCloseGraphPreview(FGuid::NewGuid(), RequestedSessionId, true);
            }
            else if (!PendingCloseRequest.IsValid() && !Component->GetDeveloperState().PreviewSessionId.IsValid())
            {
                Component->ServerClosePreview(FGuid::NewGuid(), false);
            }
        }
    }
    StateHandle.Reset();
    CloseHandle.Reset();
    PendingCloseRequest.Invalidate();
    OnHiddenForCasting.Broadcast();
    Super::NativeDestruct();
}

void UAbilityRuntimeRecipePanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    UpdatePageSize();
}

void UAbilityRuntimeRecipePanel::UpdatePageSize()
{
    if (!PageBounds)
    {
        return;
    }
    const auto* Controller = GetOwningPlayer();
    const auto* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
    const auto* Viewport = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
    if (!Viewport)
    {
        return;
    }
    FVector2D Pixels;
    Viewport->GetViewportSize(Pixels);
    const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
    if (Pixels.X <= 0.f || Pixels.Y <= 0.f || Scale <= 0.f)
    {
        return;
    }
    // 使用布局单位，让系统 DPI 只缩放一次。为人物窗口页签和四周边框留出空间。
    const FVector2D Available = Pixels / Scale;
    const float Width = FMath::Clamp(Available.X - 64.f, 320.f, 1400.f);
    const float Height = FMath::Clamp(Available.Y - 140.f, 240.f, 940.f);
    if (!FMath::IsNearlyEqual(PageBounds->GetWidthOverride(), Width) ||
        !FMath::IsNearlyEqual(PageBounds->GetHeightOverride(), Height))
    {
        PageBounds->SetWidthOverride(Width);
        PageBounds->SetHeightOverride(Height);
    }
    const float ListHeight = FMath::Clamp(Height * .55f, 160.f, 480.f);
    if (DefinitionSelector && !FMath::IsNearlyEqual(DefinitionSelector->GetMaxListHeight(), ListHeight))
    {
        DefinitionSelector->SetMaxListHeight(ListHeight);
    }
}

void UAbilityRuntimeRecipePanel::ConfigureSelector(UComboBoxString* Selector)
{
    const FLinearColor Text(.035f, .05f, .075f);
    const FSlateColorBrush Normal(FLinearColor(.72f, .78f, .84f));
    const FSlateColorBrush Hovered(FLinearColor(.87f, .92f, .97f));
    const FSlateColorBrush Selected(FLinearColor(.40f, .69f, .86f));
    auto Style = Selector->GetWidgetStyle();
    auto& Button = Style.ComboButtonStyle.ButtonStyle;
    Button.SetNormal(Normal).SetHovered(Hovered).SetPressed(Selected);
    Style.ComboButtonStyle.SetMenuBorderBrush(Normal).SetMenuBorderPadding(FMargin(3.f));
    Selector->SetWidgetStyle(Style);

    auto Rows = Selector->GetItemStyle();
    Rows.SetEvenRowBackgroundBrush(Normal)
        .SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor(.78f, .83f, .88f)))
        .SetEvenRowBackgroundHoveredBrush(Hovered)
        .SetOddRowBackgroundHoveredBrush(Hovered)
        .SetActiveBrush(Selected)
        .SetActiveHoveredBrush(Hovered)
        .SetInactiveBrush(Selected)
        .SetInactiveHoveredBrush(Hovered)
        .SetTextColor(Text)
        .SetSelectedTextColor(Text);
    Selector->SetItemStyle(Rows);
    Selector->SetContentPadding(FMargin(14.f, 10.f));
    Selector->SetMaxListHeight(420.f);
    Selector->OnGenerateWidgetEvent.BindDynamic(this, &ThisClass::GenerateSelectorOption);
}

UWidget* UAbilityRuntimeRecipePanel::GenerateSelectorOption(FString Option)
{
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetText(FText::FromString(Option));
    auto Font = Text->GetFont();
    Font.Size = 18;
    Text->SetFont(Font);
    Text->SetColorAndOpacity(FSlateColor(FLinearColor(.035f, .05f, .075f)));
    Text->SetMargin(FMargin(4.f, 7.f));
    Text->SetAutoWrapText(true);
    return Text;
}

void UAbilityRuntimeRecipePanel::BuildTree()
{
    PageBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RecipePageBounds"));
    PageBounds->SetWidthOverride(1000.f);
    PageBounds->SetHeightOverride(760.f);
    WidgetTree->RootWidget = PageBounds;
    auto* Border = WidgetTree->ConstructWidget<UBorder>();
    Border->SetBrushColor(FLinearColor(.025f, .035f, .055f, .97f));
    Border->SetPadding(FMargin(20.f));
    PageBounds->SetContent(Border);
    UpdatePageSize();
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* ContentScroll = WidgetTree->ConstructWidget<UScrollBox>();
    Border->SetContent(ContentScroll);
    ContentScroll->AddChild(Column);
    auto* Title = WidgetTree->ConstructWidget<UTextBlock>();
    Title->SetText(FText::FromString(bEmbeddedPage ? TEXT("技能改装") : TEXT("法师技能图工作台 · 免费测试")));
    auto TitleFont = Title->GetFont();
    TitleFont.Size = 21;
    Title->SetFont(TitleFont);
    Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 7));
    DefinitionSelector =
        WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipeDefinition"));
    ConfigureSelector(DefinitionSelector);
    Column->AddChildToVerticalBox(DefinitionSelector)->SetPadding(FMargin(0, 4));
    DefinitionSelector->OnSelectionChanged.AddDynamic(this, &ThisClass::DefinitionChanged);
    auto* SessionActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(SessionActions);
    AddButton(SessionActions, TEXT("开始测试"), TEXT("RecipeBegin"))
        ->OnClicked.AddDynamic(this, &ThisClass::BeginClicked);
    AddButton(SessionActions, TEXT("隐藏面板 · 按 T 施法"), TEXT("RecipeHide"))
        ->OnClicked.AddDynamic(this, &ThisClass::HideForCasting);
    AddButton(SessionActions, TEXT("关闭测试"), TEXT("RecipeClose"))
        ->OnClicked.AddDynamic(this, &ThisClass::ClosePanel);

    // 开始失败必须紧邻触发按钮显示，不能藏在尚未出现的图与编辑表单下方。
    StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RecipeStatus"));
    StatusText->SetAutoWrapText(true);
    StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .35f, .3f)));
    Column->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0, 6));
    if (bEmbeddedPage)
    {
        BuildCardPage(Column);
        return;
    }
    auto* UsageHint = WidgetTree->ConstructWidget<UTextBlock>();
    UsageHint->SetText(
        FText::FromString(TEXT("选择法师 GA → 开始测试 → 点击图中的节点/端口/连线 → 预览画布编辑 → 应用草稿。"
                               "直接 PIE 的默认法师无需先选择职业。")));
    UsageHint->SetAutoWrapText(true);
    auto HintFont = UsageHint->GetFont();
    HintFont.Size = 13;
    UsageHint->SetFont(HintFont);
    Column->AddChildToVerticalBox(UsageHint)->SetPadding(FMargin(0, 3));

    auto* GraphActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(GraphActions)->SetPadding(FMargin(0, 6, 0, 0));
    AddButton(GraphActions, TEXT("已接受图"), TEXT("RecipeGraphAccepted"))
        ->OnClicked.AddDynamic(this, &ThisClass::ShowAcceptedGraph);
    DraftViewButton = AddButton(GraphActions, TEXT("草稿图"), TEXT("RecipeGraphDraft"));
    DraftViewButton->OnClicked.AddDynamic(this, &ThisClass::ShowDraftGraph);
    AddButton(GraphActions, TEXT("适应窗口"), TEXT("RecipeGraphFit"))->OnClicked.AddDynamic(this, &ThisClass::FitGraph);
    GraphStatus = WidgetTree->ConstructWidget<UTextBlock>();
    GraphStatus->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(GraphStatus)->SetPadding(FMargin(0, 3));
    GraphCanvas = WidgetTree->ConstructWidget<UAbilityRuntimeGraphCanvas>(UAbilityRuntimeGraphCanvas::StaticClass(),
                                                                          TEXT("RecipeGraphCanvas"));
    GraphCanvas->OnSelectionChanged.AddUObject(this, &ThisClass::GraphSelectionChanged);
    auto* GraphBounds = WidgetTree->ConstructWidget<USizeBox>();
    GraphBounds->SetHeightOverride(325.f);
    GraphBounds->SetContent(GraphCanvas);
    Column->AddChildToVerticalBox(GraphBounds)->SetPadding(FMargin(0, 4));
    GraphSelectionText = WidgetTree->ConstructWidget<UTextBlock>();
    GraphSelectionText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(GraphSelectionText)->SetPadding(FMargin(0, 4));

    auto* Templates = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(Templates)->SetPadding(FMargin(0, 4));
    const auto AddEditSelector = [this, &Templates](UComboBoxString* Selector)
    {
        ConfigureSelector(Selector);
        auto* Slot = Templates->AddChildToHorizontalBox(Selector);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Slot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
    };
    ModifierSelector =
        WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipeModifier"));
    AddEditSelector(ModifierSelector);
    FragmentSelector =
        WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipeFragment"));
    AddEditSelector(FragmentSelector);
    FragmentSelector->OnSelectionChanged.AddDynamic(this, &ThisClass::FragmentChanged);
    ValueLabel = WidgetTree->ConstructWidget<UTextBlock>();
    Column->AddChildToVerticalBox(ValueLabel);
    ValueInput = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("RecipeValue"));
    ValueInput->SetMinValue(0.f);
    ValueInput->SetMaxValue(16.f);
    ValueInput->SetDelta(.1f);
    ValueInput->SetValue(1.f);
    Column->AddChildToVerticalBox(ValueInput)->SetPadding(FMargin(0, 4));
    PropagationInputs = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(PropagationInputs)->SetPadding(FMargin(0, 4));
    CountInput = AddParameter(PropagationInputs, TEXT("总目标数（含首击）"), 1.f, 64.f, 1.f, 4.f, TEXT("RecipeCount"),
                              &CountCaption);
    IntervalInput = AddParameter(PropagationInputs, TEXT("命中后等待（秒）"), .01f, 5.f, .01f, .15f,
                                 TEXT("RecipeInterval"), &IntervalCaption);
    RadiusInput = AddParameter(PropagationInputs, TEXT("下一目标半径（cm）"), 1.f, 3000.f, 25.f, 350.f,
                               TEXT("RecipeRadius"), &RadiusCaption);
    FalloffInput =
        AddParameter(PropagationInputs, TEXT("每跳伤害保留比例"), 0.f, 1.f, .05f, 1.f, TEXT("RecipeFalloff"));
    ForkedInputs = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(ForkedInputs)->SetPadding(FMargin(0, 4));
    DepthInput = AddParameter(ForkedInputs, TEXT("分裂层数（首击为0）"), 0.f, 16.f, 1.f, 2.f, TEXT("RecipeMaxDepth"));
    BranchesInput = AddParameter(ForkedInputs, TEXT("每次命中分裂数"), 1.f, 8.f, 1.f, 2.f, TEXT("RecipeBranches"));
    ModifierSelector->OnSelectionChanged.AddDynamic(this, &ThisClass::ModifierChanged);
    auto* DraftActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(DraftActions)->SetPadding(FMargin(0, 3));
    PreviewEditButton = AddButton(DraftActions, TEXT("预览画布编辑"), TEXT("RecipePreviewEdit"));
    PreviewEditButton->OnClicked.AddDynamic(this, &ThisClass::PreviewGraphClicked);
    ApplyDraftButton = AddButton(DraftActions, TEXT("应用草稿"), TEXT("RecipeApplyDraft"));
    ApplyDraftButton->OnClicked.AddDynamic(this, &ThisClass::ApplyDraftClicked);
    DiscardDraftButton = AddButton(DraftActions, TEXT("放弃草稿"), TEXT("RecipeDiscardDraft"));
    DiscardDraftButton->OnClicked.AddDynamic(this, &ThisClass::DiscardDraftClicked);

    auto* HistoryLabel = WidgetTree->ConstructWidget<UTextBlock>();
    HistoryLabel->SetText(FText::FromString(TEXT("本次测试已应用的编辑")));
    Column->AddChildToVerticalBox(HistoryLabel)->SetPadding(FMargin(0, 6, 0, 2));
    SourceSelector =
        WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipeGraphSource"));
    ConfigureSelector(SourceSelector);
    Column->AddChildToVerticalBox(SourceSelector);
    auto* HistoryActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(HistoryActions);
    UndoButton = AddButton(HistoryActions, TEXT("撤销最近一次"), TEXT("RecipeUndoLast"));
    UndoButton->OnClicked.AddDynamic(this, &ThisClass::UndoLastClicked);
    RemoveSourceButton = AddButton(HistoryActions, TEXT("移除选中编辑"), TEXT("RecipeRemoveSource"));
    RemoveSourceButton->OnClicked.AddDynamic(this, &ThisClass::RemoveSourceClicked);
    AddButton(HistoryActions, TEXT("重置全部编辑"), TEXT("RecipeReset"))
        ->OnClicked.AddDynamic(this, &ThisClass::ResetClicked);

    AddButton(Column, TEXT("切换到基础节点编辑 · 兼容入口"), TEXT("RecipeBaseMode"))
        ->OnClicked.AddDynamic(this, &ThisClass::UseBaseNode);
    NodeSelector = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipeNode"));
    ConfigureSelector(NodeSelector);
    NodeSelector->OnSelectionChanged.AddDynamic(this, &ThisClass::NodeChanged);
    Column->AddChildToVerticalBox(NodeSelector)->SetPadding(FMargin(0, 4));
    PortSelector = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RecipePort"));
    ConfigureSelector(PortSelector);
    Column->AddChildToVerticalBox(PortSelector)->SetPadding(FMargin(0, 4));
    auto* LegacyActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChildToVerticalBox(LegacyActions);
    AddButton(LegacyActions, TEXT("提交基础节点修改"), TEXT("RecipeApply"))
        ->OnClicked.AddDynamic(this, &ThisClass::ApplyClicked);
    AddButton(LegacyActions, TEXT("移除基础节点编辑"), TEXT("RecipeRemove"))
        ->OnClicked.AddDynamic(this, &ThisClass::RemoveClicked);
    AddButton(LegacyActions, TEXT("冲突时保留外部修改并退出"))->OnClicked.AddDynamic(this, &ThisClass::AbandonClicked);
    DefinitionText = WidgetTree->ConstructWidget<UTextBlock>();
    DefinitionText->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(DefinitionText);
    RefreshModifierOptions();
    auto* LogBounds = WidgetTree->ConstructWidget<USizeBox>();
    LogBounds->SetHeightOverride(180.f);
    Column->AddChildToVerticalBox(LogBounds);
    auto* LogScroll = WidgetTree->ConstructWidget<UScrollBox>();
    LogBounds->SetContent(LogScroll);
    ObservationText = WidgetTree->ConstructWidget<UTextBlock>();
    ObservationText->SetAutoWrapText(true);
    LogScroll->AddChild(ObservationText);
}

UButton* UAbilityRuntimeRecipePanel::AddButton(UPanelWidget* Container, const FString& Label, FName WidgetName)
{
    auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetText(FText::FromString(Label));
    auto Font = Text->GetFont();
    Font.Size = 18;
    Text->SetFont(Font);
    Text->SetMargin(FMargin(10.f, 8.f));
    Button->SetContent(Text);
    if (WidgetName == TEXT("RecipeBegin") || WidgetName == TEXT("RecipePreviewEdit"))
    {
        Button->SetBackgroundColor(FLinearColor(.40f, .76f, .85f));
    }
    else if (WidgetName == TEXT("RecipeApplyDraft"))
    {
        Button->SetBackgroundColor(FLinearColor(.66f, .55f, .93f));
    }
    if (auto* Row = Cast<UHorizontalBox>(Container))
    {
        Row->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0, 3, 8, 3));
    }
    else if (auto* Column = Cast<UVerticalBox>(Container))
    {
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 4));
    }
    return Button;
}

USpinBox* UAbilityRuntimeRecipePanel::AddParameter(UHorizontalBox* Row, const FString& Label, float Minimum,
                                                   float Maximum, float Step, float Default, FName WidgetName,
                                                   TObjectPtr<UTextBlock>* Caption)
{
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* ParameterSlot = Row->AddChildToHorizontalBox(Column);
    ParameterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ParameterSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
    auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
    Text->SetText(FText::FromString(Label));
    Text->SetAutoWrapText(true);
    Column->AddChildToVerticalBox(Text);
    if (Caption)
    {
        *Caption = Text;
    }
    auto* Spin = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), WidgetName);
    Spin->SetMinValue(Minimum);
    Spin->SetMaxValue(Maximum);
    Spin->SetDelta(Step);
    Spin->SetValue(Default);
    Column->AddChildToVerticalBox(Spin);
    return Spin;
}

void UAbilityRuntimeRecipePanel::ModifierChanged(FString Selection, ESelectInfo::Type)
{
    if (FragmentSelector)
    {
        const FString Previous = FragmentSelector->GetSelectedOption();
        FragmentSelector->ClearOptions();
        const auto& Options = Selection == ReplaceBranch.ToString()
                                  ? AbilityRuntimeModifierTemplates::GetReplacementFragmentIds()
                                  : AbilityRuntimeModifierTemplates::GetFragmentIds();
        const FAbilityRuntimeGraphPortView* Port = nullptr;
        if (Component && SelectedGraphNode.IsValid())
        {
            Port = FindGraphPort(FindGraphNode(Component->GetDeveloperState().AcceptedGraph, SelectedGraphNode),
                                 SelectedGraphPort);
        }
        for (FName Id : Options)
        {
            if (Selection == InsertRegistered.ToString() && AbilityRuntimeModifierTemplates::IsRuleFragment(Id))
            {
                continue;
            }
            if (!Port || Selection == ReplaceBranch.ToString() || SupportsFragmentPayload(Id, Port->Payload))
            {
                FragmentSelector->AddOption(Id.ToString());
            }
        }
        if (FragmentSelector->FindOptionIndex(Previous) != INDEX_NONE)
        {
            FragmentSelector->SetSelectedOption(Previous);
        }
        else if (FragmentSelector->GetOptionCount() > 0)
        {
            FragmentSelector->SetSelectedIndex(0);
        }
    }
    RefreshMode();
}

void UAbilityRuntimeRecipePanel::FragmentChanged(FString, ESelectInfo::Type)
{
    RefreshMode();
}

void UAbilityRuntimeRecipePanel::NodeChanged(FString, ESelectInfo::Type)
{
    ClearGraphSelection();
    RefreshPorts();
}

void UAbilityRuntimeRecipePanel::RefreshModifierOptions()
{
    if (!ModifierSelector)
    {
        return;
    }
    const FString Previous = ModifierSelector->GetSelectedOption();
    ModifierSelector->ClearOptions();
    const auto* Node =
        Component ? FindGraphNode(Component->GetDeveloperState().AcceptedGraph, SelectedGraphNode) : nullptr;
    const auto* Port = FindGraphPort(Node, SelectedGraphPort);
    const bool bBusinessPort =
        Port && Port->Name != TEXT("Cancelled") && Port->Name != TEXT("Failed") &&
        Port->Payload != EAbilityPortPayload::PreparedInventory &&
        !(Port->Name == TEXT("Apply") && Node->TypeId.ToString().StartsWith(TEXT("PassThrough.")));
    if (SelectedEdgeDestination.IsValid() && Port)
    {
        if (bBusinessPort)
        {
            ModifierSelector->AddOption(InsertRegistered.ToString());
        }
    }
    else if (Port)
    {
        if (bBusinessPort)
        {
            ModifierSelector->AddOption(AppendRegistered.ToString());
        }
        if (Port->Name == TEXT("Launch") && Node->TypeId == TEXT("Control.PrototypeEmitter"))
        {
            ModifierSelector->AddOption(ReplaceBranch.ToString());
        }
    }
    else
    {
        const auto& Options = Node ? Node->SupportedModifiers : AbilityRuntimeModifierTemplates::GetIds();
        for (const FName Id : Options)
        {
            if (Id != InsertRegistered && (!Node || !UsesRegisteredFragment(Id)))
            {
                ModifierSelector->AddOption(Id.ToString());
            }
        }
    }
    if (ModifierSelector->FindOptionIndex(Previous) != INDEX_NONE)
    {
        ModifierSelector->SetSelectedOption(Previous);
    }
    else if (ModifierSelector->GetOptionCount() > 0)
    {
        ModifierSelector->SetSelectedIndex(0);
    }
    ModifierChanged(ModifierSelector->GetSelectedOption(), ESelectInfo::Direct);
}

void UAbilityRuntimeRecipePanel::RefreshPorts()
{
    if (!Component || !DefinitionSelector || !NodeSelector || !PortSelector)
    {
        return;
    }
    const FString Previous = PortSelector->GetSelectedOption();
    PortSelector->ClearOptions();
    const int32 Definition = DefinitionSelector->GetSelectedIndex();
    const int32 Node = NodeSelector->GetSelectedIndex();
    const auto& Rows = Component->GetDefinitions();
    if (!Rows.IsValidIndex(Definition) || !Rows[Definition].Base.DefaultGraph ||
        !Rows[Definition].Base.DefaultGraph->Nodes.IsValidIndex(Node))
    {
        return;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    const auto* Descriptor = Registry.Find(Rows[Definition].Base.DefaultGraph->Nodes[Node].TypeId);
    if (Descriptor)
    {
        for (const auto& Port : Descriptor->Ports)
        {
            FString Error;
            if (AbilityRuntimeModifierTemplates::CanPublishConfiguredPort(
                    Rows[Definition].Base.DefaultGraph->Nodes[Node], Port.Name, Registry, Error))
            {
                PortSelector->AddOption(Port.Name.ToString());
            }
        }
    }
    if (PortSelector->FindOptionIndex(Previous) != INDEX_NONE)
    {
        PortSelector->SetSelectedOption(Previous);
    }
    else if (PortSelector->GetOptionCount() > 0)
    {
        PortSelector->SetSelectedIndex(0);
    }
}

void UAbilityRuntimeRecipePanel::RefreshMode()
{
    const FString Selection = ModifierSelector ? ModifierSelector->GetSelectedOption() : FString();
    const bool bForked = Selection == TEXT("Propagation.Forked");
    const bool bPropagation = bForked || Selection == TEXT("Propagation.Sequential");
    const bool bRegistered = UsesRegisteredFragment(FName(*Selection));
    const FName Fragment = FragmentSelector ? FName(*FragmentSelector->GetSelectedOption()) : NAME_None;
    const bool bQuery = bRegistered && Fragment == TEXT("Query.SelectTargets");
    const bool bKilled = bRegistered && Fragment == TEXT("Condition.Killed");
    const bool bChance = bRegistered && Fragment == TEXT("Condition.Chance");
    const bool bRepeat = bRegistered && Fragment == TEXT("Control.Repeat");
    const bool bWait = bRegistered && Fragment == TEXT("Query.WaitForTarget");
    const bool bWave = bRegistered && Fragment == TEXT("Spatial.Wavefront");
    const bool bDelay = bRegistered && Fragment == TEXT("Control.DelayOrigin");
    const auto CombatRule =
        bRegistered ? AbilityGameplayRuleNodes::FindCombatRule(Fragment) : TOptional<EAbilityCombatRule>{};
    const bool bDamageApplied = CombatRule.IsSet() && CombatRule.GetValue() == EAbilityCombatRule::DamageApplied;
    const bool bShowRadius =
        bPropagation || bQuery || bWait || bWave || (bRegistered && Fragment == TEXT("Area.Petrify"));
    const bool bBeam =
        bRegistered && FragmentSelector && FragmentSelector->GetSelectedOption() == TEXT("Prototype.Beam");
    const bool bStatus =
        bRegistered || Selection == TEXT("Append.Status.Petrify") || Selection == TEXT("Replace.Status.Petrify");
    if (ValueInput && ValueLabel)
    {
        ValueInput->SetIsEnabled(!bPropagation);
        ValueInput->SetMinValue(bStatus ? .01f : 0.f);
        ValueInput->SetMaxValue(bStatus ? 60.f : 16.f);
        ValueLabel->SetText(FText::FromString(bBeam     ? TEXT("新Beam持续时间（秒；射程保持可信Beam资产）")
                                              : bStatus ? TEXT("状态/片段持续时间（秒；减速片段保留倍率0.65）")
                                                        : TEXT("数值倍率")));
        ValueLabel->SetVisibility(bPropagation ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        ValueInput->SetVisibility(bPropagation ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        if (bQuery || bDamageApplied)
        {
            ValueLabel->SetVisibility(ESlateVisibility::Collapsed);
            ValueInput->SetVisibility(ESlateVisibility::Collapsed);
        }
        if (bKilled || bChance)
        {
            ValueInput->SetMinValue(0.f);
            ValueInput->SetMaxValue(1.f);
            ValueInput->SetDelta(bKilled ? 1.f : .05f);
            ValueLabel->SetText(
                FText::FromString(bKilled ? TEXT("通过条件：0未击杀 / 1击杀") : TEXT("通过概率（0..1）")));
        }
        if (bDelay || bRepeat || bWave || bWait)
        {
            ValueLabel->SetText(FText::FromString(bDelay ? TEXT("等待时间（秒）") : TEXT("片段最长持续时间（秒）")));
        }
        if (CombatRule.IsSet() && !bDamageApplied)
        {
            using R = EAbilityCombatRule;
            const auto Rule = CombatRule.GetValue();
            if (Rule == R::EveryNthDamage || Rule == R::DamageTriggerLimit)
            {
                ValueInput->SetMinValue(1.f);
                ValueInput->SetMaxValue(20.f);
                ValueInput->SetDelta(1.f);
                ValueLabel->SetText(FText::FromString(Rule == R::EveryNthDamage ? TEXT("每几次有效伤害触发（1..20）")
                                                                                : TEXT("每次施法触发上限（1..20）")));
            }
            else
            {
                const auto Id = Rule == R::HealthBelow       ? TEXT("Set.HealthThreshold")
                                : Rule == R::ElementalStatus ? TEXT("Set.ElementalStatus")
                                                             : TEXT("Set.ResourceAmount");
                const auto* Parameter = AbilityRuntimeModifierTemplates::FindParameterTemplate(Id);
                ValueInput->SetMinValue(Parameter->Minimum);
                ValueInput->SetMaxValue(Parameter->Maximum);
                ValueInput->SetDelta(Parameter->Step);
                ValueLabel->SetText(FText::FromString(Parameter->Label));
            }
        }
        if (const auto* Parameter = AbilityRuntimeModifierTemplates::FindParameterTemplate(FName(*Selection)))
        {
            ValueInput->SetMinValue(Parameter->Minimum);
            ValueInput->SetMaxValue(Parameter->Maximum);
            ValueInput->SetDelta(Parameter->Step);
            ValueLabel->SetText(FText::FromString(Parameter->Label));
        }
    }
    if (FragmentSelector)
    {
        FragmentSelector->SetVisibility(bRegistered ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    if (PortSelector)
    {
        PortSelector->SetVisibility(bRegistered && !SelectedGraphNode.IsValid() ? ESlateVisibility::Visible
                                                                                : ESlateVisibility::Collapsed);
    }
    if (ForkedInputs)
    {
        ForkedInputs->SetVisibility(bForked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    if (PropagationInputs)
    {
        PropagationInputs->SetVisibility(bShowRadius || bRepeat || bBeam ? ESlateVisibility::Visible
                                                                         : ESlateVisibility::Collapsed);
        for (auto* Input : {CountInput.Get(), IntervalInput.Get(), FalloffInput.Get()})
        {
            if (Input && Input->GetParent())
            {
                Input->GetParent()->SetVisibility(bPropagation || ((bQuery || bRepeat) && Input == CountInput.Get()) ||
                                                          ((bBeam || bRepeat || bWait) && Input == IntervalInput.Get())
                                                      ? ESlateVisibility::Visible
                                                      : ESlateVisibility::Collapsed);
            }
        }
        if (RadiusCaption)
        {
            RadiusCaption->SetText(
                FText::FromString(bPropagation ? TEXT("下一目标半径（cm）") : TEXT("搜索半径（cm）")));
        }
        if (CountCaption)
        {
            CountCaption->SetText(FText::FromString(bQuery    ? TEXT("查询目标数上限（1..16）")
                                                    : bRepeat ? TEXT("脉冲次数")
                                                              : TEXT("总投递数（含首击）")));
        }
        if (CountInput)
        {
            CountInput->SetMaxValue(bQuery ? 16.f : 64.f);
        }
        if (RadiusInput && RadiusInput->GetParent())
        {
            RadiusInput->GetParent()->SetVisibility(bShowRadius ? ESlateVisibility::Visible
                                                                : ESlateVisibility::Collapsed);
        }
        if (IntervalCaption)
        {
            IntervalCaption->SetText(FText::FromString(bBeam     ? TEXT("Beam周期（秒）")
                                                       : bRepeat ? TEXT("脉冲周期（秒）")
                                                       : bWait   ? TEXT("布防等待（秒）")
                                                                 : TEXT("命中后等待（秒）")));
        }
    }
    if (PreviewEditButton)
    {
        const bool bHasArguments = !bRegistered || (FragmentSelector && FragmentSelector->GetSelectedIndex() >= 0);
        PreviewEditButton->SetIsEnabled(Component && GraphCanvas && CanEditGraph(Component->GetDeveloperState()) &&
                                        !GraphCanvas->IsShowingDraft() && SelectedGraphNode.IsValid() &&
                                        !Selection.IsEmpty() && bHasArguments);
    }
    RefreshPorts();
}

void UAbilityRuntimeRecipePanel::RefreshCatalog()
{
    if (!Component || !DefinitionSelector)
    {
        return;
    }
    FString Error;
    if (Component->GetDefinitions().IsEmpty() && !Component->InitializeCatalog(nullptr, Error))
    {
        StatusText->SetText(FText::FromString(Error));
        return;
    }
    if (bEmbeddedPage)
    {
        RefreshCardCatalog();
        return;
    }
    DefinitionSelector->ClearOptions();
    for (const auto& Row : Component->GetDefinitions())
    {
        const FString Marker = !Row.Base.bSelectable        ? TEXT("[保护] ")
                               : !Row.Base.bHasDefaultGraph ? TEXT("[待适配] ")
                                                            : TEXT("");
        DefinitionSelector->AddOption(Marker + Row.Base.AbilityTag.ToString() + TEXT(" · ") +
                                      GetNameSafe(Row.Base.AbilityClass.Get()));
    }
    int32 Selected = Component->GetDefinitions().IndexOfByPredicate(
        [this](const FAbilityRuntimeDeveloperDefinition& Row)
        {
            return Row.DefinitionId == Component->GetDeveloperState().SelectedId;
        });
    DefinitionSelector->SetSelectedIndex(Selected == INDEX_NONE ? 0 : Selected);
}

void UAbilityRuntimeRecipePanel::RefreshState()
{
    if (!Component)
    {
        return;
    }
    const auto& State = Component->GetDeveloperState();
    if (bEmbeddedPage)
    {
        RefreshCardPage(State);
        return;
    }
    // 旧入口可以先启动预览，再构建面板。只在尚未绑定时接管这个旧会话的精确身份，
    // 已由另一工作台发起的会话仍须使用发起面板持有的 nonce。
    if (!RequestedSessionId.IsValid() && State.bPreviewActive && !State.bGraphPanelSession &&
        State.PreviewSessionId.IsValid())
    {
        RequestedSessionId = State.PreviewSessionId;
        bOwnSessionWasActive = true;
    }
    RefreshGraphState(State);
    if (StatusText)
    {
        FString Status = State.Error;
        if (!State.DraftError.IsEmpty() && State.DraftError != State.Error)
        {
            if (!Status.IsEmpty())
            {
                Status += TEXT("\n");
            }
            Status += State.DraftError;
        }
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    }
    if (ObservationText)
    {
        const auto& Records = Component->GetObservations();
        TArray<FString> Lines;
        for (int32 Index = FMath::Max(0, Records.Num() - 12); Index < Records.Num(); ++Index)
        {
            const auto& Record = Records[Index];
            FString Detail;
            if (Record.Kind == EAbilityRuntimeObservationKind::Cost)
            {
                Detail =
                    FString::Printf(TEXT("费用%s %.1f → %.1f"), Record.bCostSucceeded ? TEXT("成功") : TEXT("失败"),
                                    Record.ResourceBefore, Record.ResourceAfter);
            }
            else if (Record.Kind == EAbilityRuntimeObservationKind::GraphFinished)
            {
                Detail = FString::Printf(TEXT("终结原因 %d"), static_cast<int32>(Record.EndReason));
            }
            else
            {
                Detail =
                    Record.TargetLabel +
                    (Record.bActualDamageKnown ? FString::Printf(TEXT(" 伤害 %.1f"), Record.ActualDamage) : TEXT(""));
            }
            Lines.Add(FString::Printf(TEXT("#%lld %.2fs v%d %s %s %s"), Record.Sequence, Record.ServerSeconds,
                                      Record.Revision, *Record.NodeType.ToString(), *Record.Stage.ToString(), *Detail));
        }
        ObservationText->SetText(
            FText::FromString(Lines.IsEmpty() ? TEXT("尚无本玩家执行回执。") : FString::Join(Lines, TEXT("\n"))));
    }
}

bool UAbilityRuntimeRecipePanel::CanEditGraph(const FAbilityRuntimeDeveloperState& State) const
{
    if (!State.bPreviewActive || !State.AcceptedGraph.bReady || !RequestedSessionId.IsValid() ||
        State.PreviewSessionId != RequestedSessionId || PendingCloseRequest.IsValid() || !DefinitionSelector)
    {
        return false;
    }
    const auto& Definitions = Component->GetDefinitions();
    const int32 SelectedDefinition = DefinitionSelector->GetSelectedIndex();
    return Definitions.IsValidIndex(SelectedDefinition) &&
           Definitions[SelectedDefinition].DefinitionId == State.SelectedId;
}

void UAbilityRuntimeRecipePanel::RefreshGraphState(const FAbilityRuntimeDeveloperState& State)
{
    if (!GraphCanvas)
    {
        return;
    }
    bOwnSessionWasActive =
        bOwnSessionWasActive || (RequestedSessionId.IsValid() && State.PreviewSessionId == RequestedSessionId);
    const bool bBindingChanged =
        DisplayedSessionId != State.PreviewSessionId || DisplayedGraphDefinition != State.SelectedId;
    const bool bAcceptedChanged = bBindingChanged || DisplayedGraphRevision != State.AcceptedGraph.Revision ||
                                  DisplayedGraphHash != State.AcceptedGraph.ContentHash;
    if (bBindingChanged)
    {
        DisplayedSessionId = State.PreviewSessionId;
        DisplayedGraphDefinition = State.SelectedId;
        GraphCanvas->SetGraphViews({}, {});
        ClearGraphSelection();
    }
    DisplayedGraphRevision = State.AcceptedGraph.Revision;
    DisplayedGraphHash = State.AcceptedGraph.ContentHash;
    GraphCanvas->SetGraphViews(State.AcceptedGraph, State.DraftGraph);
    if (DisplayedDraftToken != State.DraftToken)
    {
        DisplayedDraftToken = State.DraftToken;
        GraphCanvas->ShowDraft(DisplayedDraftToken.IsValid());
    }

    if (DisplayedSources != State.AcceptedGraph.AppliedSources)
    {
        const FString Previous = SourceSelector->GetSelectedOption();
        DisplayedSources = State.AcceptedGraph.AppliedSources;
        SourceSelector->ClearOptions();
        for (const FName Source : DisplayedSources)
        {
            SourceSelector->AddOption(Source.ToString());
        }
        if (SourceSelector->FindOptionIndex(Previous) != INDEX_NONE)
        {
            SourceSelector->SetSelectedOption(Previous);
        }
        else if (!DisplayedSources.IsEmpty())
        {
            SourceSelector->SetSelectedIndex(DisplayedSources.Num() - 1);
        }
    }
    const bool bCanEdit = CanEditGraph(State);
    const bool bHasDraft = State.DraftToken.IsValid() && State.DraftGraph.bReady;
    DraftViewButton->SetIsEnabled(bHasDraft);
    ApplyDraftButton->SetIsEnabled(bCanEdit && bHasDraft);
    DiscardDraftButton->SetIsEnabled(bCanEdit && State.DraftToken.IsValid());
    UndoButton->SetIsEnabled(bCanEdit && !DisplayedSources.IsEmpty());
    RemoveSourceButton->SetIsEnabled(bCanEdit && !DisplayedSources.IsEmpty());
    SourceSelector->SetIsEnabled(bCanEdit && !DisplayedSources.IsEmpty());
    if (bAcceptedChanged && SelectedGraphNode.IsValid())
    {
        const auto* Node = FindGraphNode(State.AcceptedGraph, SelectedGraphNode);
        if (!Node)
        {
            ClearGraphSelection();
        }
        else
        {
            if (!FindGraphPort(Node, SelectedGraphPort))
            {
                SelectedGraphPort = NAME_None;
            }
            if (SelectedEdgeDestination.IsValid() && !State.AcceptedGraph.Edges.ContainsByPredicate(
                                                         [this](const FAbilityRuntimeGraphEdgeView& Edge)
                                                         {
                                                             return Edge.From == SelectedGraphNode &&
                                                                    Edge.OutputPort == SelectedGraphPort &&
                                                                    Edge.To == SelectedEdgeDestination;
                                                         }))
            {
                // 旧边被替换时保留源端口，后续点击已接受图中的新边仍使用新目的 ID。
                SelectedEdgeDestination.Invalidate();
            }
            GraphCanvas->SetSelection(SelectedGraphNode, SelectedGraphPort, SelectedEdgeDestination);
            RefreshModifierOptions();
        }
    }
    RefreshGraphSelection(State);
}

void UAbilityRuntimeRecipePanel::RefreshGraphSelection(const FAbilityRuntimeDeveloperState& State)
{
    const bool bDraft = GraphCanvas->IsShowingDraft();
    const auto& Graph = bDraft ? State.DraftGraph : State.AcceptedGraph;
    const FString Status =
        Graph.bReady ? FString::Printf(TEXT("%s  ·  版本 %d  ·  %d 节点 / %d 连线  ·  滚轮缩放，拖动空白平移"),
                                       bDraft ? TEXT("草稿 · 尚未应用") : TEXT("服务器已接受"), Graph.Revision,
                                       Graph.Nodes.Num(), Graph.Edges.Num())
                     : TEXT("选择法师技能，开始免费测试");
    GraphStatus->SetText(FText::FromString(Status));
    GraphStatus->SetColorAndOpacity(bDraft ? FLinearColor(.76f, .62f, 1.f) : FLinearColor(.48f, .86f, .95f));
    FString Details;
    if (bDraft)
    {
        Details = TEXT("草稿只读。绿色为新增，橙色为修改。");
    }
    else if (const auto* Node = FindGraphNode(Graph, SelectedGraphNode))
    {
        Details = Node->Label + TEXT("  ·  ") + Node->TypeId.ToString();
        if (const auto* Port = FindGraphPort(Node, SelectedGraphPort))
        {
            Details += TEXT("  ·  ") + Port->Name.ToString();
            Details += TEXT(" (") +
                       StaticEnum<EAbilityPortPayload>()->GetNameStringByValue(static_cast<int64>(Port->Payload)) +
                       TEXT(")");
        }
        if (const auto* To = FindGraphNode(Graph, SelectedEdgeDestination))
        {
            Details += TEXT("  →  ") + To->Label + TEXT("  ·  ") + To->TypeId.ToString();
            if (ModifierSelector->GetSelectedOption() == InsertRegistered.ToString())
            {
                Details += TEXT("\n片段在此连线的原后继之前运行。");
            }
        }
        else if (FName(*ModifierSelector->GetSelectedOption()) == AppendRegistered)
        {
            Details += TEXT("\n从此端口新增并行分支，保留已有后继。");
        }
        if (ModifierSelector->GetOptionCount() == 0)
        {
            Details += TEXT("\n当前选择没有可用编辑。");
        }
        Details += TEXT("\n") + Node->ParameterSummary;
    }
    else if (Graph.bReady)
    {
        Details = TEXT("点节点改参数或替换策略，点输出端口追加分支，点连线插入片段。");
    }
    if (Graph.bReady && !CanEditGraph(State) && !bDraft)
    {
        Details += TEXT("\n请使用本面板启动的测试会话，并选回该技能。");
    }
    GraphSelectionText->SetText(FText::FromString(Details.TrimStartAndEnd()));
    const FString Modifier = ModifierSelector->GetSelectedOption();
    PreviewEditButton->SetIsEnabled(
        !bDraft && CanEditGraph(State) && SelectedGraphNode.IsValid() && !Modifier.IsEmpty() &&
        (!UsesRegisteredFragment(FName(*Modifier)) || FragmentSelector->GetSelectedIndex() >= 0));
}

void UAbilityRuntimeRecipePanel::GraphSelectionChanged(FGuid NodeId, FName PortId, FGuid EdgeTo)
{
    SelectedGraphNode = NodeId;
    SelectedGraphPort = PortId;
    SelectedEdgeDestination = EdgeTo;
    RefreshModifierOptions();
    if (Component)
    {
        RefreshGraphSelection(Component->GetDeveloperState());
    }
}

void UAbilityRuntimeRecipePanel::ClearGraphSelection()
{
    if (!SelectedGraphNode.IsValid())
    {
        return;
    }
    SelectedGraphNode.Invalidate();
    SelectedGraphPort = NAME_None;
    SelectedEdgeDestination.Invalidate();
    GraphCanvas->SetSelection(FGuid());
    RefreshModifierOptions();
    if (Component)
    {
        RefreshGraphSelection(Component->GetDeveloperState());
    }
}

void UAbilityRuntimeRecipePanel::CloseAcknowledged(FGuid RequestId, bool bSucceeded)
{
    if (!PendingCloseRequest.IsValid() || RequestId != PendingCloseRequest)
    {
        return;
    }
    PendingCloseRequest.Invalidate();
    const FGuid CurrentSession = Component->GetDeveloperState().PreviewSessionId;
    if (bSucceeded ||
        (RequestedSessionId.IsValid() && CurrentSession.IsValid() && CurrentSession != RequestedSessionId))
    {
        CompleteClose();
    }
    else
    {
        RefreshState();
    }
}

void UAbilityRuntimeRecipePanel::BindComponent()
{
    if (Component)
    {
        if (!StateHandle.IsValid())
        {
            StateHandle = Component->OnDeveloperStateChanged.AddUObject(this, &ThisClass::RefreshState);
        }
        if (!CloseHandle.IsValid())
        {
            CloseHandle = Component->OnPreviewCloseAcknowledged.AddUObject(this, &ThisClass::CloseAcknowledged);
        }
    }
}

void UAbilityRuntimeRecipePanel::DefinitionChanged(FString, ESelectInfo::Type)
{
    if (!Component || !NodeSelector || !DefinitionSelector)
    {
        return;
    }
    ClearGraphSelection();
    NodeSelector->ClearOptions();
    const auto& Definitions = Component->GetDefinitions();
    const int32 Index = DefinitionSelector->GetSelectedIndex();
    if (!Definitions.IsValidIndex(Index))
    {
        return;
    }
    const auto& Row = Definitions[Index];
    FString Details = Row.Base.DisplayName.ToString() + TEXT("\n") + Row.Base.Diagnostic;
    Details += FString::Printf(
        TEXT("\n实际前端 %d | Actor %s | Mass %s | 目标合同 %d"), static_cast<int32>(Row.Base.FrontendKind),
        Row.Base.bSupportsActorTargets ? TEXT("支持") : TEXT("不支持"),
        Row.Base.bSupportsMassTargets ? TEXT("支持") : TEXT("不支持"), static_cast<int32>(Row.Base.TargetContract));
    if (const auto* Graph = Row.Base.DefaultGraph.Get())
    {
        Details += FString::Printf(
            TEXT("\nProfile %s | BudgetPolicy %d | 调用 %d / 事件 %d / 活动 %d / 深度 %d / 每帧事件 %d"),
            *Graph->CompatibilityProfile.ToString(), static_cast<int32>(Graph->BudgetPolicy),
            Graph->Budget.MaxInvocations, Graph->Budget.MaxEvents, Graph->Budget.MaxActiveNodes, Graph->Budget.MaxDepth,
            Graph->Budget.MaxEventsPerTick);
    }
    DefinitionText->SetText(FText::FromString(Details));
    if (const auto* Graph = Row.Base.DefaultGraph.Get())
    {
        for (int32 Node = 0; Node < Graph->Nodes.Num(); ++Node)
        {
            const auto& Definition = Graph->Nodes[Node];
            FString Label = FString::Printf(TEXT("%d · %s"), Node, *Definition.TypeId.ToString());
            if (const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Definition.Config);
                Scope && Scope->Parameters)
            {
                // 同一技能可有多个Scope。显示原片段及单弹/分发容器身份，避免把父时钟误作孩子伤害参数。
                const FString Name = Scope->Parameters->DisplayName.IsEmpty()
                                         ? GetNameSafe(Scope->StableReference.Get())
                                         : Scope->Parameters->DisplayName.ToString();
                Label += TEXT(" · ") + Name;
                if (Scope->Parameters->Kind == EAbilityPrototypeKind::Multishot)
                {
                    Label += Scope->bSingleProjectile ? TEXT(" · 单颗弹体") : TEXT(" · 分发容器");
                }
            }
            NodeSelector->AddOption(Label);
        }
        NodeSelector->SetSelectedIndex(0);
    }
    RefreshGraphState(Component->GetDeveloperState());
}

void UAbilityRuntimeRecipePanel::BeginClicked()
{
    if (!Component || !DefinitionSelector)
    {
        return;
    }
    const int32 Index = DefinitionSelector->GetSelectedIndex();
    if (bEmbeddedPage)
    {
        if (CardDefinitionIds.IsValidIndex(Index) && !PendingCloseRequest.IsValid())
        {
            const auto& State = Component->GetDeveloperState();
            if (!RequestedSessionId.IsValid() || (bOwnSessionWasActive && !State.bPreviewActive))
            {
                RequestedSessionId = FGuid::NewGuid();
                bOwnSessionWasActive = false;
            }
            Component->ServerBeginCardPreview(CardDefinitionIds[Index], RequestedSessionId);
        }
        return;
    }
    if (Component->GetDefinitions().IsValidIndex(Index))
    {
        if (PendingCloseRequest.IsValid())
        {
            return;
        }
        const auto& State = Component->GetDeveloperState();
        if (!RequestedSessionId.IsValid() || (bOwnSessionWasActive && !State.bPreviewActive))
        {
            RequestedSessionId = FGuid::NewGuid();
            bOwnSessionWasActive = false;
        }
        // 同一面板的重复 Begin 使用原 nonce；尚未复制状态时的关闭也能准确回收这次请求。
        Component->ServerBeginGraphPreview(Component->GetDefinitions()[Index].DefinitionId, RequestedSessionId);
    }
}

bool UAbilityRuntimeRecipePanel::ReadEdit(FAbilityRuntimeDeveloperEdit& Edit) const
{
    if (!Component || !NodeSelector || !ModifierSelector || !ValueInput || PendingCloseRequest.IsValid())
    {
        return false;
    }
    const auto& State = Component->GetDeveloperState();
    const int32 Index = DefinitionSelector ? DefinitionSelector->GetSelectedIndex() : INDEX_NONE;
    if (!Component->GetDefinitions().IsValidIndex(Index) ||
        (State.PreviewSessionId.IsValid() && State.PreviewSessionId != RequestedSessionId))
    {
        return false;
    }
    Edit.DefinitionId = Component->GetDefinitions()[Index].DefinitionId;
    Edit.ModifierId = FName(*ModifierSelector->GetSelectedOption());
    // 面板拥有的工作台会话也约束基础下标入口；旧工具的无会话流程继续发送空 ID。
    Edit.ExpectedPreviewSessionId = State.PreviewSessionId.IsValid() ? RequestedSessionId : FGuid();
    if (UsesRegisteredFragment(Edit.ModifierId))
    {
        Edit.FragmentId = FName(*FragmentSelector->GetSelectedOption());
        Edit.OutputPortId = SelectedGraphNode.IsValid() ? SelectedGraphPort : FName(*PortSelector->GetSelectedOption());
    }
    if (SelectedGraphNode.IsValid())
    {
        // 用点击时所见的已接受图身份和当前租约，避免用草稿节点或隐式基础下标发请求。
        if (!GraphCanvas || GraphCanvas->IsShowingDraft() || !CanEditGraph(State) ||
            (UsesRegisteredFragment(Edit.ModifierId) && (Edit.FragmentId.IsNone() || Edit.OutputPortId.IsNone())))
        {
            return false;
        }
        Edit.TargetNodeId = SelectedGraphNode;
        Edit.TargetEdgeTo = SelectedEdgeDestination;
        Edit.ExpectedAcceptedHash = State.AcceptedGraph.ContentHash;
    }
    else
    {
        Edit.NodeIndex = NodeSelector->GetSelectedIndex();
    }
    Edit.ExpectedRevision = State.Revision;
    Edit.ExpectedBaseHash = State.BaseHash;
    Edit.Value = ValueInput->GetValue();
    Edit.Count = CountInput ? FMath::RoundToInt(CountInput->GetValue()) : 4;
    Edit.Interval = IntervalInput ? IntervalInput->GetValue() : .15f;
    Edit.Radius = RadiusInput ? RadiusInput->GetValue() : 350.f;
    Edit.Falloff = FalloffInput ? FalloffInput->GetValue() : 1.f;
    Edit.MaxDepth = DepthInput ? FMath::RoundToInt(DepthInput->GetValue()) : 2;
    Edit.BranchesPerTarget = BranchesInput ? FMath::RoundToInt(BranchesInput->GetValue()) : 2;
    return !Edit.ModifierId.IsNone();
}

void UAbilityRuntimeRecipePanel::ApplyClicked()
{
    if (SelectedGraphNode.IsValid())
    {
        PreviewGraphClicked();
        return;
    }
    FAbilityRuntimeDeveloperEdit Edit;
    if (ReadEdit(Edit))
    {
        Component->ServerApplyEdit(Edit);
    }
}

void UAbilityRuntimeRecipePanel::PreviewGraphClicked()
{
    FAbilityRuntimeDeveloperEdit Edit;
    if (SelectedGraphNode.IsValid() && ReadEdit(Edit))
    {
        Component->ServerPreviewGraphEdit(Edit);
    }
}

void UAbilityRuntimeRecipePanel::ApplyDraftClicked()
{
    if (Component)
    {
        const auto& State = Component->GetDeveloperState();
        if (CanEditGraph(State) && State.DraftToken.IsValid() && State.DraftGraph.bReady)
        {
            Component->ServerApplyGraphDraft(State.DraftToken, State.Revision);
        }
    }
}

void UAbilityRuntimeRecipePanel::DiscardDraftClicked()
{
    if (Component)
    {
        const auto& State = Component->GetDeveloperState();
        if (CanEditGraph(State) && State.DraftToken.IsValid())
        {
            Component->ServerDiscardGraphDraft(State.DraftToken);
        }
    }
}

void UAbilityRuntimeRecipePanel::UndoLastClicked()
{
    if (Component)
    {
        const auto& State = Component->GetDeveloperState();
        if (CanEditGraph(State) && !State.AcceptedGraph.AppliedSources.IsEmpty())
        {
            Component->ServerRemoveGraphSource(State.AcceptedGraph.AppliedSources.Last(), State.Revision);
        }
    }
}

void UAbilityRuntimeRecipePanel::RemoveSourceClicked()
{
    if (Component && SourceSelector)
    {
        const auto& State = Component->GetDeveloperState();
        const FName SourceId(*SourceSelector->GetSelectedOption());
        if (CanEditGraph(State) && State.AcceptedGraph.AppliedSources.Contains(SourceId))
        {
            Component->ServerRemoveGraphSource(SourceId, State.Revision);
        }
    }
}

void UAbilityRuntimeRecipePanel::ShowAcceptedGraph()
{
    GraphCanvas->ShowDraft(false);
    if (Component)
    {
        RefreshGraphSelection(Component->GetDeveloperState());
    }
}

void UAbilityRuntimeRecipePanel::ShowDraftGraph()
{
    GraphCanvas->ShowDraft(true);
    if (Component)
    {
        RefreshGraphSelection(Component->GetDeveloperState());
    }
}

void UAbilityRuntimeRecipePanel::FitGraph()
{
    GraphCanvas->FitGraph();
}

void UAbilityRuntimeRecipePanel::UseBaseNode()
{
    ShowAcceptedGraph();
    ClearGraphSelection();
}

void UAbilityRuntimeRecipePanel::RemoveClicked()
{
    if (SelectedGraphNode.IsValid())
    {
        RemoveSourceClicked();
        return;
    }
    FAbilityRuntimeDeveloperEdit Edit;
    if (ReadEdit(Edit))
    {
        Component->ServerRemoveEdit(Edit);
    }
}

void UAbilityRuntimeRecipePanel::ResetClicked()
{
    if (Component && !PendingCloseRequest.IsValid())
    {
        const auto& State = Component->GetDeveloperState();
        if (State.PreviewSessionId.IsValid())
        {
            if (State.PreviewSessionId == RequestedSessionId)
            {
                Component->ServerResetGraphPreview(RequestedSessionId, State.Revision);
            }
        }
        else if (!RequestedSessionId.IsValid())
        {
            Component->ServerResetPreview(State.SelectedId, State.Revision, State.BaseHash);
        }
    }
}

void UAbilityRuntimeRecipePanel::AbandonClicked()
{
    RequestClose(true);
}
