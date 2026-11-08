// 调用链：服务器持久化调用提供者 → 玩家身份校验与会话互斥 → 本地存档读写；联合快照保存背包与装备记录。
// 调用链：背包持久化调用 Load/Save → 校验玩家身份与槽位结构 → UE SaveGame 本地读写；会话锁仅在当前服务器进程内互斥。
#include "Backpack/BackpackPersistence.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeLock.h"

FCriticalSection FServerLocalBackpackPersistenceProvider::SessionLockGuard;
TSet<FString> FServerLocalBackpackPersistenceProvider::ActiveSessionLocks;

// 读取玩家专属存档；失败时输出保持空快照，避免调用方复用上次数据。
// 按玩家身份读取快照，失败时不沿用调用方旧快照。
bool FServerLocalBackpackPersistenceProvider::Load(const FString& PlayerId, FBackpackSnapshot& OutSnapshot) const
{
    OutSnapshot = FBackpackSnapshot();
    if (!IsValidPlayerId(PlayerId) || !UGameplayStatics::DoesSaveGameExist(MakeSaveSlotName(PlayerId), 0))
        return false;
    const UBackpackPersistenceSaveGame* SaveGame =
        Cast<UBackpackPersistenceSaveGame>(UGameplayStatics::LoadGameFromSlot(MakeSaveSlotName(PlayerId), 0));
    if (!IsValid(SaveGame) || SaveGame->PlayerId != PlayerId)
        return false;
    OutSnapshot = SaveGame->Snapshot;
    return true;
}

// 校验快照容量后创建独立 SaveGame，并将玩家身份随快照一并写入。
// 校验身份及容量结构后持久化联合快照。
bool FServerLocalBackpackPersistenceProvider::Save(const FString& PlayerId, const FBackpackSnapshot& Snapshot)
{
    if (!IsValidPlayerId(PlayerId) || Snapshot.Capacity < 0 || Snapshot.Slots.Num() != Snapshot.Capacity)
        return false;
    UBackpackPersistenceSaveGame* SaveGame = Cast<UBackpackPersistenceSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UBackpackPersistenceSaveGame::StaticClass()));
    if (!IsValid(SaveGame))
        return false;
    SaveGame->PlayerId = PlayerId;
    SaveGame->Snapshot = Snapshot;
    return UGameplayStatics::SaveGameToSlot(SaveGame, MakeSaveSlotName(PlayerId), 0);
}

// 在临界区内登记玩家会话；同一玩家已有持锁会话时拒绝重复获取。
// 获取当前服务器进程内的玩家会话互斥登记。
bool FServerLocalBackpackPersistenceProvider::AcquireSessionLock(const FString& PlayerId)
{
    if (!IsValidPlayerId(PlayerId))
        return false;
    FScopeLock Lock(&SessionLockGuard);
    if (ActiveSessionLocks.Contains(PlayerId))
        return false;
    ActiveSessionLocks.Add(PlayerId);
    return true;
}

// 在临界区内移除玩家登记，允许后续会话重新获取。
// 释放玩家会话登记，允许后续会话接管。
void FServerLocalBackpackPersistenceProvider::ReleaseSessionLock(const FString& PlayerId)
{
    if (!IsValidPlayerId(PlayerId))
        return;
    FScopeLock Lock(&SessionLockGuard);
    ActiveSessionLocks.Remove(PlayerId);
}

// 拒绝空、过长及含控制字符的身份字符串。
// 拒绝空、过长或包含控制字符的玩家身份。
bool FServerLocalBackpackPersistenceProvider::IsValidPlayerId(const FString& PlayerId)
{
    if (PlayerId.IsEmpty() || PlayerId.Len() > 128)
        return false;
    for (const TCHAR Character : PlayerId)
    {
        if (Character < 0x20 || Character == 0x7f)
            return false;
    }
    return true;
}

// 将身份逐字符编码为十六进制，生成不包含原始路径字符的存档槽名。
// 将玩家身份编码为十六进制存档槽名，避免原始路径字符。
FString FServerLocalBackpackPersistenceProvider::MakeSaveSlotName(const FString& PlayerId)
{
    FString SaveSlotName(TEXT("Backpack_"));
    for (const TCHAR Character : PlayerId)
        SaveSlotName += FString::Printf(TEXT("%04X"), static_cast<uint32>(Character));
    return SaveSlotName;
}
