// 调用链：HUD 注入控制器参数 → 子类绑定依赖并广播初值 → ASC 技能信息转为 UI 委托；切换依赖和销毁前解除旧订阅。
// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/WidgetController/MyWidgetController.h"
#include "Player/MyPlayerController.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
// 先解除旧依赖回调、清空类型缓存，再替换本轮 UI 使用的玩家与 GAS 对象。
void UMyWidgetController::SetWidgetControllerParams(const FWidgetControllerParams& WCParams)
{
    UnbindCallbacksFromDependencies();
    MyPlayerController = nullptr;
    MyPlayerState = nullptr;
    MyASC = nullptr;
    MyAttributeSet = nullptr;
    PlayerController = WCParams.PlayerController;
    PlayerState = WCParams.PlayerState;
    ASC = WCParams.AbilitySystemComponent;
    AttributeSet = WCParams.AttributeSet;
}

// 初始值广播扩展点，基类不提供具体面板数据。
void UMyWidgetController::BroadcastInitialValues()
{
}

// 依赖订阅扩展点，具体属性与事件绑定由子类实现。
void UMyWidgetController::BindCallbacksToDependencies()
{
}
// 仅在初始技能授予完成后遍历技能，将定义信息与实时输入/状态 Tag 合并广播。
void UMyWidgetController::BroadcastAbilityInfo()
{
    if (!GetMyASC()->bStartupAbilityGiven)
        return;

    FForEachAbility BroadcastDelegate;
    BroadcastDelegate.BindLambda(
        [this](const FGameplayAbilitySpec& AbilitySpec)
        {
            FMyAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(MyASC->GetAbilityTagFromSpec(AbilitySpec));
            Info.InputTag = MyASC->GetInputTagFromSpec(AbilitySpec);
            Info.StatusTag = MyASC->GetStatusFromSpec(AbilitySpec);
            AbilityInfoDelegate.Broadcast(Info);
        });
    GetMyASC()->ForEachAbility(BroadcastDelegate);
}

// 按需转换并缓存项目 PlayerController，供子类访问项目接口。
AMyPlayerController* UMyWidgetController::GetMyPC()
{
    if (MyPlayerController == nullptr)
    {
        MyPlayerController = Cast<AMyPlayerController>(PlayerController);
    }
    return MyPlayerController;
}

// 按需转换并缓存项目 PlayerState，供子类访问玩家数据。
AMyPlayerState* UMyWidgetController::GetMyPS()
{
    if (MyPlayerState == nullptr)
    {
        MyPlayerState = Cast<AMyPlayerState>(PlayerState);
    }
    return MyPlayerState;
}

// 按需转换并缓存项目 ASC，供技能和属性订阅复用。
UMyAbilitySystemComponent* UMyWidgetController::GetMyASC()
{
    if (MyASC == nullptr)
    {
        MyASC = Cast<UMyAbilitySystemComponent>(ASC);
    }
    return MyASC;
}

// 按需转换并缓存项目属性集，供面板读取属性。
UMyAttributeSet* UMyWidgetController::GetMyAS()
{
    if (MyAttributeSet == nullptr)
    {
        MyAttributeSet = Cast<UMyAttributeSet>(AttributeSet);
    }
    return MyAttributeSet;
}

// 销毁前解除依赖回调，避免委托继续访问失效的控制器。
void UMyWidgetController::BeginDestroy()
{
    UnbindCallbacksFromDependencies();
    Super::BeginDestroy();
}
