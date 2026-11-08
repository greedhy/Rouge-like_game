// 调用链：HUD 注入 WidgetController → SetWidgetController 保存引用 → WidgetControllerSet 通知蓝图绑定显示。
// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Widget/MyUserWidget.h"

// 替换界面控制器并立即触发蓝图接线事件。
void UMyUserWidget::SetWidgetController(UObject* InWidgetController)
{
    WidgetController = InWidgetController;
    WidgetControllerSet();
}
