#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Roguelike/DungeonEliteAffix.h"
#include "DungeonRunProfile.generated.h"

class AEnemyCharacter;
class UPureMassMonsterType;
class UBackpackItemDefinitions;
enum class EDungeonRoomRole : uint8;

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonNormalEnemy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftClassPtr<AEnemyCharacter> EnemyClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
    float Health = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bPoolEligible = true;
};

USTRUCT(BlueprintType)
struct TEST_25D_API FDungeonShopOffer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FPrimaryAssetId ItemId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText Name;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Price = 30;
};

UCLASS(BlueprintType)
class TEST_25D_API UDungeonRunProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    // Empty preserves legacy NormalClass/EliteClass fallback. Nonempty is a balanced shuffle bag.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemies")
    TArray<FDungeonNormalEnemy> NormalRoster;
    bool ValidateNormalRoster(FString& Error) const;
    TArray<int32> BuildNormalComposition(int32 Seed, int32 Room, int32 Group, int32 Count) const;
    int32 NormalPoolQuota(UClass* Class) const;

    // 战斗房首波固定出现一只，第二波按概率补一只。专用精英房也抽取词缀。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite Affixes")
    bool bEnableEliteAffixes = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite Affixes", meta = (ClampMin = "0", ClampMax = "1"))
    float SecondWaveEliteChance = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite Affixes", meta = (ClampMin = "1", ClampMax = "1000000"))
    float CombatEliteHealth = 180.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite Affixes")
    TArray<FDungeonEliteAffixWeight> EliteAffixWeights = {
        {EDungeonEliteAffix::Fortified, 1},
        {EDungeonEliteAffix::Frenzied, 1},
        {EDungeonEliteAffix::Regenerating, 1},
        {EDungeonEliteAffix::Protective, 1}};

    // 导演提供本局种子，按房间和波次独立抽样。
    bool ValidateEliteAffixes(FString& Error) const;
    EDungeonEliteAffix RollEliteAffix(int32 Seed, int32 Room, EDungeonRoomRole Role, int32 Wave) const;

    // Includes active and idle common enemies; 0 disables pooling for A/B profiling.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool", meta = (ClampMin = "0", ClampMax = "64"))
    int32 NormalPoolCapacity = 12;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Pool", meta = (ClampMin = "0", ClampMax = "10"))
    float NormalCorpseSeconds = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance")
    bool bSamplePerformance = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.1"))
    float PerformanceSampleInterval = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1", ClampMax = "8"))
    int32 SpawnsPerFrame = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
    float SpawnBudgetMs = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
    float EnemyLoadTimeout = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass Melee")
    bool bUseMassMelee = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass Melee", meta = (ClampMin = "1", ClampMax = "50"))
    int32 MassPerCombatRoom = 50;
    int32 MassCountForWave(bool bTwoWaves, int32 Wave) const;
    int32 EncounterCapacity() const
    {
        return 30 + (bUseMassMelee ? FMath::Clamp(MassPerCombatRoom, 1, 50) : 0);
    }
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftClassPtr<AEnemyCharacter> NormalClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float NormalHealth = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UPureMassMonsterType> MonsterType;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftClassPtr<AEnemyCharacter> EliteClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftClassPtr<AEnemyCharacter> BossClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UBackpackItemDefinitions> ItemDefinitions;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FPrimaryAssetId> ChestItems;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDungeonShopOffer> Offers;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 CombatCoins = 15;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 EliteCoins = 30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 BossCoins = 50;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float EliteHealth = 450.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BossHealth = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BossPulseDamage = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BossPulseRadius = 550.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ReviveFraction = .3f;
};
