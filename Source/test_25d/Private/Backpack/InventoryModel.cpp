// 调用链：背包组件写入槽位模型 → 库存通知 → 背包视图；复制更新也回写此模型。旧式物品数组入口独立保留。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Backpack/InventoryModel.h"

#include "Backpack/ItemDefinition.h"

// 建立指定数量的槽位，负数量按零处理。
void UInventoryModel::InitializeSlots(int32 SlotCount)
{
    Slots.SetNum(FMath::Max(0, SlotCount));
}

// 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
FInventoryAddResult UInventoryModel::TryAddItem(const UItemDefinition* Definition, int32 Quantity)
{
    FInventoryAddResult Result;
    Result.RemainingQuantity = FMath::Max(Quantity, 0);
    if (!IsValid(Definition) || Result.RemainingQuantity == 0)
    {
        return Result;
    }

    const FPrimaryAssetId ItemDefinitionId = Definition->GetPrimaryAssetId();
    if (const auto* Entry = FindItemDefinition(ItemDefinitionId); Entry && Entry->bRandomizedEquipment)
        return Result;
    const int32 StackLimit = Definition->bStackable ? FMath::Max(1, Definition->MaxStack) : 1;
    ItemDefinitions.FindOrAdd(ItemDefinitionId) = const_cast<UItemDefinition*>(Definition);
    // 先扫描现有同类堆叠，后续扫描空格；剩余数量决定实际接收结果。
    for (FInventorySlot& Slot : Slots)
    {
        if (Slot.ItemDefinitionId != ItemDefinitionId || Slot.Quantity >= StackLimit)
        {
            continue;
        }

        const int32 Accepted = FMath::Min(StackLimit - Slot.Quantity, Result.RemainingQuantity);
        Slot.Quantity += Accepted;
        Result.AcceptedQuantity += Accepted;
        Result.RemainingQuantity -= Accepted;
        if (Result.RemainingQuantity == 0)
        {
            Result.bSucceeded = true;
            OnInventoryUpdated.Broadcast(Slots);
            return Result;
        }
    }

    // 先扫描现有同类堆叠，后续扫描空格；剩余数量决定实际接收结果。
    for (FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty())
        {
            continue;
        }

        const int32 Accepted = FMath::Min(StackLimit, Result.RemainingQuantity);
        Slot.ItemDefinitionId = ItemDefinitionId;
        Slot.Quantity = Accepted;
        Slot.ItemInstanceId = FGuid::NewGuid();
        Result.AcceptedQuantity += Accepted;
        Result.RemainingQuantity -= Accepted;
        if (Result.RemainingQuantity == 0)
        {
            break;
        }
    }

    Result.bSucceeded = Result.AcceptedQuantity > 0;
    if (Result.bSucceeded)
    {
        OnInventoryUpdated.Broadcast(Slots);
    }
    return Result;
}

// 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
FInventoryAddResult UInventoryModel::TryAddItem(const FPrimaryAssetId& ItemDefinitionId, int32 Quantity)
{
    FInventoryAddResult Result;
    Result.RemainingQuantity = FMath::Max(Quantity, 0);
    const FBackpackItemDefEntry* Definition = FindItemDefinition(ItemDefinitionId);
    if (!Definition || Definition->bRandomizedEquipment || Result.RemainingQuantity == 0 ||
        (Definition->bEquippable && (Definition->bStackable || Definition->MaxStack != 1)))
    {
        return Result;
    }

    const int32 StackLimit = Definition->bStackable ? FMath::Max(1, Definition->MaxStack) : 1;
    // 先扫描现有同类堆叠，后续扫描空格；剩余数量决定实际接收结果。
    for (FInventorySlot& Slot : Slots)
    {
        if (Slot.ItemDefinitionId != ItemDefinitionId || Slot.Quantity >= StackLimit)
        {
            continue;
        }

        const int32 Accepted = FMath::Min(StackLimit - Slot.Quantity, Result.RemainingQuantity);
        Slot.Quantity += Accepted;
        Result.AcceptedQuantity += Accepted;
        Result.RemainingQuantity -= Accepted;
        if (Result.RemainingQuantity == 0)
        {
            Result.bSucceeded = true;
            OnInventoryUpdated.Broadcast(Slots);
            return Result;
        }
    }

    // 先扫描现有同类堆叠，后续扫描空格；剩余数量决定实际接收结果。
    for (FInventorySlot& Slot : Slots)
    {
        if (!Slot.IsEmpty())
        {
            continue;
        }

        const int32 Accepted = FMath::Min(StackLimit, Result.RemainingQuantity);
        Slot.ItemDefinitionId = ItemDefinitionId;
        Slot.Quantity = Accepted;
        Slot.ItemInstanceId = FGuid::NewGuid();
        Result.AcceptedQuantity += Accepted;
        Result.RemainingQuantity -= Accepted;
        if (Result.RemainingQuantity == 0)
        {
            break;
        }
    }

    Result.bSucceeded = Result.AcceptedQuantity > 0;
    if (Result.bSucceeded)
    {
        OnInventoryUpdated.Broadcast(Slots);
    }
    return Result;
}

