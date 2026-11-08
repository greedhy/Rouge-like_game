/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 主链路：UI RequestEquip/RequestUnequip读取实例ID及背包/装备Revision -> 所属组件可靠Server RPC -> TryEquip/TryUnequip服务器复核 -> 准备GE/能力 -> 联合提交 -> ClientResult及状态复制。
 * 同步时机：服务器RPC完成后ForceNetUpdate请求尽快更新；Slots/Revision/bNeedsReconcile为普通复制属性，没有OwnerOnly条件。实际接收者仍受宿主PlayerState的网络相关性约束。
 * ClientResult仅返回本次成功与否，不携带完整装备状态；不可认为收到成功回执时背包与装备复制已经同时到达。客户端OnRep_State刷新外观和UI，服务器提交也显式调用它服务监听服务器本地显示。
 * 生命周期：由PlayerState构造并持有；运行GE句柄和武器能力句柄仅在服务器维护。Avatar/ASC变化时ReconcileEffects重建归属，EndPlay推进WeaponGeneration、撤销本组件武器能力并清理装备GE；不清空整个ASC。
 */
// 调用链：界面请求 → 服务器装备或卸装 → 背包实例校验、效果及武器能力准备 → 联合提交 → 复制与持久化通知。
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentSlots.h"
#include "Equipment/EquipmentStats.h"
#include "Equipment/EquipmentGameplayEffect.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ScopeExit.h"
#include "Equipment/WeaponSetConfig.h"

