// 调用链：编辑器 PIE 验证脚本 → Slate 键盘事件或视口鼠标事件 → 实际本地控制器。
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "GameplayTagContainer.h"
#include "GameplayMenuInputProbe.generated.h"

class APlayerController;
class UUserWidget;
class USpellMenuWidgetController;

UCLASS()
class UGameplayMenuInputProbe : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static bool SendInput(APlayerController* Controller, FKey Key, bool bPressed);

    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static FString DescribeMoveInput(APlayerController* Controller, class UInputAction* Action, FKey Key);

    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static bool ClickSlateButton(UUserWidget* Root, const FString& Label);

    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static TArray<FString> ReadSlateText(UUserWidget* Root, bool bReadToolTips);
    /** Dispatch the normal slot selection on the PIE world tick so client RPCs reach authority. */
    UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
    static bool QueueSpellSlot(USpellMenuWidgetController* Controller, FGameplayTag Slot, FGameplayTag AbilityType);
};
