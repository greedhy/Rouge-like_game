/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 所有权与入口：构造函数CreateDefaultSubobject创建ASC、AttributeSet、Backpack和Equipment；组件随PlayerState存活，不随一次攻击创建。
 * 服务器PossessedBy初始化角色ActorInfo；客户端OnRep_PlayerState补建ActorInfo，Owner为PlayerState，Avatar为当前Pawn。
 * 同步：ASC启用Mixed效果复制；不能假设所有客户端都能遍历完整Active GE。成长字段通过DOREPLIFETIME复制，OnRep广播本地UI事件；服务器修改入口也负责本地通知。
 * 旅行：CopyProperties将背包装备快照和成长数据交给目标PlayerState，OverrideWith恢复旧状态；转移的是业务数据，不是旧世界中的GE句柄/组件指针。
 */
// 主要链路：玩家状态持有跨角色使用的能力系统、属性、背包和装备；属性变更广播驱动界面，联合快照用于旅行与恢复。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/MyPlayerState.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentGenerator.h"
#include <Net/UnrealNetwork.h>

#include "AbilitySystem/MyAttributeSet.h"
#include "Game/LoadScreenSaveGame.h"
// 创建可复制能力系统、属性集、背包与装备组件。
AMyPlayerState::AMyPlayerState()
{
    //NetUpdateFrequency = 100.f;
    SetNetUpdateFrequency(100.f);

    AbilitySystemComponent = CreateDefaultSubobject<UMyAbilitySystemComponent>("AbilitySystemComponent");
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    AttributeSet = CreateDefaultSubobject<UMyAttributeSet>("AttributeSet");
    BackpackComponent = CreateDefaultSubobject<UBackpackComponent>("BackpackComponent");
    EquipmentComponent = CreateDefaultSubobject<UEquipmentComponent>("EquipmentComponent");
}

// 设置物品目录，服务器初始化背包。
void AMyPlayerState::BeginPlay()
{
    Super::BeginPlay();
    BackpackComponent->SetItemDefinitions(BackpackItemDefinitions);

    if (HasAuthority())
    {
        BackpackComponent->Initialize();
    }
}

// 客户端玩家状态初始化时建立背包本地状态。
// 客户端PlayerState获得控制器时补初始化背包；复制和控制器就绪先后不固定，Initialize的幂等性用于处理这类时序。
void AMyPlayerState::ClientInitialize(AController* C)
{
    Super::ClientInitialize(C);
    BackpackComponent->Initialize();
}

// 注册本类需要复制的状态，同时保留父类复制字段。
void AMyPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMyPlayerState, Level);
    DOREPLIFETIME(AMyPlayerState, XP);
    DOREPLIFETIME(AMyPlayerState, AttributePoints);
    DOREPLIFETIME(AMyPlayerState, SpellPoints);
    DOREPLIFETIME(AMyPlayerState, TravelPlayerSlot);
}

// 返回当前角色或玩家状态持有的能力系统。
UAbilitySystemComponent* AMyPlayerState::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

// 累加经验并通过玩家状态通知订阅者。
void AMyPlayerState::AddToXP(int32 InXP)
{
    XP += InXP;
    OnXPChangedDelegate.Broadcast(XP);
}

// 累加等级并广播升级通知。
void AMyPlayerState::AddToLevel(int32 InLevel)
{
    Level += InLevel;
    OnLevelChangedDelegate.Broadcast(Level, true);
}

// 覆盖经验值并通知订阅者。
void AMyPlayerState::SetXP(int32 InXP)
{
    XP = InXP;
    OnXPChangedDelegate.Broadcast(XP);
}

// 覆盖等级并广播非升级式刷新。
void AMyPlayerState::SetLevel(int32 InLevel)
{
    Level = InLevel;
    OnLevelChangedDelegate.Broadcast(Level, false);
}

