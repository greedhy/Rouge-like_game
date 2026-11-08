// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ViewModel/MVVM_Backpack.h"

#include "MVVMViewModelBase.h"
#include "Backpack/InventoryModel.h"
#include "Equipment/EquipmentGenerator.h"
#include "Player/MyPlayerController.h"
#include "UI/ViewModel/MVVM_InventorySlot.h"

void UMVVM_Backpack::Initialize(UInventoryModel* InModel, AMyPlayerController* InOwningPlayerController)
{
    Model = InModel;
    SetOwningPlayerController(InOwningPlayerController);
    if (Model)
    {
        Model->OnInventoryUpdated.AddUObject(this, &UMVVM_Backpack::HandleInventoryUpdated);
        Model->OnInventoryMaxSlotsChanged.AddUObject(this, &UMVVM_Backpack::OnMaxSlotsChanged);
    }
    RebuildFromModel();
}

void UMVVM_Backpack::SetOwningPlayerController(AMyPlayerController* InOwningPlayerController)
{
    OwningPlayerController = InOwningPlayerController;
}

void UMVVM_Backpack::HandleInventoryUpdated(const TArray<FInventorySlot>& Slots)
{
    RebuildFromModel();
}

void UMVVM_Backpack::RebuildFromModel()
{
    if (!Model)
        return;
    const TArray<FInventorySlot>& Slots = Model->GetSlots();
    MaxSlotsCache = Slots.Num();
    SlotVMs.Reset();
    SlotVMsTyped.SetNum(Slots.Num());
    if (CurrentSelectedIndex >= Slots.Num() || CurrentSelectedIndex < 0 || Slots[CurrentSelectedIndex].IsEmpty())
    {
        ResetSelection();
    }
    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        UMVVM_InventorySlot* VM = SlotVMsTyped[i];
        if (!IsValid(VM))
        {
            VM = NewObject<UMVVM_InventorySlot>(this);
            SlotVMsTyped[i] = VM;
        }
        VM->SetSlotIndex(i);
        if (!Slots[i].IsEmpty())
        {
            const FBackpackItemDefEntry* Definition = Model->FindItemDefinition(Slots[i].ItemDefinitionId);
            VM->SetEmpty(false);
            VM->SetIcon(Definition ? Definition->Icon : nullptr);
            VM->SetLoadedIcon(Definition ? Definition->Icon : nullptr);
            VM->SetStackCount(Slots[i].Quantity);
        }
        else
        {
            VM->SetEmpty(true);
            VM->SetIcon(nullptr);
            VM->SetLoadedIcon(nullptr);
            VM->SetStackCount(0);
        }
        SlotVMs.Add(VM);
    }
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSlotViewModels);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemName);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemDescription);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedStackText);
}

void UMVVM_Backpack::RequestDrop(int32 SlotIndex)
{
    if (IsValid(Model) && Model->GetSlots().IsValidIndex(SlotIndex) && !Model->GetSlots()[SlotIndex].IsEmpty())
    {
        OnDropRequested.Broadcast(SlotIndex);
    }
}

void UMVVM_Backpack::RequestSwap(int32 From, int32 To)
{
    if (IsValid(Model) && Model->GetSlots().IsValidIndex(From) && Model->GetSlots().IsValidIndex(To) && From != To)
    {
        if (AMyPlayerController* Controller = OwningPlayerController.Get();
            IsValid(Controller) && Controller->IsLocalController())
        {
            UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Client requests swap: %d -> %d."), From, To);
            Controller->ServerRequestSwap(From, To);
            return;
        }

        UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Client cannot send swap %d -> %d: no local owning controller."),
               From, To);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Client rejected invalid swap request: %d -> %d."), From, To);
}

// ==================== 拖拽逻辑 ====================
bool UMVVM_Backpack::RequestBeginDrag(int32 SourceSlotIndex)
{
    if (!IsValid(Model) || !Model->GetSlots().IsValidIndex(SourceSlotIndex) ||
        Model->GetSlots()[SourceSlotIndex].IsEmpty())
        return false;

    DragSourceIndex = SourceSlotIndex;
    bIsDragging = true;

    // 标记源槽位为拖拽状态，UI自动更新样式
    if (SlotVMsTyped.IsValidIndex(SourceSlotIndex))
    {
        SlotVMsTyped[SourceSlotIndex]->SetDragSource(true);
    }

    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsDragging);
    return true;
}

bool UMVVM_Backpack::RequestDropOnSlot(int32 TargetSlotIndex)
{
    if (!bIsDragging || !IsValid(Model) || DragSourceIndex == INDEX_NONE)
    {
        ClearDragState();
        return false;
    }

    const bool bSuccess = Model->GetSlots().IsValidIndex(TargetSlotIndex) && TargetSlotIndex != DragSourceIndex;
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Client accepted drop: %d -> %d."), DragSourceIndex, TargetSlotIndex);
        RequestSwap(DragSourceIndex, TargetSlotIndex);
    }

    ClearDragState();
    return bSuccess;
}