// 设置共享物品目录供后续查找与库存操作使用。
void UInventoryModel::SetItemDefinitions(UBackpackItemDefinitions* InItemDefinitions)
{
    ItemDefinitionCatalog = InItemDefinitions;
}

// 在当前物品目录中查找定义，目录缺失时返回空。
const FBackpackItemDefEntry* UInventoryModel::FindItemDefinition(const FPrimaryAssetId& ItemDefinitionId) const
{
    return IsValid(ItemDefinitionCatalog) ? ItemDefinitionCatalog->FindDefinition(ItemDefinitionId) : nullptr;
}

// 校验槽位及可使用标志后扣除一件，耗尽时清空身份并广播。
bool UInventoryModel::UseItemAt(int32 SlotIndex)
{
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
    {
        return false;
    }

    const FBackpackItemDefEntry* CatalogDefinition = FindItemDefinition(Slots[SlotIndex].ItemDefinitionId);
    UItemDefinition* LegacyDefinition = ItemDefinitions.FindRef(Slots[SlotIndex].ItemDefinitionId);
    if ((CatalogDefinition && (CatalogDefinition->bEquippable || !CatalogDefinition->bUsable)) ||
        (!CatalogDefinition && (!IsValid(LegacyDefinition) || !LegacyDefinition->bUsable)))
    {
        return false;
    }

    FInventorySlot& Slot = Slots[SlotIndex];
    --Slot.Quantity;
    if (Slot.Quantity <= 0)
    {
        Slot.Clear();
    }
    OnInventoryUpdated.Broadcast(Slots);
    return true;
}

// 从旧式物品数组扣减指定数量，不生成掉落物。
void UInventoryModel::DeleteItem(FName ItemID, int32 Count)
{
    int32 Idx = Items.IndexOfByPredicate(
        [&](const FInventoryItem& I)
        {
            return I.ItemID == ItemID;
        });
    if (Idx == INDEX_NONE)
        return;

    Items[Idx].Quantity -= Count;
    if (Items[Idx].Quantity <= 0)
        Items.RemoveAt(Idx);
    OnInventoryUpdated.Broadcast(Slots); // 只广播库存更新，不触发掉落逻辑
}
// 旧式物品数组的单件扣减入口，当前未实现额外使用效果。
void UInventoryModel::UseItem(FName ItemID)
{
    int32 Idx = Items.IndexOfByPredicate(
        [&](const FInventoryItem& I)
        {
            return I.ItemID == ItemID;
        });
    if (Idx == INDEX_NONE)
        return;

    Items[Idx].Quantity -= 1;
    if (Items[Idx].Quantity <= 0)
        Items.RemoveAt(Idx);
    //use logic

    //
    OnInventoryUpdated.Broadcast(Slots); // 只广播库存更新，不触发掉落逻辑
}

