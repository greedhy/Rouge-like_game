// 调用链：背包、装备及拾取按定义标识或拾取类查询目录 → 装备表优先 → 内置列表回退。
#include "Backpack/ItemDefinition.h"

// 优先查装备数据表，再查目录内置列表，按定义标识返回匹配条目。
const FBackpackItemDefEntry* UBackpackItemDefinitions::FindDefinition(const FPrimaryAssetId& ItemDefinitionId) const
{
    if (!ItemDefinitionId.IsValid())
    {
        return nullptr;
    }

    if (EquipmentTable && EquipmentTable->GetRowStruct() == FBackpackItemDefEntry::StaticStruct())
    {
        for (const auto& Pair : EquipmentTable->GetRowMap())
        {
            const auto* Row = reinterpret_cast<const FBackpackItemDefEntry*>(Pair.Value);
            if (Row->ItemDefinitionId == ItemDefinitionId)
                return Row;
        }
    }
    for (const FBackpackItemDefEntry& Entry : Items)
    {
        if (Entry.ItemDefinitionId == ItemDefinitionId)
        {
            return &Entry;
        }
    }

    return nullptr;
}

// 优先查装备表，再按拾取类继承关系查找匹配定义。
const FBackpackItemDefEntry* UBackpackItemDefinitions::FindDefinitionForPickupActor(
    const UClass* PickupActorClass) const
{
    if (EquipmentTable && EquipmentTable->GetRowStruct() == FBackpackItemDefEntry::StaticStruct())
    {
        for (const auto& Pair : EquipmentTable->GetRowMap())
        {
            const auto* Row = reinterpret_cast<const FBackpackItemDefEntry*>(Pair.Value);
            if (Row->PickupActorClass && PickupActorClass && PickupActorClass->IsChildOf(Row->PickupActorClass))
                return Row;
        }
    }
    for (const FBackpackItemDefEntry& Item : Items)
    {
        if (Item.PickupActorClass && PickupActorClass && PickupActorClass->IsChildOf(Item.PickupActorClass))
        {
            return &Item;
        }
    }
    return nullptr;
}
