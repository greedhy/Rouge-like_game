// 调用链：人物窗口第四页/调试入口创建原生 UMG -> 所属 PlayerState 回执 -> 画布选择节点/端口/边 -> 版本校验 RPC。
// 面板只拥有自己的焦点与委托，关闭时请求归还临时资源，不持有或执行服务器图。
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AbilityRuntimeRecipePanel.generated.h"

class UAbilityRuntimeRecipeDeveloperComponent;
class UAbilityRuntimeGraphCanvas;
class UComboBoxString;
class USpinBox;
class UTextBlock;
class UVerticalBox;
class UButton;
class UHorizontalBox;
class UPanelWidget;
class UNativeWidgetHost;
class USizeBox;
struct FAbilityRuntimeDeveloperEdit;
struct FAbilityRuntimeDeveloperState;

DECLARE_MULTICAST_DELEGATE(FAbilityRuntimeRecipePanelHidden);

UCLASS()
class TEST_25D_API UAbilityRuntimeRecipePanel : public UUserWidget
{
    GENERATED_BODY()
public:
    /** 命令入口注入本玩家组件，不能指定其他玩家状态或任意图。 */
    void InitializePanel(UAbilityRuntimeRecipeDeveloperComponent* InComponent);
    /** 内嵌工作台由人物窗口管理显示和输入；准确关闭回执交还宿主，不移除整个人物窗口。 */
    void SetEmbeddedPage();
    /** 重开已确认关闭的同一页面；切页/隐藏试放时保留原会话，不能重新初始化租约。 */
    void ShowEmbeddedPage();
    /** 宿主销毁时精确放弃本页租约，保留其他系统写入的基础图。 */
    void AbandonEmbeddedPage();
    /** 已收到本页的关闭确认；隐藏试放仍返回 false。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool IsSessionClosed() const
    {
        return bAcknowledgedClose;
    }
    /** 显式关闭时请求服务器结束预览；普通隐藏可用于继续实际鼠标施法。 */
    UFUNCTION()
    void ClosePanel();
    /** 保留预览并隐藏界面，控制器恢复游戏输入后可使用真实鼠标瞄准和原 GA。 */
    UFUNCTION()
    void HideForCasting();
    /** 只读最近一次本面板关闭身份；回执后仍保留，便于准确核对异步关闭与资源归还。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    FGuid GetLastCloseRequestId() const
    {
        return LastCloseRequestId;
    }
    /** 只读所属Slate用户的实际视口焦点路径；不发送输入、不转移焦点，也不以施法成功猜测焦点。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool HasOwnerViewportFocus() const;
    FAbilityRuntimeRecipePanelHidden OnHiddenForCasting;
    FAbilityRuntimeRecipePanelHidden OnClosed;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;

private:
    bool bEmbeddedPage = false;
    UPROPERTY(Transient)
    TObjectPtr<USizeBox> PageBounds;
    /** 依据所属游戏视口的 DPI 后尺寸调整页面，滚动区域保留完整按钮大小。 */
    void UpdatePageSize();
    /** 下拉控件的关闭态、列表行和选中态共用清晰的浅色样式。 */
    void ConfigureSelector(UComboBoxString* Selector);
    UFUNCTION()
    UWidget* GenerateSelectorOption(FString Option);
    /** 第四页按卡片协议显示，调试入口仍由原画布构建。 */
    UPROPERTY(Transient)
    TObjectPtr<UNativeWidgetHost> CardHost;
    TArray<FName> CardDefinitionIds;
    void BuildCardPage(UVerticalBox* Column);
    void RefreshCardPage(const FAbilityRuntimeDeveloperState& State);
    void RefreshCardCatalog();
    void CompleteClose();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityRuntimeRecipeDeveloperComponent> Component;
    UPROPERTY(Transient)
    TObjectPtr<UAbilityRuntimeGraphCanvas> GraphCanvas;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> GraphStatus;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> GraphSelectionText;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> SourceSelector;
    UPROPERTY(Transient)
    TObjectPtr<UButton> DraftViewButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> PreviewEditButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> ApplyDraftButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> DiscardDraftButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> UndoButton;
    UPROPERTY(Transient)
    TObjectPtr<UButton> RemoveSourceButton;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> DefinitionSelector;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> NodeSelector;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> ModifierSelector;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> FragmentSelector;
    UPROPERTY(Transient)
    TObjectPtr<UComboBoxString> PortSelector;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> ValueInput;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> ValueLabel;
    UPROPERTY(Transient)
    TObjectPtr<UHorizontalBox> PropagationInputs;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> CountInput;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> CountCaption;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> IntervalInput;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> RadiusInput;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> FalloffInput;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> RadiusCaption;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> IntervalCaption;
    UPROPERTY(Transient)
    TObjectPtr<UHorizontalBox> ForkedInputs;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> DepthInput;
    UPROPERTY(Transient)
    TObjectPtr<USpinBox> BranchesInput;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> DefinitionText;
    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> ObservationText;
    FDelegateHandle StateHandle;
    FDelegateHandle CloseHandle;
    FGuid PendingCloseRequest;
    FGuid LastCloseRequestId;
    FGuid RequestedSessionId;
    FGuid DisplayedSessionId;
    FGuid DisplayedDraftToken;
    FName DisplayedGraphDefinition;
    FString DisplayedGraphHash;
    int32 DisplayedGraphRevision = INDEX_NONE;
    FGuid SelectedGraphNode;
    FName SelectedGraphPort;
    FGuid SelectedEdgeDestination;
    TArray<FName> DisplayedSources;
    bool bOwnSessionWasActive = false;
    bool bAcknowledgedClose = false;

