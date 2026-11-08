// 调用链：服务器持久化调用提供者 → 玩家身份校验与会话互斥 → 本地存档读写；联合快照保存背包与装备记录。
#pragma once

#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
#include "GameFramework/SaveGame.h"
#include "Equipment/EquipmentComponent.h"
#include "BackpackPersistence.generated.h"

USTRUCT(BlueprintType)
struct FBackpackSnapshotSlot
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, BlueprintReadOnly)
    FPrimaryAssetId ItemDefinitionId;

    UPROPERTY(SaveGame, BlueprintReadOnly)
    FGuid ItemInstanceId;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FEquipmentRollData RollData;

    UPROPERTY(SaveGame, BlueprintReadOnly)
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
struct FBackpackSnapshot
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, BlueprintReadOnly)
    TArray<FBackpackSnapshotSlot> Slots;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    TArray<FEquipmentSlot> Equipment;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 EquipmentRevision = 0;

    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Capacity = 0;

    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Revision = 0;
};

UCLASS()
class TEST_25D_API UBackpackPersistenceSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame)
    FString PlayerId;

    UPROPERTY(SaveGame)
    FBackpackSnapshot Snapshot;
};

class TEST_25D_API IBackpackPersistenceProvider
{
public:
    // 通过接口销毁具体存储提供者时执行正确的派生析构。
    virtual ~IBackpackPersistenceProvider() = default;
    // 按玩家身份读取快照，失败时不沿用调用方旧快照。
    virtual bool Load(const FString& PlayerId, FBackpackSnapshot& OutSnapshot) const = 0;
    // 校验身份及容量结构后持久化联合快照。
    virtual bool Save(const FString& PlayerId, const FBackpackSnapshot& Snapshot) = 0;
    // 获取当前服务器进程内的玩家会话互斥登记。
    virtual bool AcquireSessionLock(const FString& PlayerId) = 0;
    // 释放玩家会话登记，允许后续会话接管。
    virtual void ReleaseSessionLock(const FString& PlayerId) = 0;
};

class TEST_25D_API FServerLocalBackpackPersistenceProvider final : public IBackpackPersistenceProvider
{
public:
    // 按玩家身份读取快照，失败时不沿用调用方旧快照。
    virtual bool Load(const FString& PlayerId, FBackpackSnapshot& OutSnapshot) const override;
    // 校验身份及容量结构后持久化联合快照。
    virtual bool Save(const FString& PlayerId, const FBackpackSnapshot& Snapshot) override;
    // 获取当前服务器进程内的玩家会话互斥登记。
    virtual bool AcquireSessionLock(const FString& PlayerId) override;
    // 释放玩家会话登记，允许后续会话接管。
    virtual void ReleaseSessionLock(const FString& PlayerId) override;

    // 拒绝空、过长或包含控制字符的玩家身份。
    static bool IsValidPlayerId(const FString& PlayerId);

private:
    // 将玩家身份编码为十六进制存档槽名，避免原始路径字符。
    static FString MakeSaveSlotName(const FString& PlayerId);

    static FCriticalSection SessionLockGuard;
    static TSet<FString> ActiveSessionLocks;
};
