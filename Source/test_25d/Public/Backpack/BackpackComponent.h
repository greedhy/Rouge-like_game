/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 主链路：拾取/控制器RPC -> 服务器背包操作 -> InventoryModel -> SynchronizeReplicatedSlotsFromModel -> 复制列表 -> 客户端OnRep_Inventory -> 本地模型与MVVM。
 * 生命周期：PlayerState创建本组件；Initialize按需NewObject创建InventoryModel与InventoryVM，以组件为Outer且由成员引用持有。重复Initialize只刷新控制器，不重复建立模型。它们是本地对象，不作为独立网络对象复制。
 * 同步：ReplicatedSlots使用FastArray差量序列化；项目显式MarkItemDirty/MarkArrayDirty通知变化。Capacity和Revision也复制；没有OwnerOnly条件，复制范围由宿主决定。客户端按复制数据重建本地视图，不自行生成装备实例或词条。
 * 装备事务以双Revision和实例ID校验旧UI请求；存档恢复通过背包装备联合快照，避免仅恢复库存而遗失已穿戴实例。
 */
// 调用链：拾取或控制器背包操作 → 服务器组件 → 库存模型 → 复制槽位；客户端复制回调更新模型与背包视图。
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Backpack/InventoryModel.h"
#include "Backpack/ReplicatedInventory.h"
#include "BackpackComponent.generated.h"

class UMVVM_Backpack;
class UItemDefinition;
class UBackpackItemDefinitions;
class APawn;
class AMyPlayerController;

DECLARE_MULTICAST_DELEGATE(FOnBackpackStateChanged);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TEST_25D_API UBackpackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // 启用背包复制，服务器模型与客户端视图通过复制槽位同步。
    UBackpackComponent();

    // 按需建立库存模型及视图模型，已有视图时更新所属控制器。
    void Initialize();
    // 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
    FInventoryAddResult TryAddItem(const UItemDefinition* Definition, int32 Quantity);
    // 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
    FInventoryAddResult TryAddItem(const FPrimaryAssetId& ItemDefinitionId, int32 Quantity);
    // 保留已有实例身份和词条入包，拒绝重复实例及非法定义。
    bool TryAddExistingItem(const FPrimaryAssetId& Id, const FGuid& InstanceId,
                            const FEquipmentRollData& RollData = FEquipmentRollData());
    // 设置共享物品目录供后续查找与库存操作使用。
    void SetItemDefinitions(UBackpackItemDefinitions* InItemDefinitions);
    // 配置新掉落物的网络拾取行为。
    static void ConfigureSpawnedPickupActor(AActor* PickupActor);
    // 先创建并配置携带原实例的掉落物，再清空背包槽并发布更新。
    bool DropWholeStack(int32 SlotIndex, const FVector& SpawnLocation);
    // 交换两个有效槽位，保留槽内物品身份和词条。
    bool SwapSlots(int32 IndexA, int32 IndexB);
    // 验证可使用物品与角色能力系统，施加使用效果成功后扣除一个物品。
    bool TryUseItemForPawn(int32 SlotIndex, APawn* Pawn);
    // 在服务器校验缩容安全性，再调整模型和复制槽位容量。
    bool SetCapacity(int32 NewCapacity);
    // 验证持久化槽位及实例冲突后恢复背包容量、版本和模型。
    bool RestorePersistentSlots(const TArray<FReplicatedInventorySlot>& SavedSlots, int32 SavedCapacity,
                                int32 SavedRevision);

    // 提供库存模型的只读访问。
    const UInventoryModel* GetInventoryModel() const
    {
        return InventoryModel;
    }
    // 提供用于快照导出的复制槽位。
    const FReplicatedInventoryList& GetReplicatedSlots() const
    {
        return ReplicatedSlots;
    }
    // 返回背包当前容量。
    int32 GetCapacity() const
    {
        return Capacity;
    }
    // 返回背包版本供请求校验和快照记录。
    int32 GetRevision() const
    {
        return Revision;
    }

    FOnBackpackStateChanged OnBackpackStateChanged;

    // 登记组件业务状态的网络复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintReadOnly)
    UInventoryModel* InventoryModel;

    UPROPERTY(BlueprintReadOnly)
    UMVVM_Backpack* InventoryVM;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Backpack")
    TObjectPtr<UBackpackItemDefinitions> ItemDefinitions;

private:
    friend class UEquipmentComponent;
    bool bEquipmentMutation = false;
    UFUNCTION()
    // 将服务器复制的槽位与容量应用到本地模型。
    void OnRep_Inventory();

    // 判断组件所有者是否拥有服务器权限。
    bool HasServerAuthority() const;
    // 从所属玩家状态查找背包视图的玩家控制器。
    AMyPlayerController* GetOwningPlayerController() const;
    // 按当前容量建立复制槽位。
    void InitializeReplicatedSlots();
    // 调整快速数组容量并标记新增槽位或数组删除变化。
    void ResizeReplicatedSlots(int32 NewCapacity);
    // 比较服务器模型与复制槽位，标记变化并推进背包版本。
    void SynchronizeReplicatedSlotsFromModel();

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    FReplicatedInventoryList ReplicatedSlots;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    int32 Capacity = 16;

    UPROPERTY(ReplicatedUsing = OnRep_Inventory)
    int32 Revision = 0;
};
