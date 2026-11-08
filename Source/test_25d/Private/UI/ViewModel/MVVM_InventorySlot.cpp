// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ViewModel/MVVM_InventorySlot.h"
#include "UI/ViewModel/MVVM_Backpack.h"

UMVVM_Backpack* UMVVM_InventorySlot::GetBackpackViewModel() const
{
    return GetTypedOuter<UMVVM_Backpack>();
}

FSlateBrush UMVVM_InventorySlot::GetIconBrush() const
{
    FSlateBrush Brush;
    Brush.SetResourceObject(LoadedIcon ? LoadedIcon : Icon.Get());
    Brush.ImageSize = FVector2D(48.f, 48.f);
    return Brush;
}

FLinearColor UMVVM_InventorySlot::GetSlotBackgroundTint() const
{
    if (bIsSelected)
        return FLinearColor(1.f, 0.82f, 0.25f, 1.f);
    if (bIsDragHovered)
        return FLinearColor(0.45f, 0.75f, 1.f, 1.f);
    if (bIsDragSource)
        return FLinearColor(0.65f, 0.65f, 0.65f, 1.f);
    return FLinearColor::Transparent;
}

FText UMVVM_InventorySlot::GetStackCountText() const
{
    if (bEmpty || StackCount <= 1)
        return FText::GetEmpty();
    return FText::AsNumber(StackCount);
}

void UMVVM_InventorySlot::SetEmpty(bool bNewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(bEmpty, bNewValue))
    {
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetIconVisibility);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetStackCountVisibility);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetStackCountText);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasStack);
    }
}

void UMVVM_InventorySlot::SetStackCount(int32 NewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(StackCount, NewValue))
    {
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetStackCountText);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasStack);
    }
}

void UMVVM_InventorySlot::SetIcon(TSoftObjectPtr<UTexture2D> NewValue)
{

    UE_MVVM_SET_PROPERTY_VALUE(Icon, NewValue);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetIcon);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetIconBrush);
}

void UMVVM_InventorySlot::SetDropRequest(bool bNewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(bDropRequest, bNewValue))
    {
        bDropRequest = false; // 一次性信号，立即重置，避免死循环
    }
}

void UMVVM_InventorySlot::SetLoadedIcon(UTexture2D* NewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(LoadedIcon, NewValue))
    {
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetLoadedIcon);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetIconBrush);
    }
}

void UMVVM_InventorySlot::SetDragSource(bool bNewValue)
{
    UE_MVVM_SET_PROPERTY_VALUE(bIsDragSource, bNewValue);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSlotBackgroundTint);
}

void UMVVM_InventorySlot::SetDragHovered(bool bNewValue)
{
    UE_MVVM_SET_PROPERTY_VALUE(bIsDragHovered, bNewValue);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSlotBackgroundTint);
}

// InventorySlotViewModel.cpp 新增
void UMVVM_InventorySlot::SetSelected(bool bNewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(bIsSelected, bNewValue))
    {
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bIsSelected);
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSlotBackgroundTint);
    }
}

void UMVVM_InventorySlot::SetCanBeUsed(bool bNewValue)
{
    if (UE_MVVM_SET_PROPERTY_VALUE(bCanBeUsed, bNewValue))
    {
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bCanBeUsed);
    }
}
