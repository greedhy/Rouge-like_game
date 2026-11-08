/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 主链路：拾取/控制器RPC -> 服务器背包操作 -> InventoryModel -> SynchronizeReplicatedSlotsFromModel -> 复制列表 -> 客户端OnRep_Inventory -> 本地模型与MVVM。
 * 生命周期：PlayerState创建本组件；Initialize按需NewObject创建InventoryModel与InventoryVM，以组件为Outer且由成员引用持有。重复Initialize只刷新控制器，不重复建立模型。它们是本地对象，不作为独立网络对象复制。
 * 同步：ReplicatedSlots使用FastArray差量序列化；项目显式MarkItemDirty/MarkArrayDirty通知变化。Capacity和Revision也复制；没有OwnerOnly条件，复制范围由宿主决定。客户端按复制数据重建本地视图，不自行生成装备实例或词条。
 * 装备事务以双Revision和实例ID校验旧UI请求；存档恢复通过背包装备联合快照，避免仅恢复库存而遗失已穿戴实例。
 */
// 调用链：拾取或控制器背包操作 → 服务器组件 → 库存模型 → 复制槽位；客户端复制回调更新模型与背包视图。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Backpack/BackpackComponent.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/InventoryModel.h"
#include "Backpack/ItemDefinition.h"
#include "Backpack/WorldPickupComponent.h"
#include "Player/MyPlayerController.h"
#include "Player/MyPlayerState.h"
#include "UI/ViewModel/MVVM_InventorySlot.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/Pawn.h"

// 启用背包复制，服务器模型与客户端视图通过复制槽位同步。
UBackpackComponent::UBackpackComponent()
{
    SetIsReplicatedByDefault(true);
}

// 按需建立库存模型及视图模型，已有视图时更新所属控制器。
// 按需且可重复调用：UPROPERTY引用保持模型/VM存活，Outer只描述归属，不能代替GC强引用。客户端这里只应用收到的槽位，不拥有库存修改权。
void UBackpackComponent::Initialize()
{
    if (IsValid(InventoryModel))
    {
        if (IsValid(InventoryVM))
        {
            InventoryVM->SetOwningPlayerController(GetOwningPlayerController());
        }
        return;
    }
    InventoryModel = NewObject<UInventoryModel>(this);
    InventoryModel->SetItemDefinitions(ItemDefinitions);
    if (HasServerAuthority())
    {
        InitializeReplicatedSlots();
    }
    InventoryModel->ApplyReplicatedSlots(ReplicatedSlots.Items, Capacity);
    InventoryVM = NewObject<UMVVM_Backpack>(this);
    InventoryVM->Initialize(InventoryModel, GetOwningPlayerController());
}

// 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
FInventoryAddResult UBackpackComponent::TryAddItem(const UItemDefinition* Definition, int32 Quantity)
{
    FInventoryAddResult Result;
    if (!HasServerAuthority() || bEquipmentMutation || !IsValid(Definition) || Quantity <= 0)
    {
        return Result;
    }

    Initialize();
    if (const auto* Entry = InventoryModel->FindItemDefinition(Definition->GetPrimaryAssetId());
        Entry && Entry->bRandomizedEquipment)
        return TryAddItem(Definition->GetPrimaryAssetId(), Quantity);
    Result = InventoryModel->TryAddItem(Definition, Quantity);
    if (Result.bSucceeded)
    {
        SynchronizeReplicatedSlotsFromModel();
        OnBackpackStateChanged.Broadcast();
    }
    return Result;
}

