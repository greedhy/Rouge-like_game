// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Widget/BackpackSlot.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "UI/ViewModel/MVVM_InventorySlot.h"

void UBackpackSlot::NativeOnListItemObjectSet(UObject* ListItemObject)
{
    IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);
    SlotViewModel = Cast<UMVVM_InventorySlot>(ListItemObject);
}

FReply UBackpackSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    FReply Reply = Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    if (SlotViewModel && !SlotViewModel->IsEmpty() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        return Reply.DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
    }

    return Reply;
}

void UBackpackSlot::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
                                         UDragDropOperation*& Operation)
{
    if (!SlotViewModel)
    {
        return;
    }

    UMVVM_Backpack* BackpackViewModel = SlotViewModel->GetBackpackViewModel();
    if (!BackpackViewModel || !BackpackViewModel->RequestBeginDrag(SlotViewModel->GetSlotIndex()))
    {
        return;
    }

    UDragDropOperation* DragOperation = NewObject<UDragDropOperation>(this);
    DragOperation->Payload = SlotViewModel;
    DragOperation->Pivot = EDragPivot::MouseDown;

    if (UTexture2D* Icon = SlotViewModel->GetLoadedIcon())
    {
        UImage* DragVisual = NewObject<UImage>(DragOperation);
        DragVisual->SetBrushFromTexture(Icon, true);
        DragOperation->DefaultDragVisual = DragVisual;
    }

    Operation = DragOperation;
}

void UBackpackSlot::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                      UDragDropOperation* InOperation)
{
    if (SlotViewModel && Cast<UMVVM_InventorySlot>(InOperation ? InOperation->Payload : nullptr))
    {
        if (UMVVM_Backpack* BackpackViewModel = SlotViewModel->GetBackpackViewModel())
        {
            BackpackViewModel->RequestHoverSlot(SlotViewModel->GetSlotIndex());
        }
    }

    Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
}

void UBackpackSlot::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    if (SlotViewModel)
    {
        if (UMVVM_Backpack* BackpackViewModel = SlotViewModel->GetBackpackViewModel())
        {
            BackpackViewModel->RequestHoverSlot(INDEX_NONE);
        }
    }

    Super::NativeOnDragLeave(InDragDropEvent, InOperation);
}

bool UBackpackSlot::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
                                 UDragDropOperation* InOperation)
{
    UMVVM_InventorySlot* SourceSlot = Cast<UMVVM_InventorySlot>(InOperation ? InOperation->Payload : nullptr);
    if (!SourceSlot || !SlotViewModel)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Entry drop rejected: source or target slot VM is missing."));
        return false;
    }

    UMVVM_Backpack* SourceBackpack = SourceSlot->GetBackpackViewModel();
    if (!SourceBackpack || SourceBackpack != SlotViewModel->GetBackpackViewModel())
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[BackpackDrag] Entry drop rejected: source and target do not share a backpack VM."));
        return false;
    }

    const int32 SourceIndex = SourceSlot->GetSlotIndex();
    const int32 TargetIndex = SlotViewModel->GetSlotIndex();
    const bool bHandled = SourceBackpack->RequestDropOnSlot(TargetIndex);
    UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Entry received drop: %d -> %d; handled=%s."), SourceIndex, TargetIndex,
           bHandled ? TEXT("true") : TEXT("false"));
    return bHandled;
}

void UBackpackSlot::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Source entry received drag-cancelled."));
    if (UMVVM_InventorySlot* SourceSlot = Cast<UMVVM_InventorySlot>(InOperation ? InOperation->Payload : nullptr))
    {
        if (UMVVM_Backpack* BackpackViewModel = SourceSlot->GetBackpackViewModel())
        {
            // TileView may report an unhandled entry drop as DragCancelled on the source.
            // Reuse the recorded hover target so that only a drop over another backpack slot swaps.
            if (!BackpackViewModel->RequestDropOnHoveredSlot())
            {
                BackpackViewModel->RequestCancelDrag();
            }
        }
    }

    Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
}