// 旧式物品数组的堆叠或新增入口，受堆叠上限和容量限制。
void UInventoryModel::AddItem(const FInventoryItem& NewItem)
{
    for (auto& It : Items)
    {
        if (It.ItemID == NewItem.ItemID && It.Quantity < It.MaxStack)
        {
            It.Quantity = FMath::Min(It.Quantity + NewItem.Quantity, It.MaxStack);
            OnInventoryUpdated.Broadcast(Slots);
            return;
        }
    }
    if (Items.Num() < MaxSlots)
    {
        Items.Add(NewItem);
        OnInventoryUpdated.Broadcast(Slots);
    }
}

// 按旧式物品标识扣减数量，耗尽时删除数组记录。
void UInventoryModel::RemoveItem(FName ItemID, int32 Count)
{
    int32 Idx = Items.IndexOfByPredicate(
        [&](const FInventoryItem& I)
        {
            return I.ItemID == ItemID;
        });
    if (Idx == INDEX_NONE)
        return;
    Items[Idx].Quantity -= Count;
    if (Items[Idx].Quantity <= 0)
        Items.RemoveAt(Idx);
    OnInventoryUpdated.Broadcast(Slots);
}

// 交换两个有效槽位，保留槽内物品身份和词条。
bool UInventoryModel::SwapSlots(int32 IndexA, int32 IndexB)
{
    if (!Slots.IsValidIndex(IndexA) || !Slots.IsValidIndex(IndexB) || IndexA == IndexB)
    {
        return false;
    }

    Swap(Slots[IndexA], Slots[IndexB]);
    OnInventoryUpdated.Broadcast(Slots);
    return true;
}

// 将复制槽位完整映射到本地模型，按通知参数决定是否广播。
void UInventoryModel::ApplyReplicatedSlots(const TArray<FReplicatedInventorySlot>& ReplicatedSlots, int32 NewMaxSlots,
                                           bool bNotify)
{
    const int32 ClampedMaxSlots = FMath::Max(0, NewMaxSlots);
    const bool bMaxSlotsChanged = MaxSlots != ClampedMaxSlots;
    MaxSlots = ClampedMaxSlots;
    Slots.SetNum(MaxSlots);

    for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
    {
        FInventorySlot& LocalSlot = Slots[SlotIndex];
        if (ReplicatedSlots.IsValidIndex(SlotIndex) && !ReplicatedSlots[SlotIndex].IsEmpty())
        {
            LocalSlot.ItemDefinitionId = ReplicatedSlots[SlotIndex].ItemDefinitionId;
            LocalSlot.Quantity = ReplicatedSlots[SlotIndex].Quantity;
            LocalSlot.ItemInstanceId = ReplicatedSlots[SlotIndex].ItemInstanceId;
            LocalSlot.RollData = ReplicatedSlots[SlotIndex].RollData;
        }
        else
        {
            LocalSlot.Clear();
        }
    }

    if (!bNotify)
        return;
    if (bMaxSlotsChanged)
    {
        OnInventoryMaxSlotsChanged.Broadcast(MaxSlots);
    }
    OnInventoryUpdated.Broadcast(Slots);
}

// 调整模型容量，收集被裁掉的非空槽并通知监听者。
void UInventoryModel::SetMaxSlots(int32 NewMaxSlots, bool bNotify)
{
    const int32 ClampedMaxSlots = FMath::Max(0, NewMaxSlots);
    if (ClampedMaxSlots == MaxSlots && Slots.Num() == ClampedMaxSlots)
    {
        return;
    }

    TArray<FInventorySlot> DiscardedSlots;
    if (Slots.Num() > ClampedMaxSlots)
    {
        for (int32 Index = ClampedMaxSlots; Index < Slots.Num(); ++Index)
        {
            if (!Slots[Index].IsEmpty())
            {
                DiscardedSlots.Add(Slots[Index]);
            }
        }
    }

    MaxSlots = ClampedMaxSlots;
    Slots.SetNum(MaxSlots);
    if (!DiscardedSlots.IsEmpty())
    {
        OnSlotsDiscarded.Broadcast(DiscardedSlots);
    }
    OnInventoryMaxSlotsChanged.Broadcast(MaxSlots);
    OnInventoryUpdated.Broadcast(Slots);
}
