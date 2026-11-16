// 调用链：菜单构建、显隐与销毁 → 玩家控制器注册或移除该 Widget。
#include "UI/Widget/GameplayMenuWidget.h"

#include "Player/MyPlayerController.h"

void UGameplayMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    UpdateInputRegistration();
}

void UGameplayMenuWidget::SetVisibility(ESlateVisibility InVisibility)
{
    Super::SetVisibility(InVisibility);
    UpdateInputRegistration();
}

void UGameplayMenuWidget::NativeDestruct()
{
    if (auto* Controller = GetOwningPlayer<AMyPlayerController>())
    {
        Controller->UnregisterGameplayMenu(this);
    }
    Super::NativeDestruct();
}

void UGameplayMenuWidget::SetEmbeddedPage()
{
    bEmbeddedPage = true;
    UpdateInputRegistration();
}

// 蓝图可缓存并重新显示同一实例，因此每次显隐都同步当前的输入状态。
void UGameplayMenuWidget::UpdateInputRegistration()
{
    if (auto* Controller = GetOwningPlayer<AMyPlayerController>())
    {
        if (!bEmbeddedPage && IsVisible() && (IsInViewport() || GetCachedWidget().IsValid()))
        {
            Controller->RegisterGameplayMenu(this);
        }
        else
        {
            Controller->UnregisterGameplayMenu(this);
        }
    }
}
