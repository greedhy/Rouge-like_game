/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 数据链路：服务器BackpackComponent修改槽位并标脏 -> NetDeltaSerialize/FastArrayDeltaSerialize -> 组件复制回调 -> InventoryModel应用槽位。
 * 本结构不是UObject/单例：作为组件的值成员存活；ItemInstanceId与RollData随槽位复制。修改Items不会自动替业务代码调用MarkItemDirty；删除/缩容需标记数组变化。
 * 复制数据与客户端MVVM对象分离，网络上传输槽位业务状态，不传输UInventoryModel或Widget指针。
 */
#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ReplicatedInventory.generated.h"

USTRUCT(BlueprintType)
struct FReplicatedInventorySlot : public FFastArraySerializerItem
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

    bool IsEmpty() const
    {
        return !ItemDefinitionId.IsValid() || Quantity <= 0;
    }

    void Clear()
    {
        RollData = FEquipmentRollData();
        ItemInstanceId.Invalidate();
        ItemDefinitionId = FPrimaryAssetId();
        Quantity = 0;
    }
};

USTRUCT()
struct FReplicatedInventoryList : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FReplicatedInventorySlot> Items;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
    {
        return FastArrayDeltaSerialize<FReplicatedInventorySlot, FReplicatedInventoryList>(Items, DeltaParams, *this);
    }
};

template <>
struct TStructOpsTypeTraits<FReplicatedInventoryList> : public TStructOpsTypeTraitsBase2<FReplicatedInventoryList>
{
    enum
    {
        WithNetDeltaSerializer = true,
    };
};
