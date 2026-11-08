// 调用链：InitOverlay 创建界面 → 获取并初始化控制器 → 连接 Widget → 广播初值 → 加入视口；各菜单控制器按需创建并缓存。
// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HUD/MyHUD.h"

#include "GameFramework/HUD.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"

// 首次请求时创建主界面控制器并绑定依赖，后续返回缓存对象。
UOverlayWidgetController* AMyHUD::GetOverlayWidgetController(const FWidgetControllerParams& WCParams)
{
    if (OverlayWidgetController == nullptr)
    {
        OverlayWidgetController = NewObject<UOverlayWidgetController>(this, OverlayWidgetControllerClass);
        OverlayWidgetController->SetWidgetControllerParams(WCParams);
        OverlayWidgetController->BindCallbacksToDependencies();
    }

    return OverlayWidgetController;
}

// 首次请求时创建属性菜单控制器并注入玩家/GAS 依赖。
UAttributeMenuWidgetController* AMyHUD::GetAttributeMenuWidgetController(const FWidgetControllerParams& WCParams)
{
    if (AttributeMenuWidgetController == nullptr)
    {
        AttributeMenuWidgetController =
            NewObject<UAttributeMenuWidgetController>(this, AttributeMenuWidgetControllerClass);
        AttributeMenuWidgetController->SetWidgetControllerParams(WCParams);
        AttributeMenuWidgetController->BindCallbacksToDependencies();
    }

    return AttributeMenuWidgetController;
}

// 首次请求时创建技能菜单控制器并绑定技能状态回调。
USpellMenuWidgetController* AMyHUD::GetSpellMenuWidgetController(const FWidgetControllerParams& WCParams)
{
    if (SpellMenuWidgetController == nullptr)
    {
        SpellMenuWidgetController = NewObject<USpellMenuWidgetController>(this, SpellMenuWidgetControllerClass);
        SpellMenuWidgetController->SetWidgetControllerParams(WCParams);
        SpellMenuWidgetController->BindCallbacksToDependencies();
    }

    return SpellMenuWidgetController;
}

// 验证蓝图配置后创建 Overlay，注入控制器并发布首帧数据，然后显示到视口。
void AMyHUD::InitOverlay(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS)
{
    checkf(OverlayWidgetClass, TEXT("OverlayWidgetClass没有在蓝图中初始化"))
        checkf(OverlayWidgetControllerClass, TEXT("OverlayWidgetControllerClass没有在蓝图中初始化"))

        //创建Widget，其类为蓝图上放置的Overlay类
        UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), OverlayWidgetClass);

    OverlayWidget = Cast<UMyUserWidget>(Widget);
    //初始化WidgetController
    const FWidgetControllerParams WidgetControllerParams(PC, PS, ASC, AS);
    UOverlayWidgetController* WidgetController = GetOverlayWidgetController(WidgetControllerParams);

    OverlayWidget->SetWidgetController(WidgetController);

    WidgetController->BroadcastInitialValues();

    //添加进视口
    Widget->AddToViewport();
}
