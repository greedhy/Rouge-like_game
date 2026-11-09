// 调用链：EnemyCharacter::Die / DungeonRunDirector::CompleteEncounter → 共享规则 → 队列 → WorldPickupComponent。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Equipment/EquipmentRollTypes.h"
#include "SharedLootSubsystem.generated.h"

class AEnemyCharacter;
class UBackpackItemDefinitions;

UENUM(BlueprintType)
enum class EEnemyLootRank : uint8
{
    Normal,
    Elite,
    Boss,
    None
};

/** 全模式读取同一份 DefaultGame.ini 配置，不从 GameMode 复制规则。 */
UCLASS(Config = Game, DefaultConfig)
class TEST_25D_API USharedLootSettings : public UObject
{
    GENERATED_BODY()
public:
    USharedLootSettings();
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    FSoftObjectPath ItemCatalog;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    float HealthPotionChance = .1f;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    float ManaPotionChance = .1f;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 EliteConsumableMin = 1;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 EliteConsumableMax = 3;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 EliteEquipmentCount = 1;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 BossConsumableMin = 2;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 BossConsumableMax = 4;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 BossEquipmentCount = 2;
    UPROPERTY(Config, EditAnywhere, Category = "Loot")
    int32 RoomEquipmentCount = 2;
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    int32 BaseRoomLevel = 1;
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    int32 RoomLevelStep = 1;
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    int32 EliteLevelBonus = 1;
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    int32 BossLevelBonus = 2;
    // 现有词缀表覆盖 1–20；扩展词缀层级后可同步提高上限。
    UPROPERTY(Config, EditAnywhere, Category = "Levels")
    int32 MaximumEquipmentLevel = 20;
    UPROPERTY(Config, EditAnywhere, Category = "Ranks")
    TArray<FSoftClassPath> EliteClasses;
    UPROPERTY(Config, EditAnywhere, Category = "Ranks")
    TArray<FSoftClassPath> BossClasses;

    /** 基于实际类继承链识别身份；None 显式禁止掉落，Boss 优先于 Elite。 */
    EEnemyLootRank ResolveRank(const UClass* EnemyClass, EEnemyLootRank Override) const;
    int32 RoomLevel(int32 Depth) const;
    int32 MonsterLevel(int32 RoomLevel, EEnemyLootRank Rank) const;
};

/** 纯规则结果可由固定种子测试，不创建 Actor。 */
struct TEST_25D_API FSharedLootCounts
{
    int32 HealthPotions = 0;
    int32 ManaPotions = 0;
    int32 Equipment = 0;
    static FSharedLootCounts Roll(const USharedLootSettings& Rules, EEnemyLootRank Rank, FRandomStream& Random);
};

USTRUCT()
struct FPendingSharedLoot
{
    GENERATED_BODY()
    UPROPERTY()
    FPrimaryAssetId ItemId;
    UPROPERTY()
    FEquipmentRollData Roll;
    FVector Location = FVector::ZeroVector;
    int32 ItemLevel = 1;
    int32 Seed = 0;
    bool bEquipment = false;
    bool bReportedFailure = false;
};

/** 服务器世界持有未落地奖励；失败重试只处理剩余项，不重抽已准备的装备。 */
UCLASS()
class TEST_25D_API USharedLootSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    /** 仅死亡入口调用；每条生命的去重由 EnemyCharacter 的 bDead 保护。 */
    void QueueEnemyLoot(const AEnemyCharacter& Enemy);
    /** 同一局的同一房间只能领取一次队伍共享奖励。 */
    void QueueRoomLoot(const FGuid& RunId, FName RoomId, int32 Level, const FVector& Location);
    int32 GetPendingCount() const
    {
        return Pending.Num();
    }

private:
    void Enqueue(const FSharedLootCounts& Counts, int32 Level, const FVector& Location);
    bool SpawnOne(FPendingSharedLoot& Item);
    UPROPERTY(Transient)
    TObjectPtr<UBackpackItemDefinitions> Catalog;
    UPROPERTY(Transient)
    TArray<FPendingSharedLoot> Pending;
    TSet<FString> RewardedRooms;
    float RetryDelay = 0.f;
    int32 NextPendingIndex = 0;
};
