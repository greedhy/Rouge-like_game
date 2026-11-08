// 调用链：背包组件写入槽位模型 → 库存通知 → 背包视图；复制更新也回写此模型。旧式物品数组入口独立保留。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
#include "UObject/Object.h"
#include "Backpack/ReplicatedInventory.h"
#include "InventoryModel.generated.h"

class UItemDefinition;
class UBackpackItemDefinitions;
struct FBackpackItemDefEntry;

/**
 * 
 */

USTRUCT(BlueprintType)
struct FInventoryItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ItemID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UTexture2D> Icon;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Quantity = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxStack = 99;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsUsable = false;
};

USTRUCT(BlueprintType)
struct FInventorySlot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FPrimaryAssetId ItemDefinitionId;

    UPROPERTY(SaveGame, BlueprintReadOnly)
    FGuid ItemInstanceId;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FEquipmentRollData RollData;

    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 0;

    // 以定义无效或数量非正判定空槽。
    bool IsEmpty() const
    {
        return !ItemDefinitionId.IsValid() || Quantity <= 0;
    }

    // 清空定义、实例身份、随机词条及数量。
    void Clear()
    {
        RollData = FEquipmentRollData();
        ItemInstanceId.Invalidate();
        ItemDefinitionId = FPrimaryAssetId();
        Quantity = 0;
    }
};

USTRUCT(BlueprintType)
struct FInventoryAddResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSucceeded = false;

    UPROPERTY(BlueprintReadOnly)
    int32 AcceptedQuantity = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 RemainingQuantity = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnInventoryUpdated, const TArray<FInventorySlot>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnInventoryMaxSlotsChanged, int32 /*NewMaxSlots*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSlotsDiscarded, const TArray<FInventorySlot>&);

UCLASS()
class TEST_25D_API UInventoryModel : public UObject
{
    GENERATED_BODY()
public:
    // 建立指定数量的槽位，负数量按零处理。
    void InitializeSlots(int32 SlotCount);

    // 返回当前槽位数组的只读引用。
    const TArray<FInventorySlot>& GetSlots() const
    {
        return Slots;
    }

    // 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
    FInventoryAddResult TryAddItem(const UItemDefinition* Definition, int32 Quantity);
    // 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
    FInventoryAddResult TryAddItem(const FPrimaryAssetId& ItemDefinitionId, int32 Quantity);
    // 设置共享物品目录供后续查找与库存操作使用。
    void SetItemDefinitions(UBackpackItemDefinitions* InItemDefinitions);
    // 在当前物品目录中查找定义，目录缺失时返回空。
    const FBackpackItemDefEntry* FindItemDefinition(const FPrimaryAssetId& ItemDefinitionId) const;
    // 校验槽位及可使用标志后扣除一件，耗尽时清空身份并广播。
    bool UseItemAt(int32 SlotIndex);
    // 交换两个有效槽位，保留槽内物品身份和词条。
    bool SwapSlots(int32 IndexA, int32 IndexB);
    // 将复制槽位完整映射到本地模型，按通知参数决定是否广播。
    void ApplyReplicatedSlots(const TArray<FReplicatedInventorySlot>& ReplicatedSlots, int32 NewMaxSlots,
                              bool bNotify = true);

    // InventoryModel.h 新增
    // 从旧式物品数组扣减指定数量，不生成掉落物。
    void DeleteItem(FName ItemID, int32 Count);
    // 旧式物品数组的单件扣减入口，当前未实现额外使用效果。
    void UseItem(FName ItemID); // 直接删除，不触发掉落
    // 旧式物品数组的堆叠或新增入口，受堆叠上限和容量限制。
    void AddItem(const FInventoryItem& NewItem);

    // 按旧式物品标识扣减数量，耗尽时删除数组记录。
    void RemoveItem(FName ItemID, int32 Count);

    // 返回旧式物品数组的只读引用。
    const TArray<FInventoryItem>& GetItems() const
    {
        return Items;
    }

    FOnInventoryUpdated OnInventoryUpdated;

    // 动态格数接口
    // 返回模型容量上限。
    int32 GetMaxSlots() const
    {
        return MaxSlots;
    }

    FOnInventoryMaxSlotsChanged OnInventoryMaxSlotsChanged; // 格数变化广播
    FOnSlotsDiscarded OnSlotsDiscarded;

private:
    friend class UBackpackComponent;
    friend class UEquipmentComponent;

    // 调整模型容量，收集被裁掉的非空槽并通知监听者。
    void SetMaxSlots(int32 NewMaxSlots, bool bNotify = true);

    UPROPERTY()
    TArray<FInventorySlot> Slots;

    UPROPERTY()
    TArray<FInventoryItem> Items;

    UPROPERTY()
    TMap<FPrimaryAssetId, TObjectPtr<UItemDefinition>> ItemDefinitions;

    UPROPERTY()
    TObjectPtr<UBackpackItemDefinitions> ItemDefinitionCatalog;

    int32 MaxSlots = 16;
};
