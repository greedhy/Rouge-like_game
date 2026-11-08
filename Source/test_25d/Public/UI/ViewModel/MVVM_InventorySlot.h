// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "MVVMViewModelBase.h"
#include "Styling/SlateBrush.h"
#include "MVVM_InventorySlot.generated.h"

/**
 * 
 */
UCLASS()
class TEST_25D_API UMVVM_InventorySlot : public UMVVMViewModelBase
{
    GENERATED_BODY()
    friend class UBackPackComponent;
    friend class UInventoryViewModel; // 允许背包VM修改状态

public:
    // === Data Getters ===
    UFUNCTION(BlueprintPure)
    bool IsEmpty() const
    {
        return bEmpty;
    }

    UFUNCTION(BlueprintPure)
    int32 GetStackCount() const
    {
        return StackCount;
    }

    UFUNCTION(BlueprintPure, FieldNotify)
    TSoftObjectPtr<UTexture2D> GetIcon() const
    {
        return Icon;
    }

    UFUNCTION(BlueprintPure, FieldNotify)
    FSlateBrush GetIconBrush() const;

    UFUNCTION(BlueprintPure, FieldNotify)
    ESlateVisibility GetIconVisibility() const
    {
        return bEmpty ? ESlateVisibility::Hidden : ESlateVisibility::Visible;
    }

    UFUNCTION(BlueprintPure, FieldNotify)
    ESlateVisibility GetStackCountVisibility() const
    {
        return HasStack() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
    }

    UFUNCTION(BlueprintPure, FieldNotify)
    FLinearColor GetSlotBackgroundTint() const;

    // 新增：缓存加载后的图标，解决软引用绑定延迟问题
    UFUNCTION(BlueprintPure, FieldNotify)
    UTexture2D* GetLoadedIcon() const
    {
        return LoadedIcon;
    }

    // === Computed Properties（FieldNotify，UI 可绑定）===
    UFUNCTION(BlueprintPure, FieldNotify)
    FText GetStackCountText() const;

    UFUNCTION(BlueprintPure, FieldNotify)
    bool HasStack() const
    {
        return !bEmpty && StackCount > 1;
    }

    // 选中状态（UI可绑定）
    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "GetIsSelected", meta = (AllowPrivateAccess = true))
    bool bIsSelected = false;

    // 是否可使用（计算属性，UI绑定按钮可交互性）
    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "GetCanBeUsed", meta = (AllowPrivateAccess = true))
    bool bCanBeUsed = false;

    bool GetCanBeUsed() const
    {
        return bCanBeUsed;
    }
    bool GetIsSelected() const
    {
        return bIsSelected;
    }
    // 仅允许总VM修改选中状态
    void SetSelected(bool bNewValue);
    void SetCanBeUsed(bool bNewValue);
    // === Request 字段（V -> VM 的一次性信号）===
    UFUNCTION(BlueprintPure)
    bool HasDropRequest() const
    {
        return bDropRequest;
    }

    UFUNCTION(BlueprintCallable, Category = "Inventory|Request")
    void SetDropRequest(bool bNewValue);

    // === 拖拽状态（UI可绑定）===
    UFUNCTION(BlueprintPure, FieldNotify)
    bool IsDragSource() const
    {
        return bIsDragSource;
    }

    UFUNCTION(BlueprintPure, FieldNotify)
    bool IsDragHovered() const
    {
        return bIsDragHovered;
    }

    // 槽位索引（由背包VM赋值，UI拖拽时传递参数）
    UFUNCTION(BlueprintPure)
    int32 GetSlotIndex() const
    {
        return SlotIndex;
    }

    class UMVVM_Backpack* GetBackpackViewModel() const;

    // === Data Setters（仅 Model / Component 可调用）===
    void SetEmpty(bool bNewValue);
    void SetStackCount(int32 NewValue);
    void SetIcon(TSoftObjectPtr<UTexture2D> NewValue);

    // === 数据Setter（仅友元类可调用）===

    void SetLoadedIcon(UTexture2D* NewValue);
    void SetSlotIndex(int32 NewIndex)
    {
        SlotIndex = NewIndex;
    }

    // === 拖拽状态Setter ===
    void SetDragSource(bool bNewValue);
    void SetDragHovered(bool bNewValue);

    UFUNCTION(BlueprintPure)
    bool GetbIsDragSource() const
    {
        return bIsDragSource;
    }

    UFUNCTION(BlueprintPure)
    bool GetbIsDragHovered() const
    {
        return bIsDragHovered;
    }

private:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "IsEmpty", meta = (AllowPrivateAccess = true))
    bool bEmpty = true;

    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter, meta = (AllowPrivateAccess = true))
    int32 StackCount = 0;

    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter, meta = (AllowPrivateAccess = true))
    TSoftObjectPtr<UTexture2D> Icon;

    UPROPERTY(BlueprintReadWrite, FieldNotify, Getter = "HasDropRequest", Setter = "SetDropRequest",
              meta = (AllowPrivateAccess = true))
    bool bDropRequest = false;

    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "GetLoadedIcon", meta = (AllowPrivateAccess = true))
    UTexture2D* LoadedIcon = nullptr;

    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "GetbIsDragSource", meta = (AllowPrivateAccess = true))
    bool bIsDragSource = false;

    UPROPERTY(BlueprintReadOnly, FieldNotify, Getter = "GetbIsDragHovered", meta = (AllowPrivateAccess = true))
    bool bIsDragHovered = false;

    UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    int32 SlotIndex = INDEX_NONE;
};
