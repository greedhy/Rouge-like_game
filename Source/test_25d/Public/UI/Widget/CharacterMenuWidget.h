// 调用链：HUD 快捷入口或背包装备选中 → 人物菜单切页 → 单一游戏内菜单输入注册。
#pragma once

#include "UI/Widget/GameplayMenuWidget.h"
#include "CharacterMenuWidget.generated.h"

class UEquipmentPanel;
class SWidgetSwitcher;

/** 将现有属性和技能蓝图与原生装备页放在同一个窗口。 */
UCLASS()
class TEST_25D_API UCharacterMenuWidget : public UGameplayMenuWidget
{
    GENERATED_BODY()

public:
    void InitializePages(TSubclassOf<UUserWidget> AttributeClass, TSubclassOf<UUserWidget> SpellClass);
    void ShowPage(int32 PageIndex);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> Attributes;

    UPROPERTY(Transient)
    TObjectPtr<UUserWidget> Skills;

    UPROPERTY(Transient)
    TObjectPtr<UEquipmentPanel> Equipment;

    TSharedPtr<SWidgetSwitcher> Pages;
    int32 ActivePage = 0;
};