// 按物品定义尝试入包并返回接收和剩余数量；随机装备由组件生成实例。
FInventoryAddResult UBackpackComponent::TryAddItem(const FPrimaryAssetId& ItemDefinitionId, int32 Quantity)
{
    FInventoryAddResult Result;
    if (!HasServerAuthority() || bEquipmentMutation || !ItemDefinitionId.IsValid() || Quantity <= 0)
    {
        return Result;
    }

    Initialize();
    const auto* Definition = InventoryModel->FindItemDefinition(ItemDefinitionId);
    if (Definition && Definition->bRandomizedEquipment)
    {
        Result.RemainingQuantity = Quantity;
        while (Result.RemainingQuantity > 0 && InventoryModel->Slots.ContainsByPredicate(
                                                   [](const FInventorySlot& S)
                                                   {
                                                       return S.IsEmpty();
                                                   }))
        {
            FEquipmentRollData Roll;
            FString Error;
            if (!FEquipmentGenerator::Generate(*ItemDefinitions, *Definition, 0, FMath::Rand(), Roll, Error))
                break;
            if (!TryAddExistingItem(ItemDefinitionId, FGuid::NewGuid(), Roll))
                break;
            ++Result.AcceptedQuantity;
            --Result.RemainingQuantity;
        }
        Result.bSucceeded = Result.AcceptedQuantity > 0;
        return Result;
    }
    Result = InventoryModel->TryAddItem(ItemDefinitionId, Quantity);
    if (Result.bSucceeded)
    {
        SynchronizeReplicatedSlotsFromModel();
        OnBackpackStateChanged.Broadcast();
    }
    return Result;
}

// 保留已有实例身份和词条入包，拒绝重复实例及非法定义。
bool UBackpackComponent::TryAddExistingItem(const FPrimaryAssetId& Id, const FGuid& InstanceId,
                                            const FEquipmentRollData& RollData)
{
    if (!HasServerAuthority() || bEquipmentMutation || !InstanceId.IsValid())
        return false;
    Initialize();
    if (const auto* Equipment = GetOwner()->FindComponentByClass<UEquipmentComponent>())
        if (Equipment->ContainsInstance(InstanceId))
            return false;
    const auto* Def = InventoryModel->FindItemDefinition(Id);
    if (!Def || !Def->bEquippable || Def->bStackable || Def->MaxStack != 1 ||
        !FEquipmentGenerator::ValidateRoll(RollData) || Def->bRandomizedEquipment != (RollData.GenerationVersion > 0) ||
        (Def->bRandomizedEquipment && RollData.ProfileId != Def->GenerationProfile))
        return false;
    for (const auto& Slot : InventoryModel->Slots)
        if (Slot.ItemInstanceId == InstanceId)
            return false;
    for (auto& Slot : InventoryModel->Slots)
        if (Slot.IsEmpty())
        {
            Slot.ItemDefinitionId = Id;
            Slot.ItemInstanceId = InstanceId;
            Slot.Quantity = 1;
            Slot.RollData = RollData;
            SynchronizeReplicatedSlotsFromModel();
            InventoryModel->OnInventoryUpdated.Broadcast(InventoryModel->Slots);
            OnBackpackStateChanged.Broadcast();
            return true;
        }
    return false;
}

// 设置共享物品目录供后续查找与库存操作使用。
void UBackpackComponent::SetItemDefinitions(UBackpackItemDefinitions* InItemDefinitions)
{
    ItemDefinitions = InItemDefinitions;
    if (IsValid(InventoryModel))
    {
        InventoryModel->SetItemDefinitions(ItemDefinitions);
    }
}

// 配置新掉落物的网络拾取行为。
void UBackpackComponent::ConfigureSpawnedPickupActor(AActor* PickupActor)
{
    UWorldPickupComponent::ConfigureOwnerForNetworkedPickup(PickupActor);
}