bool UMVVM_Backpack::RequestDropOnHoveredSlot()
{
    return RequestDropOnSlot(CurrentHoverIndex);
}

void UMVVM_Backpack::RequestCancelDrag()
{
    ClearDragState();
}

void UMVVM_Backpack::RequestHoverSlot(int32 HoverSlotIndex)
{
    if (!bIsDragging || CurrentHoverIndex == HoverSlotIndex)
        return;

    // 清除上一个悬停槽位的状态
    if (SlotVMsTyped.IsValidIndex(CurrentHoverIndex))
    {
        SlotVMsTyped[CurrentHoverIndex]->SetDragHovered(false);
    }
    // 设置新的悬停槽位状态
    if (SlotVMsTyped.IsValidIndex(HoverSlotIndex))
    {
        SlotVMsTyped[HoverSlotIndex]->SetDragHovered(true);
    }
    CurrentHoverIndex = HoverSlotIndex;
}

void UMVVM_Backpack::ClearDragState()
{
    // 重置所有槽位的拖拽状态
    if (SlotVMsTyped.IsValidIndex(DragSourceIndex))
    {
        SlotVMsTyped[DragSourceIndex]->SetDragSource(false);
    }
    if (SlotVMsTyped.IsValidIndex(CurrentHoverIndex))
    {
        SlotVMsTyped[CurrentHoverIndex]->SetDragHovered(false);
    }

    bIsDragging = false;
    DragSourceIndex = INDEX_NONE;
    CurrentHoverIndex = INDEX_NONE;
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IsDragging);
}
void UMVVM_Backpack::OnMaxSlotsChanged(int32 NewMaxSlots)
{
    if (MaxSlotsCache != NewMaxSlots)
    {
        MaxSlotsCache = NewMaxSlots;
        UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetMaxSlots);
        RebuildFromModel(); // 格数变化，重建SlotVM数组
    }
}

void UMVVM_Backpack::SelectSlot(int32 SlotIndex)
{
    if (!IsValid(Model))
        return;

    // 1. 取消之前选中的槽位
    if (CurrentSelectedIndex != INDEX_NONE && SlotVMsTyped.IsValidIndex(CurrentSelectedIndex))
    {
        SlotVMsTyped[CurrentSelectedIndex]->SetSelected(false);
        SlotVMsTyped[CurrentSelectedIndex]->SetCanBeUsed(false);
    }

    // 2. 选中新槽位（点击空白/无效索引则取消选中）
    if (Model->GetSlots().IsValidIndex(SlotIndex) && !Model->GetSlots()[SlotIndex].IsEmpty())
    {
        CurrentSelectedIndex = SlotIndex;
        SlotVMsTyped[SlotIndex]->SetSelected(true);
        SlotVMsTyped[SlotIndex]->SetCanBeUsed(false);
    }
    else
    {
        CurrentSelectedIndex = INDEX_NONE;
    }

    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCurrentSelectedIndex);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasSelectedItem);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemName);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemDescription);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedStackText);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCanUseSelectedItem);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCanDropSelectedItem);
}

bool UMVVM_Backpack::RequestUseSelectedItem()
{
    if (!HasSelectedItem())
        return false;
    if (AMyPlayerController* Controller = OwningPlayerController.Get();
        IsValid(Controller) && Controller->IsLocalController())
    {
        Controller->ServerRequestUse(CurrentSelectedIndex);
        return true;
    }
    return false;
}

bool UMVVM_Backpack::RequestDeleteSelectedItem(int32 Count)
{
    if (!HasSelectedItem())
        return false;
    OnDeleteRequested.Broadcast(CurrentSelectedIndex, Count);
    ResetSelection(); // 删除后取消选中
    return true;
}

FText UMVVM_Backpack::GetSelectedItemName() const
{
    if (!HasSelectedItem())
        return FText::GetEmpty();
    const auto& Id = Model->GetSlots()[CurrentSelectedIndex].ItemDefinitionId;
    const auto* Def = Model->FindItemDefinition(Id);
    return FEquipmentGenerator::MakeDisplayName(Def ? Def->DisplayName : FText::FromName(Id.PrimaryAssetName),
                                                Model->GetSlots()[CurrentSelectedIndex].RollData);
}

FText UMVVM_Backpack::GetSelectedItemDescription() const
{
    return HasSelectedItem() ? FEquipmentGenerator::MakeDescription(Model->GetSlots()[CurrentSelectedIndex].RollData)
                             : FText::GetEmpty();
}

void UMVVM_Backpack::ResetSelection()
{
    if (CurrentSelectedIndex != INDEX_NONE && SlotVMsTyped.IsValidIndex(CurrentSelectedIndex))
    {
        SlotVMsTyped[CurrentSelectedIndex]->SetSelected(false);
        SlotVMsTyped[CurrentSelectedIndex]->SetCanBeUsed(false);
    }
    CurrentSelectedIndex = INDEX_NONE;
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCurrentSelectedIndex);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HasSelectedItem);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemName);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedItemDescription);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetSelectedStackText);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCanUseSelectedItem);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GetCanDropSelectedItem);
}