// 覆盖属性点并广播变化。
void AMyPlayerState::SetAttributePoints(int32 InPoints)
{
    AttributePoints = InPoints;
    OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

// 覆盖技能点并广播变化。
void AMyPlayerState::SetSpellPoints(int32 InPoints)
{
    SpellPoints = InPoints;
    OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}

// 复制等级到达后通知本地界面。
void AMyPlayerState::OnRep_Level(int32 OldLevel)
{
    OnLevelChangedDelegate.Broadcast(Level, true);
}

// 复制经验到达后通知本地界面。
void AMyPlayerState::OnRep_XP(int32 OldXP)
{
    OnXPChangedDelegate.Broadcast(XP);
}

// 复制属性点到达后通知本地界面。
void AMyPlayerState::OnRep_AttributePoints(int32 OldAttributePoints)
{
    OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

// 复制技能点到达后通知本地界面。
void AMyPlayerState::OnRep_SpellPoints(int32 OldSpellPoints)
{
    OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}

// 累加属性点并通知订阅者。
void AMyPlayerState::AddToAttributePoints(int32 InPoints)
{
    AttributePoints += InPoints;
    OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

// 累加技能点并通知订阅者。
void AMyPlayerState::AddToSpellPoints(int32 InPoints)
{
    SpellPoints += InPoints;
    OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}

// 一并导出背包、装备、版本与物品实例词条，保持恢复数据对应。
FBackpackSnapshot AMyPlayerState::ExportBackpackSnapshot() const
{
    FBackpackSnapshot Snapshot;
    if (!IsValid(BackpackComponent))
    {
        return Snapshot;
    }

    Snapshot.Equipment = EquipmentComponent->Slots;
    Snapshot.EquipmentRevision = EquipmentComponent->Revision;
    Snapshot.Capacity = BackpackComponent->GetCapacity();
    Snapshot.Revision = BackpackComponent->GetRevision();
    const TArray<FReplicatedInventorySlot>& ReplicatedSlots = BackpackComponent->GetReplicatedSlots().Items;
    Snapshot.Slots.SetNum(ReplicatedSlots.Num());
    for (int32 SlotIndex = 0; SlotIndex < ReplicatedSlots.Num(); ++SlotIndex)
    {
        Snapshot.Slots[SlotIndex].ItemDefinitionId = ReplicatedSlots[SlotIndex].ItemDefinitionId;
        Snapshot.Slots[SlotIndex].Quantity = ReplicatedSlots[SlotIndex].Quantity;
        Snapshot.Slots[SlotIndex].ItemInstanceId = ReplicatedSlots[SlotIndex].ItemInstanceId;
        Snapshot.Slots[SlotIndex].RollData = ReplicatedSlots[SlotIndex].RollData;
    }
    return Snapshot;
}

// 服务器先校验容量、装备、实例唯一性与随机词条，再交给装备组件联合恢复。
bool AMyPlayerState::ImportBackpackSnapshot(const FBackpackSnapshot& Snapshot)
{
    if (!HasAuthority() || !IsValid(BackpackComponent) || Snapshot.Capacity < 0 ||
        Snapshot.Slots.Num() != Snapshot.Capacity)
    {
        return false;
    }

    if (!BackpackComponent->ItemDefinitions && BackpackItemDefinitions)
        BackpackComponent->SetItemDefinitions(BackpackItemDefinitions);
    if (!EquipmentComponent->ValidateRecords(Snapshot.Equipment))
        return false;
    // 装备与背包共用实例去重集合，禁止同一物品同时出现在两个位置。
    TSet<FGuid> Seen;
    for (const auto& S : Snapshot.Equipment)
        Seen.Add(S.ItemInstanceId);
    TArray<FReplicatedInventorySlot> ReplicatedSlots;
    ReplicatedSlots.SetNum(Snapshot.Slots.Num());
    for (int32 SlotIndex = 0; SlotIndex < Snapshot.Slots.Num(); ++SlotIndex)
    {
        FReplicatedInventorySlot& ReplicatedSlot = ReplicatedSlots[SlotIndex];
        const FBackpackSnapshotSlot& SavedSlot = Snapshot.Slots[SlotIndex];
        if (!FEquipmentGenerator::ValidateRoll(SavedSlot.RollData) ||
            (SavedSlot.IsEmpty() && SavedSlot.RollData.GenerationVersion != 0))
            return false;
        if (SavedSlot.Quantity < 0 || (SavedSlot.ItemDefinitionId.IsValid() != (SavedSlot.Quantity > 0)))
            return false;
        if (!SavedSlot.IsEmpty())
        {
            ReplicatedSlot.ItemDefinitionId = SavedSlot.ItemDefinitionId;
            ReplicatedSlot.Quantity = SavedSlot.Quantity;
            ReplicatedSlot.RollData = SavedSlot.RollData;
            ReplicatedSlot.ItemInstanceId =
                SavedSlot.ItemInstanceId.IsValid() ? SavedSlot.ItemInstanceId : FGuid::NewGuid();
            if (Seen.Contains(ReplicatedSlot.ItemInstanceId))
                return false;
            Seen.Add(ReplicatedSlot.ItemInstanceId);
            const auto* Def = BackpackComponent->ItemDefinitions
                                  ? BackpackComponent->ItemDefinitions->FindDefinition(SavedSlot.ItemDefinitionId)
                                  : nullptr;
            if (!FEquipmentGenerator::ValidateRoll(SavedSlot.RollData))
                return false;
            if (SavedSlot.RollData.GenerationVersion > 0 &&
                (!Def || !Def->bRandomizedEquipment || SavedSlot.RollData.ProfileId != Def->GenerationProfile))
                return false;
            if (Def && Def->bRandomizedEquipment && SavedSlot.RollData.GenerationVersion == 0)
                return false;
            if (Def && Def->bEquippable && (SavedSlot.Quantity != 1 || Def->bStackable || Def->MaxStack != 1))
                return false;
        }
    }
    // 所有字段校验通过后统一恢复，避免先改背包再发现装备无效。
    return EquipmentComponent->RestoreCombined(ReplicatedSlots, Snapshot.Capacity, Snapshot.Revision,
                                               Snapshot.Equipment, Snapshot.EquipmentRevision);
}

// 记录背包持久化使用的玩家标识。
void AMyPlayerState::SetBackpackPersistencePlayerId(const FString& InPlayerId)
{
    BackpackPersistencePlayerId = InPlayerId;
}

// 向新玩家状态转移背包装备快照、旅行资源与成长数据。
// 旅行框架建立目标PlayerState时拷贝业务快照；目标组件属于新Owner，需要按新Avatar/ASC重建运行效果，不能复用源GE句柄。
void AMyPlayerState::CopyProperties(APlayerState* PlayerState)
{
    Super::CopyProperties(PlayerState);
    if (AMyPlayerState* DestinationState = Cast<AMyPlayerState>(PlayerState))
    {
        DestinationState->SetBackpackPersistencePlayerId(BackpackPersistencePlayerId);
        DestinationState->ImportBackpackSnapshot(ExportBackpackSnapshot());
        DestinationState->TravelPlayerSlot = TravelPlayerSlot;
        DestinationState->TravelProgress = TravelProgress;
        DestinationState->TravelHealth = TravelHealth;
        DestinationState->TravelMana = TravelMana;
        DestinationState->Level = Level;
        DestinationState->XP = XP;
        DestinationState->AttributePoints = AttributePoints;
        DestinationState->SpellPoints = SpellPoints;
    }
}

// 从旧玩家状态恢复持久化标识和背包装备联合快照。
void AMyPlayerState::OverrideWith(APlayerState* PlayerState)
{
    Super::OverrideWith(PlayerState);
    if (const AMyPlayerState* SourceState = Cast<AMyPlayerState>(PlayerState))
    {
        SetBackpackPersistencePlayerId(SourceState->GetBackpackPersistencePlayerId());
        ImportBackpackSnapshot(SourceState->ExportBackpackSnapshot());
    }
}

// 暂存跨地图进度及当前生命、法力。
void AMyPlayerState::SetTravelProgress(ULoadScreenSaveGame* Progress, float Health, float Mana)
{
    TravelProgress = Progress;
    TravelHealth = Health;
    TravelMana = Mana;
}