// 先创建并配置携带原实例的掉落物，再清空背包槽并发布更新。
bool UBackpackComponent::DropWholeStack(int32 SlotIndex, const FVector& SpawnLocation)
{
    if (!HasServerAuthority() || bEquipmentMutation)
    {
        return false;
    }

    Initialize();
    const TArray<FInventorySlot>& Slots = InventoryModel->GetSlots();
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
    {
        return false;
    }

    const FInventorySlot StackToDrop = Slots[SlotIndex];
    const FBackpackItemDefEntry* Definition = InventoryModel->FindItemDefinition(StackToDrop.ItemDefinitionId);
    if (!Definition || !Definition->PickupActorClass)
    {
        return false;
    }

    const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
    AActor* DroppedActor = GetWorld()->SpawnActorDeferred<AActor>(Definition->PickupActorClass, SpawnTransform);
    if (!DroppedActor)
        return false;
    // Blueprint-added pickup components exist after construction. Keep overlaps disabled until configured.
    // 掉落配置完成前关闭碰撞，避免拾取回调抢先读取尚未迁移的实例。
    DroppedActor->SetActorEnableCollision(false);
    DroppedActor->FinishSpawning(SpawnTransform);
    ConfigureSpawnedPickupActor(DroppedActor);
    UWorldPickupComponent* DroppedPickup =
        IsValid(DroppedActor) ? DroppedActor->FindComponentByClass<UWorldPickupComponent>() : nullptr;
    if (!IsValid(DroppedPickup))
    {
        if (IsValid(DroppedActor))
            DroppedActor->Destroy();
        return false;
    }
    DroppedPickup->ItemDefinitions = ItemDefinitions;
    DroppedPickup->ConfigurePickup(StackToDrop.ItemDefinitionId, StackToDrop.Quantity, true, &StackToDrop.RollData);
    DroppedPickup->ItemInstanceId = StackToDrop.ItemInstanceId;
    DroppedPickup->RollData = StackToDrop.RollData;
    // 掉落物准备完成后才清空来源槽，随后同步复制并开放拾取。
    InventoryModel->Slots[SlotIndex].Clear();
    SynchronizeReplicatedSlotsFromModel();
    DroppedActor->SetActorEnableCollision(true);
    InventoryModel->OnInventoryUpdated.Broadcast(InventoryModel->Slots);
    OnBackpackStateChanged.Broadcast();
    return true;
}

// 交换两个有效槽位，保留槽内物品身份和词条。
bool UBackpackComponent::SwapSlots(int32 IndexA, int32 IndexB)
{
    if (!HasServerAuthority() || bEquipmentMutation)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Server rejected swap %d -> %d: component has no authority."),
               IndexA, IndexB);
        return false;
    }

    Initialize();
    if (!InventoryModel->SwapSlots(IndexA, IndexB))
    {
        UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Server rejected swap %d -> %d: model rejected the indices."),
               IndexA, IndexB);
        return false;
    }

    SynchronizeReplicatedSlotsFromModel();
    OnBackpackStateChanged.Broadcast();
    UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Server completed swap: %d <-> %d."), IndexA, IndexB);
    return true;
}

// 验证可使用物品与角色能力系统，施加使用效果成功后扣除一个物品。
bool UBackpackComponent::TryUseItemForPawn(int32 SlotIndex, APawn* Pawn)
{
    if (!HasServerAuthority() || bEquipmentMutation || !IsValid(Pawn))
    {
        return false;
    }

    Initialize();
    const TArray<FInventorySlot>& Slots = InventoryModel->GetSlots();
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
    {
        return false;
    }

    const FBackpackItemDefEntry* CatalogDefinition =
        InventoryModel->FindItemDefinition(Slots[SlotIndex].ItemDefinitionId);
    UItemDefinition* LegacyDefinition = InventoryModel->ItemDefinitions.FindRef(Slots[SlotIndex].ItemDefinitionId);
    const bool bUsesCatalogDefinition = CatalogDefinition != nullptr;
    const bool bCanUse =
        bUsesCatalogDefinition
            ? !CatalogDefinition->bEquippable && CatalogDefinition->bUsable && CatalogDefinition->UseEffect
            : IsValid(LegacyDefinition) && LegacyDefinition->bUsable && LegacyDefinition->UseEffect;
    if (!bCanUse)
    {
        return false;
    }

    UAbilitySystemComponent* TargetAbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
    if (!IsValid(TargetAbilitySystem))
    {
        return false;
    }

    FGameplayEffectContextHandle EffectContext = TargetAbilitySystem->MakeEffectContext();
    EffectContext.AddSourceObject(bUsesCatalogDefinition ? static_cast<UObject*>(ItemDefinitions)
                                                         : static_cast<UObject*>(LegacyDefinition));
    const TSubclassOf<UGameplayEffect> UseEffect =
        bUsesCatalogDefinition ? CatalogDefinition->UseEffect : LegacyDefinition->UseEffect;
    const FGameplayEffectSpecHandle EffectSpec = TargetAbilitySystem->MakeOutgoingSpec(UseEffect, 1.f, EffectContext);
    if (!EffectSpec.IsValid())
    {
        return false;
    }

    const FActiveGameplayEffectHandle AppliedEffect =
        TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*EffectSpec.Data.Get());
    if (!AppliedEffect.WasSuccessfullyApplied() || !InventoryModel->UseItemAt(SlotIndex))
    {
        return false;
    }

    SynchronizeReplicatedSlotsFromModel();
    OnBackpackStateChanged.Broadcast();
    return true;
}