// 启用装备组件复制，由玩家状态持有装备业务状态。
UEquipmentComponent::UEquipmentComponent()
{
    SetIsReplicatedByDefault(true);
}
// 注册时补齐标准装备槽，兼容旧蓝图默认配置。
void UEquipmentComponent::OnRegister()
{
    // 注册时补齐标准装备槽，兼容旧蓝图默认配置。
    Super::OnRegister();
    // Extend legacy blueprint defaults without changing saved slot identities.
    for (const auto& Slot : EquipmentSlots::All())
        AvailableSlots.AddTag(Slot.Tag);
}
// 从所属玩家状态取得背包组件。
UBackpackComponent* UEquipmentComponent::Backpack() const
{
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    return State ? State->GetBackpackComponent() : nullptr;
}
// 从所属玩家状态取得共享能力系统组件。
UAbilitySystemComponent* UEquipmentComponent::ASC() const
{
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    return State ? State->GetAbilitySystemComponent() : nullptr;
}
// 通过背包共用目录查询物品定义。
const FBackpackItemDefEntry* UEquipmentComponent::Definition(const FPrimaryAssetId& Id) const
{
    const auto* Bag = Backpack();
    return Bag && Bag->ItemDefinitions ? Bag->ItemDefinitions->FindDefinition(Id) : nullptr;
}
// 检查指定实例是否已占用装备槽。
bool UEquipmentComponent::ContainsInstance(const FGuid& Id) const
{
    return Id.IsValid() && Slots.ContainsByPredicate(
                               [&](const FEquipmentSlot& S)
                               {
                                   return S.ItemInstanceId == Id;
                               });
}
// 验证服务器权限、双版本、事务重入及当前角色存活状态。
bool UEquipmentComponent::CanMutate(int32 BagRevision, int32 EquipmentRevision) const
{
    const auto* Bag = Backpack();
    const auto* AbilitySystem = ASC();
    const auto* State = Cast<AMyPlayerState>(GetOwner());
    if (!State || !State->HasAuthority() || !Bag || bMutating || Bag->bEquipmentMutation ||
        BagRevision != Bag->GetRevision() || EquipmentRevision != Revision || !AbilitySystem ||
        AbilitySystem->GetOwnerActor() != State || !AbilitySystem->GetAvatarActor() ||
        AbilitySystem->GetAvatarActor() != State->GetPawn())
        return false;
    AActor* Avatar = AbilitySystem->GetAvatarActor();
    if (Avatar->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Avatar))
        return false;
    const auto* Dungeon = Cast<ADungeonPlayerCharacter>(Avatar);
    return !Dungeon || !Dungeon->IsDungeonDowned();
}
// 校验槽位、实例唯一性、装备效果及随机词条和武器配置。
bool UEquipmentComponent::ValidateRecords(const TArray<FEquipmentSlot>& Records) const
{
    int32 WeaponSets = 0;
    TSet<FGuid> Ids;
    TSet<FGameplayTag> UsedSlots;
    for (const auto& Slot : Records)
    {
        const auto* Def = Definition(Slot.ItemDefinitionId);
        if (!Slot.ItemInstanceId.IsValid() || Ids.Contains(Slot.ItemInstanceId) || UsedSlots.Contains(Slot.SlotTag) ||
            !AvailableSlots.HasTagExact(Slot.SlotTag) || !Def || !Def->bEquippable || Def->bStackable ||
            Def->MaxStack != 1 || !Def->AllowedEquipmentSlots.HasTagExact(Slot.SlotTag) || !Def->EquipEffect ||
            !Def->EquipEffect.GetDefaultObject()->IsSafeEquipmentEffect(Def->EquipEffectLevel,
                                                                        Def->bRandomizedEquipment) ||
            !FEquipmentGenerator::ValidateRoll(Slot.RollData) ||
            Def->bRandomizedEquipment != (Slot.RollData.GenerationVersion > 0) ||
            (Def->bRandomizedEquipment && Slot.RollData.ProfileId != Def->GenerationProfile))
            return false;
        if (Def->WeaponSet && (++WeaponSets > 1 || !Def->WeaponSet->Validate()))
            return false;
        Ids.Add(Slot.ItemInstanceId);
        UsedSlots.Add(Slot.SlotTag);
    }
    return true;
}
// 构造装备效果规格并注入词条总值，只接受实际生效且未受抑制的效果。
FActiveGameplayEffectHandle UEquipmentComponent::Apply(const FBackpackItemDefEntry& Def, const FEquipmentRollData& Roll)
{
    auto* AbilitySystem = ASC();
    auto Context = AbilitySystem->MakeEffectContext();
    Context.AddSourceObject(this);
    auto Spec = AbilitySystem->MakeOutgoingSpec(Def.EquipEffect, Def.EquipEffectLevel, Context);
    if (!Spec.IsValid())
        return {};
    if (Def.bRandomizedEquipment)
    {
        if (!FEquipmentGenerator::ValidateRoll(Roll) || Roll.ProfileId != Def.GenerationProfile)
            return {};
        const auto Totals = FEquipmentGenerator::GetTotals(Roll);
        const auto* Effect = Def.EquipEffect.GetDefaultObject();
        TSet<FGameplayTag> Supported;
        for (const auto& Modifier : Effect->Modifiers)
        {
            const auto Tag = Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag;
            Supported.Add(Tag);
            Spec.Data->SetSetByCallerMagnitude(Tag, Totals.FindRef(Tag));
        }
        // Subset templates are allowed, but must never silently discard rolled stats.
        for (const auto& Total : Totals)
            if (!Supported.Contains(Total.Key))
                return {};
    }
    auto Handle = AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    const auto* Active = AbilitySystem->GetActiveGameplayEffect(Handle);
    if (!Handle.WasSuccessfullyApplied() || !Active || Active->bIsInhibited)
    {
        if (Active)
            AbilitySystem->RemoveActiveGameplayEffect(Handle);
        return {};
    }
    return Handle;
}
// 按实例句柄检查当前能力系统上的装备效果是否仍有效。
bool UEquipmentComponent::HasEffect(const FGuid& Id) const
{
    const auto* Handle = Handles.Find(Id);
    const auto* Active = Handle && EffectASC.Get() == ASC() && EffectASC.IsValid()
                             ? EffectASC->GetActiveGameplayEffect(*Handle)
                             : nullptr;
    return Active && !Active->bIsInhibited;
}
// 按最终生命和法力上限裁剪当前资源，不因上限增加补满。
void UEquipmentComponent::ClampResources()
{
    auto* AbilitySystem = ASC();
    if (!AbilitySystem)
        return;
    const float Health = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
    const float Mana = AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetManaAttribute());
    const float MaxHealth =
        FMath::Max(0.f, AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()));
    const float MaxMana = FMath::Max(0.f, AbilitySystem->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()));
    if (Health > MaxHealth)
        AbilitySystem->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), MaxHealth);
    if (Mana > MaxMana)
        AbilitySystem->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), MaxMana);
}
// 统一提交装备版本、背包复制和资源修正，再通知界面及持久化监听者。
void UEquipmentComponent::Commit()
{
    auto* Bag = Backpack();
    ++Revision;
    Bag->SynchronizeReplicatedSlotsFromModel();
    ClampResources();
    GetOwner()->ForceNetUpdate();
    Bag->InventoryModel->OnInventoryUpdated.Broadcast(Bag->InventoryModel->Slots);
    OnEquipmentChanged.Broadcast();
    // The existing GameMode persistence listener now exports BOTH collections.
    Bag->OnBackpackStateChanged.Broadcast();
}
// 校验背包实例后准备武器能力和新效果，将旧装备返还原背包格并统一提交。
bool UEquipmentComponent::TryEquip(int32 Index, FGameplayTag Tag, FGuid Expected, int32 BagRevision,
                                   int32 EquipmentRevision)
{
    if (!CanMutate(BagRevision, EquipmentRevision))
        return false;
    if (bNeedsReconcile && !ReconcileEffects())
        return false;
    auto* Bag = Backpack();
    Bag->Initialize();
    auto& Items = Bag->InventoryModel->Slots;
    if (!Items.IsValidIndex(Index) || Items[Index].IsEmpty() || Items[Index].Quantity != 1 ||
        Items[Index].ItemInstanceId != Expected || ContainsInstance(Expected))
        return false;
    FEquipmentSlot NewSlot;
    NewSlot.SlotTag = Tag;
    NewSlot.ItemDefinitionId = Items[Index].ItemDefinitionId;
    NewSlot.ItemInstanceId = Expected;
    NewSlot.RollData = Items[Index].RollData;
    if (!ValidateRecords({NewSlot}))
        return false;
    const int32 OldIndex = Slots.IndexOfByPredicate(
        [&](const FEquipmentSlot& S)
        {
            return S.SlotTag == Tag;
        });
    if (OldIndex != INDEX_NONE && !HasEffect(Slots[OldIndex].ItemInstanceId))
    {
        bNeedsReconcile = true;
        OnRep_State();
        return false;
    }
    // 同时锁定装备与背包变更，防止效果回调重入造成实例重复移动。
    TGuardValue<bool> Guard(bMutating, true);
    TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
    auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
    if (!Attributes)
        return false;
    TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
    TArray<FEquipmentSlot> Proposed = Slots;
    if (OldIndex != INDEX_NONE)
        Proposed[OldIndex] = NewSlot;
    else
        Proposed.Add(NewSlot);
    TArray<FGameplayAbilitySpecHandle> Prepared;
    if (!PrepareWeaponSet(Proposed, Prepared))
        return false;
    bool WeaponCommitted = false;
    ON_SCOPE_EXIT
    {
        if (!WeaponCommitted)
            DiscardWeaponSet(Prepared);
    };
    // 新效果先验证成功，再移除旧效果；旧效果移除失败时撤销新效果。
    const auto Handle = Apply(*Definition(NewSlot.ItemDefinitionId), NewSlot.RollData);
    if (!Handle.IsValid())
        return false;
    EffectASC = ASC();
    BindEffects();
    FInventorySlot Returned;
    if (OldIndex != INDEX_NONE)
    {
        const auto OldSlot = Slots[OldIndex];
        if (!ASC()->RemoveActiveGameplayEffect(Handles.FindChecked(OldSlot.ItemInstanceId)))
        {
            ASC()->RemoveActiveGameplayEffect(Handle);
            return false;
        }
        Handles.Remove(OldSlot.ItemInstanceId);
        Returned.ItemDefinitionId = OldSlot.ItemDefinitionId;
        Returned.ItemInstanceId = OldSlot.ItemInstanceId;
        Returned.Quantity = 1;
        Returned.RollData = OldSlot.RollData;
        Slots[OldIndex] = NewSlot;
    }
    else
        Slots.Add(NewSlot);
    // 用新装备腾出的格子承接旧装备，因此满背包仍可原位换装。
    Items[Index] = Returned;
    Handles.Add(Expected, Handle);
    CommitWeaponSet(Slots, Prepared);
    WeaponCommitted = true;
    Commit();
    return true;
}
// 预留背包空格并准备武器切换，移除实例效果后返还原物品。
bool UEquipmentComponent::TryUnequip(FGameplayTag Tag, FGuid Expected, int32 BagRevision, int32 EquipmentRevision)
{
    if (!CanMutate(BagRevision, EquipmentRevision))
        return false;
    if (bNeedsReconcile && !ReconcileEffects())
        return false;
    auto* Bag = Backpack();
    Bag->Initialize();
    const int32 Index = Slots.IndexOfByPredicate(
        [&](const FEquipmentSlot& S)
        {
            return S.SlotTag == Tag && S.ItemInstanceId == Expected;
        });
    const int32 Empty = Bag->InventoryModel->Slots.IndexOfByPredicate(
        [](const FInventorySlot& S)
        {
            return S.IsEmpty();
        });
    if (Index == INDEX_NONE || Empty == INDEX_NONE)
        return false;
    if (!HasEffect(Expected))
    {
        bNeedsReconcile = true;
        OnRep_State();
        return false;
    }
    // 同时锁定装备与背包变更，防止效果回调重入造成实例重复移动。
    TGuardValue<bool> Guard(bMutating, true);
    TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
    auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
    if (!Attributes)
        return false;
    TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
    TArray<FEquipmentSlot> Proposed = Slots;
    Proposed.RemoveAt(Index);
    TArray<FGameplayAbilitySpecHandle> Prepared;
    if (!PrepareWeaponSet(Proposed, Prepared))
        return false;
    bool WeaponCommitted = false;
    ON_SCOPE_EXIT
    {
        if (!WeaponCommitted)
            DiscardWeaponSet(Prepared);
    };
    if (!ASC()->RemoveActiveGameplayEffect(Handles.FindChecked(Expected)))
        return false;
    auto& Item = Bag->InventoryModel->Slots[Empty];
    Item.ItemDefinitionId = Slots[Index].ItemDefinitionId;
    Item.ItemInstanceId = Expected;
    Item.Quantity = 1;
    Item.RollData = Slots[Index].RollData;
    Slots.RemoveAt(Index);
    Handles.Remove(Expected);
    CommitWeaponSet(Slots, Prepared);
    WeaponCommitted = true;
    Commit();
    return true;
}
// 订阅装备效果移除事件，用于检测外部移除造成的状态缺失。
void UEquipmentComponent::BindEffects()
{
    if (EffectASC.IsValid() && !RemovedDelegate.IsValid())
        RemovedDelegate = EffectASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this, &ThisClass::OnEffectRemoved);
}
// 忽略事务内部移除；外部移除本组件效果时标记需要对账。
void UEquipmentComponent::OnEffectRemoved(const FActiveGameplayEffect& Effect)
{
    if (bMutating)
        return;
    for (const auto& Pair : Handles)
        if (Pair.Value == Effect.Handle)
        {
            bNeedsReconcile = true;
            GetOwner()->ForceNetUpdate();
            OnRep_State();
            return;
        }
}
// 解绑移除通知并按本组件保存的句柄清除效果。
void UEquipmentComponent::ClearEffects()
{
    if (EffectASC.IsValid())
        EffectASC->OnAnyGameplayEffectRemovedDelegate().Remove(RemovedDelegate);
    RemovedDelegate.Reset();
    if (EffectASC.IsValid())
        for (const auto& Pair : Handles)
            EffectASC->RemoveActiveGameplayEffect(Pair.Value);
    Handles.Reset();
    EffectASC.Reset();
}
// 校验背包与装备跨集合实例唯一性，联合恢复记录并按角色就绪状态重建效果。
bool UEquipmentComponent::RestoreCombined(const TArray<FReplicatedInventorySlot>& Items, int32 Capacity,
                                          int32 BagRevision, const TArray<FEquipmentSlot>& Records, int32 SavedRevision)
{
    auto* Bag = Backpack();
    if (!GetOwner()->HasAuthority() || bMutating || !Bag || Bag->bEquipmentMutation || Capacity < 0 ||
        Items.Num() != Capacity || !ValidateRecords(Records))
        return false;
    TSet<FGuid> SeenInstances;
    for (const auto& Record : Records)
        SeenInstances.Add(Record.ItemInstanceId);
    for (const auto& Item : Items)
    {
        if (!FEquipmentGenerator::ValidateRoll(Item.RollData))
            return false;
        if (Item.IsEmpty())
        {
            if (Item.RollData.GenerationVersion != 0)
                return false;
            continue;
        }
        const auto* Def = Definition(Item.ItemDefinitionId);
        if (!FEquipmentGenerator::ValidateForDefinition(Def, Item.RollData))
            return false;
        if (Def && Def->bEquippable && (Item.Quantity != 1 || !Item.ItemInstanceId.IsValid()))
            return false;
        if (Item.ItemInstanceId.IsValid() && SeenInstances.Contains(Item.ItemInstanceId))
            return false;
        if (Item.ItemInstanceId.IsValid())
            SeenInstances.Add(Item.ItemInstanceId);
    }

    {
        TGuardValue<bool> Guard(bMutating, true);
        TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
        auto* Attributes = ASC() ? const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>()) : nullptr;
        bool Unused = false;
        TGuardValue<bool> AttributeGuard(Attributes ? Attributes->bEquipmentChanging : Unused, true);
        // Live restores must validate and stage the new ability group before publishing either snapshot.
        // Initial loads without actor info remain pending until ReconcileEffects after initialization.
        const bool bLiveRestore = IsReadyForCurrentAvatar();
        TArray<FGameplayAbilitySpecHandle> Prepared;
        if (bLiveRestore && !PrepareWeaponSet(Records, Prepared))
            return false;
        bool WeaponCommitted = false;
        ON_SCOPE_EXIT
        {
            if (!WeaponCommitted)
                DiscardWeaponSet(Prepared);
        };
        Bag->Initialize();
        // Publish both record collections before any GAS removal callback can inspect them.
        // 联合发布两个物品集合后再清理旧效果，避免回调观察到半份快照。
        Slots = Records;
        Revision = FMath::Max(0, SavedRevision);
        Bag->ReplicatedSlots.Items = Items;
        Bag->Capacity = Capacity;
        Bag->Revision = FMath::Max(0, BagRevision);
        Bag->ReplicatedSlots.MarkArrayDirty();
        Bag->InventoryModel->ApplyReplicatedSlots(Items, Capacity, false);
        ClearEffects();
        bNeedsReconcile = !Slots.IsEmpty();
        if (bLiveRestore)
            CommitWeaponSet(Slots, Prepared);
        WeaponCommitted = true;
    }
    if (IsReadyForCurrentAvatar())
        ReconcileEffects();
    {
        TGuardValue<bool> Guard(bMutating, true);
        TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
        Bag->InventoryModel->OnInventoryMaxSlotsChanged.Broadcast(Capacity);
        Bag->InventoryModel->OnInventoryUpdated.Broadcast(Bag->InventoryModel->Slots);
        OnRep_State();
        GetOwner()->ForceNetUpdate();
    }
    return true;
}
// 判断效果初始化记录是否对应当前角色。
bool UEquipmentComponent::IsReadyForCurrentAvatar() const
{
    return ASC() && ReadyAvatar.IsValid() && ReadyAvatar.Get() == ASC()->GetAvatarActor();
}
// 在角色初始化后补建缺失效果和武器能力，保留有效效果以避免重复加成。
bool UEquipmentComponent::ReconcileEffects()
{
    if (!GetOwner()->HasAuthority() || bMutating || !ASC() || ASC()->GetOwnerActor() != GetOwner() ||
        !ASC()->GetAvatarActor())
        return false;
    auto* Bag = Backpack();
    if (!Bag || Bag->bEquipmentMutation)
        return false;
    // 同时锁定装备与背包变更，防止效果回调重入造成实例重复移动。
    TGuardValue<bool> Guard(bMutating, true);
    TGuardValue<bool> BagGuard(Bag->bEquipmentMutation, true);
    auto* Attributes = const_cast<UMyAttributeSet*>(ASC()->GetSet<UMyAttributeSet>());
    if (!Attributes)
        return false;
    TGuardValue<bool> AttributeGuard(Attributes->bEquipmentChanging, true);
    if (EffectASC.Get() != ASC())
        ClearEffects();
    EffectASC = ASC();
    BindEffects();
    if (ReadyAvatar.Get() != ASC()->GetAvatarActor())
        ++WeaponGeneration;
    ReadyAvatar = ASC()->GetAvatarActor();
    bNeedsReconcile = !ValidateRecords(Slots);
    if (!bNeedsReconcile)
        for (const auto& Slot : Slots)
        {
            // 同一能力系统上的有效实例效果直接保留，只修复缺失或抑制的效果。
            if (HasEffect(Slot.ItemInstanceId))
                continue;
            if (auto* Old = Handles.Find(Slot.ItemInstanceId))
                ASC()->RemoveActiveGameplayEffect(*Old);
            auto Handle = Apply(*Definition(Slot.ItemDefinitionId), Slot.RollData);
            if (Handle.IsValid())
                Handles.Add(Slot.ItemInstanceId, Handle);
            else
            {
                Handles.Remove(Slot.ItemInstanceId);
                bNeedsReconcile = true;
            }
        }
    if (!bNeedsReconcile)
    {
        TArray<FGameplayAbilitySpecHandle> Prepared;
        if (PrepareWeaponSet(Slots, Prepared))
            CommitWeaponSet(Slots, Prepared);
        else
            bNeedsReconcile = true;
    }
    ClampResources();
    OnRep_State();
    return !bNeedsReconcile;
}
// 从本地背包读取实例及双版本并向服务器发起装备请求。
void UEquipmentComponent::RequestEquip(int32 Index, FGameplayTag Tag)
{
    auto* Bag = Backpack();
    if (!Bag)
        return;
    Bag->Initialize();
    const auto& Items = Bag->GetInventoryModel()->GetSlots();
    if (!Items.IsValidIndex(Index) || Items[Index].IsEmpty())
        return;
    ServerEquip(Index, Tag, Items[Index].ItemInstanceId, Bag->GetRevision(), Revision);
}
// 从本地装备槽读取实例及双版本并向服务器发起卸装请求。
void UEquipmentComponent::RequestUnequip(FGameplayTag Tag)
{
    const auto* Bag = Backpack();
    const auto* Slot = Slots.FindByPredicate(
        [&](const FEquipmentSlot& S)
        {
            return S.SlotTag == Tag;
        });
    if (Bag && Slot)
        ServerUnequip(Tag, Slot->ItemInstanceId, Bag->GetRevision(), Revision);
}
// 执行服务器装备事务并向请求客户端返回结果。
// 网络入口：拥有此PlayerState组件的客户端发起可靠请求；服务端校验双版本和实例。ClientResult只回执结果，ForceNetUpdate只请求尽早复制，不提供跨组件原子到达保证。
void UEquipmentComponent::ServerEquip_Implementation(int32 Index, FGameplayTag Tag, FGuid Id, int32 BagRevision,
                                                     int32 EquipmentRevision)
{
    ClientResult(TryEquip(Index, Tag, Id, BagRevision, EquipmentRevision));
    GetOwner()->ForceNetUpdate();
}
// 执行服务器卸装事务并向请求客户端返回结果。
void UEquipmentComponent::ServerUnequip_Implementation(FGameplayTag Tag, FGuid Id, int32 BagRevision,
                                                       int32 EquipmentRevision)
{
    ClientResult(TryUnequip(Tag, Id, BagRevision, EquipmentRevision));
    GetOwner()->ForceNetUpdate();
}
// 将服务器事务结果广播给本地请求监听者。
// 接收端只广播操作结果；UI仍应以OnRep_State/背包复制后的数据刷新，不能在此自行扣除物品或生成GE。
void UEquipmentComponent::ClientResult_Implementation(bool bSucceeded)
{
    OnRequestResult.Broadcast(bSucceeded);
}
// 装备复制更新后刷新武器外观和装备界面。
// 客户端由复制系统调用；服务器提交路径也直接调用。外观刷新可以早于新Pawn准备好，后续ActorInfo/角色初始化还会补刷新。
void UEquipmentComponent::OnRep_State()
{
    RefreshWeaponVisuals();
    OnEquipmentChanged.Broadcast();
}
// 服务器结束组件生命周期时撤销武器能力及本组件装备效果。
// 清理只作用于本组件的服务器句柄。推进武器代次使旧弹体归属失效；然后撤销能力并移除GE，最后进入父类生命周期。
void UEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetOwner()->HasAuthority())
    {
        ++WeaponGeneration;
        CommitWeaponSet({}, {});
        ClearEffects();
    }
    Super::EndPlay(Reason);
}
// 登记组件业务状态的网络复制字段。
void UEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    // 登记组件业务状态的网络复制字段。
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UEquipmentComponent, Slots);
    DOREPLIFETIME(UEquipmentComponent, Revision);
    DOREPLIFETIME(UEquipmentComponent, bNeedsReconcile);
}