    /** 构建本地控件，无 Blueprint 资产、反射任意属性或客户端生成图。 */
    void BuildTree();
    /** 绑定本玩家回执并重建显示行；实际批准以服务器 State 为准。 */
    void RefreshCatalog();
    /** 只更新诊断、版本及最近真实回执，显式关闭仅由匹配请求确认。 */
    void RefreshState();
    /** 按当前所有者回执更新画布、精确目标与按提交顺序排列的来源。 */
    void RefreshGraphState(const FAbilityRuntimeDeveloperState& State);
    /** 草稿只读。精确选择消失时退回存活的端点或提示重新选择。 */
    void RefreshGraphSelection(const FAbilityRuntimeDeveloperState& State);
    void GraphSelectionChanged(FGuid NodeId, FName PortId, FGuid EdgeTo);
    void ClearGraphSelection();
    /** 基础节点下拉仍供旧工具使用，画布操作只列当前选择种类对应的模板。 */
    void RefreshModifierOptions();
    bool CanEditGraph(const FAbilityRuntimeDeveloperState& State) const;
    bool ReadEdit(FAbilityRuntimeDeveloperEdit& Edit) const;
    /** 只认本面板发出的精确请求回执；旧 Error/其他属性刷新不结束关闭等待。 */
    void CloseAcknowledged(FGuid RequestId, bool bSucceeded);
    void RequestClose(bool bAbandon);
    void BindComponent();
    UButton* AddButton(UPanelWidget* Container, const FString& Label, FName WidgetName = NAME_None);
    /** 添加带单位的本地有界输入；服务器仍独立验证所有参数，控件范围不是权限依据。 */
    USpinBox* AddParameter(UHorizontalBox* Row, const FString& Label, float Minimum, float Maximum, float Step,
                           float Default, FName WidgetName = NAME_None, TObjectPtr<UTextBlock>* Caption = nullptr);
    /** 只列本地可信目录所选节点已声明的业务端口；服务器仍验证准确类型及配置能力。 */
    void RefreshPorts();
    /** 更新参数单位/可见性，不发RPC、不改变当前已接受版本。 */
    void RefreshMode();
    UFUNCTION()
    void DefinitionChanged(FString Selection, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void ModifierChanged(FString Selection, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void FragmentChanged(FString Selection, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void NodeChanged(FString Selection, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void BeginClicked();
    UFUNCTION()
    void ApplyClicked();
    UFUNCTION()
    void PreviewGraphClicked();
    UFUNCTION()
    void ApplyDraftClicked();
    UFUNCTION()
    void DiscardDraftClicked();
    UFUNCTION()
    void UndoLastClicked();
    UFUNCTION()
    void RemoveSourceClicked();
    UFUNCTION()
    void ShowAcceptedGraph();
    UFUNCTION()
    void ShowDraftGraph();
    UFUNCTION()
    void FitGraph();
    UFUNCTION()
    void UseBaseNode();
    /** 只撤当前服务器租约拥有的编辑，保留临时Spec/外部来源/已冻结施法。 */
    UFUNCTION()
    void RemoveClicked();
    /** 事务撤回本预览全部编辑；关闭预览是另一条明确生命周期操作。 */
    UFUNCTION()
    void ResetClicked();
    UFUNCTION()
    void AbandonClicked();
};