// 在服务器校验缩容安全性，再调整模型和复制槽位容量。
bool UBackpackComponent::SetCapacity(int32 NewCapacity)
{
    if (!HasServerAuthority() || bEquipmentMutation)
    {
        return false;
    }

    const int32 ClampedCapacity = FMath::Max(0, NewCapacity);
    Initialize();
    for (int32 I = ClampedCapacity; I < InventoryModel->GetSlots().Num(); ++I)
    {
        const auto& Slot = InventoryModel->GetSlots()[I];
        const auto* Def = InventoryModel->FindItemDefinition(Slot.ItemDefinitionId);
        if (!Slot.IsEmpty() && Def && Def->bEquippable)
            return false;
    }
    if (Capacity == ClampedCapacity)
    {
        return false;
    }

    Initialize();
    InventoryModel->SetMaxSlots(ClampedCapacity);
    ResizeReplicatedSlots(ClampedCapacity);
    Capacity = ClampedCapacity;
    ++Revision;
    OnBackpackStateChanged.Broadcast();
    return true;
}

// 验证持久化槽位及实例冲突后恢复背包容量、版本和模型。
bool UBackpackComponent::RestorePersistentSlots(const TArray<FReplicatedInventorySlot>& SavedSlots, int32 SavedCapacity,
                                                int32 SavedRevision)
{
    if (!HasServerAuthority() || bEquipmentMutation || SavedCapacity < 0 || SavedSlots.Num() != SavedCapacity)
    {
        return false;
    }

    Initialize();
    TSet<FGuid> Seen;
    const auto* Equipment = GetOwner()->FindComponentByClass<UEquipmentComponent>();
    for (const auto& Item : SavedSlots)
    {
        const auto* Def = InventoryModel->FindItemDefinition(Item.ItemDefinitionId);
        if (!FEquipmentGenerator::ValidateForDefinition(Def, Item.RollData))
            return false;
        if (Item.IsEmpty())
        {
            if (Item.RollData.GenerationVersion != 0)
                return false;
            continue;
        }
        if (Def && Def->bEquippable && (Item.Quantity != 1 || !Item.ItemInstanceId.IsValid()))
            return false;
        if (Item.ItemInstanceId.IsValid() &&
            (Seen.Contains(Item.ItemInstanceId) || (Equipment && Equipment->ContainsInstance(Item.ItemInstanceId))))
            return false;
        if (Item.ItemInstanceId.IsValid())
            Seen.Add(Item.ItemInstanceId);
    }
    ReplicatedSlots.Items = SavedSlots;
    for (FReplicatedInventorySlot& Slot : ReplicatedSlots.Items)
    {
        if (Slot.IsEmpty())
        {
            Slot.Clear();
        }
    }
    ReplicatedSlots.MarkArrayDirty();
    Capacity = SavedCapacity;
    Revision = FMath::Max(0, SavedRevision);
    InventoryModel->ApplyReplicatedSlots(ReplicatedSlots.Items, Capacity);
    return true;
}

