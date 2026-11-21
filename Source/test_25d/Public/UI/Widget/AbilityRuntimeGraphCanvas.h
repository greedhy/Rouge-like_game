// 调用链：所属玩家的配方回执 → SetGraphViews → Slate 布局、绘制和命中测试 → 选择委托 → 面板。
#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "AbilityRuntimeGraphCanvas.generated.h"

class SAbilityRuntimeGraphCanvas;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FAbilityRuntimeGraphSelectionChanged, FGuid, FName, FGuid);

/** 运行时图视图。选择使用服务器原始身份，草稿显示只读。 */
UCLASS()
class TEST_25D_API UAbilityRuntimeGraphCanvas : public UWidget
{
    GENERATED_BODY()

public:
    /** 缓存两份服务器视图；回执变化才重排版，观察日志刷新不重置相机。 */
    void SetGraphViews(const FAbilityRuntimeGraphView& Accepted, const FAbilityRuntimeGraphView& Draft);
    /** 编辑选择；空端口代表节点，空后继代表端口。程序同步不会广播。 */
    void SetSelection(FGuid NodeId, FName PortId = NAME_None, FGuid EdgeTo = FGuid());
    /** 有草稿时切换只读比对；草稿应用或取消后返回已接受图。 */
    void ShowDraft(bool bShow);
    bool IsShowingDraft() const;
    /** 将当前图定位到可见区域；选点与画线都使用同一缩放变换。 */
    void FitGraph();

    /** 返回实际画布局部坐标，可供自动化将 Slate 点击定位到实际节点。 */
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool GetCanvasNodePosition(FGuid NodeId, FVector2D& Position) const;
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool GetCanvasPortPosition(FGuid NodeId, FName PortId, FVector2D& Position) const;
    UFUNCTION(BlueprintPure, Category = "Development|RuntimeRecipe")
    bool GetCanvasEdgePosition(FGuid From, FName PortId, FGuid To, float Fraction, FVector2D& Position) const;

    FAbilityRuntimeGraphSelectionChanged OnSelectionChanged;

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    /** 重建显示视图和选择；只在切换实际图、草稿和已接受版本时调用。 */
    void RefreshSlateGraph();
    /** UI 输入回传唯一身份，供面板验证当前已接受版本。 */
    void SelectFromSlate(FGuid NodeId, FName PortId, FGuid EdgeTo);

    FAbilityRuntimeGraphView AcceptedView;
    FAbilityRuntimeGraphView DraftView;
    FGuid SelectedNode;
    FName SelectedPort;
    FGuid SelectedDestination;
    TSharedPtr<SAbilityRuntimeGraphCanvas> SlateCanvas;
    bool bShowingDraft = false;
};
