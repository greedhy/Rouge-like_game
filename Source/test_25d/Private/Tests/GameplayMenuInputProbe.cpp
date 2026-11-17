// 调用链：PIE 验证脚本 → 本地 Slate/视口输入 → 检查 PlayerInput 实际按键状态。
#include "Tests/GameplayMenuInputProbe.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "MyGameplayTags.h"
#include "Blueprint/UserWidget.h"
#include "Layout/Children.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"
#include "Engine/World.h"
#include "TimerManager.h"

/** 挂到 PIE 世界下一次 Tick，避开编辑器脚本上下文下 RPC 被本地处理的问题。 */
bool UGameplayMenuInputProbe::QueueSpellSlot(USpellMenuWidgetController* Controller, FGameplayTag Slot,
                                           FGameplayTag AbilityType)
{
#if WITH_EDITOR
    UWorld* World = Controller ? Controller->GetWorld() : nullptr;
    if (World && World->WorldType == EWorldType::PIE && Slot.IsValid() && AbilityType.IsValid())
    {
        World->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateWeakLambda(Controller, [Controller, Slot, AbilityType]()
            {
                Controller->SpellRowGlobePressed(Slot, AbilityType);
            }));
        return true;
    }
#endif
    return false;
}

#if WITH_EDITOR
namespace
{
    // 遍历活动 Slate 分支，页签切换后的隐藏页不计入读回。
    void VisitSlate(const TSharedRef<SWidget>& Widget, TFunctionRef<void(SWidget&)> Visit)
    {
        Visit(Widget.Get());
        FChildren* Children = Widget->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            VisitSlate(Children->GetChildAt(Index), Visit);
        }
    }
}
#endif

bool UGameplayMenuInputProbe::ClickSlateButton(UUserWidget* Root, const FString& Label)
{
#if WITH_EDITOR
    if (Root && Root->GetWorld()->WorldType == EWorldType::PIE && Root->GetCachedWidget())
    {
        TSharedPtr<SButton> Match;
        VisitSlate(Root->GetCachedWidget().ToSharedRef(), [&Label, &Match](SWidget& Widget)
        {
            if (!Match && Widget.GetTypeAsString() == TEXT("SButton"))
            {
                SButton& Button = static_cast<SButton&>(Widget);
                VisitSlate(Button.AsShared(), [&Label, &Match, &Button](SWidget& Child)
                {
                    if (Child.GetTypeAsString() == TEXT("STextBlock") &&
                        static_cast<STextBlock&>(Child).GetText().ToString() == Label)
                    {
                        Match = StaticCastSharedRef<SButton>(Button.AsShared());
                    }
                });
            }
        });
        if (Match && Match->IsEnabled())
        {
            Match->SimulateClick();
            return true;
        }
    }
#endif
    return false;
}

TArray<FString> UGameplayMenuInputProbe::ReadSlateText(UUserWidget* Root, bool bReadToolTips)
{
    TArray<FString> Text;
#if WITH_EDITOR
    if (Root && Root->GetWorld()->WorldType == EWorldType::PIE && Root->GetCachedWidget())
    {
        const auto ReadText = [&Text](SWidget& Widget)
        {
            if (Widget.GetTypeAsString() == TEXT("STextBlock"))
            {
                Text.Add(static_cast<STextBlock&>(Widget).GetText().ToString());
            }
        };
        VisitSlate(Root->GetCachedWidget().ToSharedRef(), [&](SWidget& Widget)
        {
            if (!bReadToolTips)
            {
                ReadText(Widget);
            }
            else if (const TSharedPtr<IToolTip> Tip = Widget.GetToolTip(); Tip && !Tip->IsEmpty())
            {
                VisitSlate(Tip->GetContentWidget(), ReadText);
            }
        });
    }
#endif
    return Text;
}

// 鼠标模拟从 UI 未处理后进入的视口路径，键盘从 Slate 焦点路径进入。
bool UGameplayMenuInputProbe::SendInput(APlayerController* Controller, FKey Key, bool bPressed)
{
#if WITH_EDITOR
    if (!Controller || Controller->GetWorld()->WorldType != EWorldType::PIE || !FSlateApplication::IsInitialized())
    {
        return false;
    }
    ULocalPlayer* Player = Controller->GetLocalPlayer();
    const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
    UGameViewportClient* Viewport = Player ? Player->ViewportClient.Get() : nullptr;
    if (!User || !Viewport || !Viewport->Viewport)
    {
        return false;
    }
    const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller->GetPlatformUserId());
    if (Key.IsMouseButton())
    {
        Viewport->InputKey(FInputKeyEventArgs(Viewport->Viewport, Device, Key, bPressed ? IE_Pressed : IE_Released,
                                             FPlatformTime::Cycles64()));
    }
    else
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Event(Key, FModifierKeysState(), Device, false, 0, 0, User->GetUserIndex());
        if (bPressed)
        {
            Slate.ProcessKeyDownEvent(Event);
        }
        else
        {
            Slate.ProcessKeyUpEvent(Event);
        }
    }
    return Controller->IsInputKeyDown(Key);
#else
    return false;
#endif
}

FString UGameplayMenuInputProbe::DescribeMoveInput(APlayerController* Controller, UInputAction* Action, FKey Key)
{
#if WITH_EDITOR
    if (Controller && Controller->GetWorld()->WorldType == EWorldType::PIE)
    {
        const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(Controller->PlayerInput);
        const FKeyState* State = Input ? Input->GetKeyState(Key) : nullptr;
        UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Controller->GetPawn());
        const auto Mappings = Input ? Input->GetEnhancedActionMappingsView()
                                    : TConstArrayView<const FEnhancedActionKeyMapping>();
        const auto* Mapping = Mappings.FindByPredicate([Action, Key](const FEnhancedActionKeyMapping& Entry)
        {
            return Entry.Action == Action && Entry.Key == Key;
        });
        return FString::Printf(TEXT("Action=%s Raw=%s Block=%d Paused=%d Mappings=%d KeyMapped=%d Ignored=%d"),
                               Input ? *Input->GetActionValue(Action).ToString() : TEXT("None"),
                               State ? *State->RawValue.ToString() : TEXT("None"),
                               ASC && ASC->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed),
                               Controller->IsPaused(), Mappings.Num(), Mapping != nullptr,
                               Mapping && Mapping->bShouldBeIgnored);
    }
#endif
    return {};
}