// 登记组件业务状态的网络复制字段。
void UBackpackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UBackpackComponent, ReplicatedSlots);
    DOREPLIFETIME(UBackpackComponent, Capacity);
    DOREPLIFETIME(UBackpackComponent, Revision);
}

// 将服务器复制的槽位与容量应用到本地模型。
// 收到属性复制后按需创建本地模型，再应用当前槽位与容量；不同组件复制不保证同一帧到达，因此请求仍携带双版本防止使用过期UI。
void UBackpackComponent::OnRep_Inventory()
{
    Initialize();
    InventoryModel->ApplyReplicatedSlots(ReplicatedSlots.Items, Capacity);
}

// 判断组件所有者是否拥有服务器权限。
bool UBackpackComponent::HasServerAuthority() const
{
    return GetOwner() && GetOwner()->HasAuthority();
}

// 从所属玩家状态查找背包视图的玩家控制器。
AMyPlayerController* UBackpackComponent::GetOwningPlayerController() const
{
    if (const AMyPlayerState* PlayerState = Cast<AMyPlayerState>(GetOwner()))
    {
        return Cast<AMyPlayerController>(PlayerState->GetPlayerController());
    }
    return nullptr;
}

// 按当前容量建立复制槽位。
void UBackpackComponent::InitializeReplicatedSlots()
{
    ResizeReplicatedSlots(Capacity);
}

// 调整快速数组容量并标记新增槽位或数组删除变化。
void UBackpackComponent::ResizeReplicatedSlots(int32 NewCapacity)
{
    if (NewCapacity < ReplicatedSlots.Items.Num())
    {
        ReplicatedSlots.Items.SetNum(NewCapacity);
        ReplicatedSlots.MarkArrayDirty();
        return;
    }

    const int32 FirstNewSlot = ReplicatedSlots.Items.Num();
    ReplicatedSlots.Items.SetNum(NewCapacity);
    for (int32 SlotIndex = FirstNewSlot; SlotIndex < NewCapacity; ++SlotIndex)
    {
        ReplicatedSlots.MarkItemDirty(ReplicatedSlots.Items[SlotIndex]);
    }
}

// 比较服务器模型与复制槽位，标记变化并推进背包版本。
// 仅服务器调用：逐槽比较身份、数量和RollData，变化槽标脏并推进Revision。这里不是发送RPC，差量在后续网络复制轮次传输。
void UBackpackComponent::SynchronizeReplicatedSlotsFromModel()
{
    check(HasServerAuthority());
    const TArray<FInventorySlot>& ModelSlots = InventoryModel->GetSlots();
    check(ModelSlots.Num() == ReplicatedSlots.Items.Num());

    bool bChanged = false;
    for (int32 SlotIndex = 0; SlotIndex < ModelSlots.Num(); ++SlotIndex)
    {
        FReplicatedInventorySlot& ReplicatedSlot = ReplicatedSlots.Items[SlotIndex];
        const FInventorySlot& ModelSlot = ModelSlots[SlotIndex];
        if (ReplicatedSlot.ItemDefinitionId == ModelSlot.ItemDefinitionId &&
            ReplicatedSlot.Quantity == ModelSlot.Quantity &&
            ReplicatedSlot.ItemInstanceId == ModelSlot.ItemInstanceId && ReplicatedSlot.RollData == ModelSlot.RollData)
        {
            continue;
        }

        ReplicatedSlot.ItemDefinitionId = ModelSlot.ItemDefinitionId;
        ReplicatedSlot.Quantity = ModelSlot.Quantity;
        ReplicatedSlot.ItemInstanceId = ModelSlot.ItemInstanceId;
        ReplicatedSlot.RollData = ModelSlot.RollData;
        ReplicatedSlots.MarkItemDirty(ReplicatedSlot);
        bChanged = true;
    }

    if (bChanged)
    {
        ++Revision;
    }
}
