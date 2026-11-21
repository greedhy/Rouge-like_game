// 调用链：HUD 快捷入口或背包装备选中 → 人物菜单切页 → 单一游戏内菜单输入注册。
#pragma once

#include "UI/Widget/GameplayMenuWidget.h"
#include "CharacterMenuWidget.generated.h"

class UEquipmentPanel;
class UAbilityRuntimeRecipePanel;
class SBox;
class SWidgetSwitcher;

/** 同一窗口持有属性、装备、技能和技能组装四页，统一管理输入与工作台会话。 */
UCLASS()
class TEST_25D_API UCharacterMenuWidget : public UGameplayMenuWidget
{
    GENERATED_BODY()

public:
    void InitializePages(TSubclassOf<UUserWidget> AttributeClass, TSubclassOf<UUserWidget> SpellClass);
    void ShowPage(int32 PageIndex);
    virtual void SetVisibility(ESlateVisibility InVisibility) override;
    /** 普通关闭等待工作台准确回执；隐藏试放是保留租约的独立动作。 */
    void RequestCloseMenu();
    UFUNCTION(BlueprintPure, Category = "UI")
    int32 GetActivePage() const
    {
        return ActivePage;
    }
    UFUNCTION(BlueprintPure, Category = "UI")
    UAbilityRuntimeRecipePanel* GetRuntimeRecipePanel() const
    {
        return RecipePanel;
    }

protected:
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    /** 惰性创建唯一工作台；没有角色或组件时保持可重试的错误提示。 */
    void EnsureRecipePage();
    void HideForRecipeCasting();
    void RecipeClosed();
    void DeselectSkillSlot();
    UPROPERTY(Transient)
    TObjectPtr<UAbilityRuntimeRecipePanel> RecipePanel;
    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> Attributes;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> Skills;

    UPROPERTY(Transient)
    TObjectPtr<UEquipmentPanel> Equipment;

    TSharedPtr<SWidgetSwitcher> Pages;
    TSharedPtr<SBox> RecipePageHost;
    int32 ActivePage = 0;
};
