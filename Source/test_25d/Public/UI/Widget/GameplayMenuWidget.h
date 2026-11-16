// 调用链：Overlay 蓝图显示或隐藏菜单 → 原生 Widget 同步本地控制器的菜单输入注册。
#pragma once

#include "UI/Widget/MyUserWidget.h"
#include "GameplayMenuWidget.generated.h"

/** 为游戏内的属性和技能菜单管理输入注册。 */
UCLASS()
class TEST_25D_API UGameplayMenuWidget : public UMyUserWidget
{
    GENERATED_BODY()

public:
    virtual void SetVisibility(ESlateVisibility InVisibility) override;
    // 内嵌页面由人物窗口统一注册输入。
    void SetEmbeddedPage();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void UpdateInputRegistration();
    bool bEmbeddedPage = false;
};
