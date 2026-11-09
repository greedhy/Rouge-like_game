// 调用链：死亡/清房排队 → Tick 准备随机实例 → 延迟生成目录拾取类 → 原有背包/装备通路。
#include "Equipment/SharedLootSubsystem.h"
#include "Equipment/EquipmentGenerator.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "Backpack/ItemDefinition.h"
#include "Backpack/WorldPickupComponent.h"
#include "Character/EnemyCharacter.h"
#include "Engine/World.h"

// 默认资产路径只用于全局分类与目录；可在配置文件覆盖，不修改已有蓝图数值。
USharedLootSettings::USharedLootSettings()
{
    ItemCatalog = FSoftObjectPath(TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    EliteClasses = {
        FSoftClassPath(TEXT("/Game/BluePrints/Characters/Goblin/BP_Shaman.BP_Shaman_C")),
        FSoftClassPath(
            TEXT("/Game/BluePrints/Characters/Goblin/ShamanExpansion/BP_ShamanExpanded.BP_ShamanExpanded_C")),
        FSoftClassPath(TEXT("/Game/BluePrints/Characters/Ghoul/BP_Ghoul.BP_Ghoul_C")),
        FSoftClassPath(TEXT("/Game/BluePrints/Characters/LegacyExpansion/BP_Ghoul.BP_Ghoul_C")),
        FSoftClassPath(TEXT("/Game/BluePrints/Characters/StoneGolem/BP_StoneGolemElite.BP_StoneGolemElite_C"))};
    BossClasses = {FSoftClassPath(TEXT("/Game/BluePrints/Characters/Grux/Expansion/BP_GruxBoss.BP_GruxBoss_C"))};
}

// 比较已加载类的继承链路径，不为识别一个敌人加载全部怪物资源。
EEnemyLootRank USharedLootSettings::ResolveRank(const UClass* EnemyClass, EEnemyLootRank Override) const
{
    if (Override == EEnemyLootRank::None || Override == EEnemyLootRank::Boss)
    {
        return Override;
    }
    bool bElite = Override == EEnemyLootRank::Elite;
    for (const UClass* Class = EnemyClass; Class; Class = Class->GetSuperClass())
    {
        const FSoftClassPath Path(Class);
        if (BossClasses.Contains(Path))
        {
            return EEnemyLootRank::Boss;
        }
        bElite |= EliteClasses.Contains(Path);
    }
    return bElite ? EEnemyLootRank::Elite : EEnemyLootRank::Normal;
}

// 房间等级按入口的图深度增长；使用 int64 避免错误配置溢出。
int32 USharedLootSettings::RoomLevel(int32 Depth) const
{
    return static_cast<int32>(FMath::Clamp<int64>(
        int64(BaseRoomLevel) + int64(FMath::Max(0, Depth)) * FMath::Max(0, RoomLevelStep), 1, 10000));
}

// 普通怪等于房间等级，精英和 Boss 使用统一增量。
int32 USharedLootSettings::MonsterLevel(int32 Level, EEnemyLootRank Rank) const
{
    const int32 Bonus = Rank == EEnemyLootRank::Boss    ? BossLevelBonus
                        : Rank == EEnemyLootRank::Elite ? EliteLevelBonus
                                                        : 0;
    return static_cast<int32>(FMath::Clamp<int64>(int64(Level) + Bonus, 1, 10000));
}

// 普通怪独立两次概率；精英/Boss 的消耗品总数先抽取，再等概率选择红蓝药水。
FSharedLootCounts FSharedLootCounts::Roll(const USharedLootSettings& Rules, EEnemyLootRank Rank, FRandomStream& Random)
{
    FSharedLootCounts Result;
    if (Rank == EEnemyLootRank::Normal)
    {
        Result.HealthPotions = Random.GetFraction() < FMath::Clamp(Rules.HealthPotionChance, 0.f, 1.f);
        Result.ManaPotions = Random.GetFraction() < FMath::Clamp(Rules.ManaPotionChance, 0.f, 1.f);
    }
    else if (Rank == EEnemyLootRank::Elite || Rank == EEnemyLootRank::Boss)
    {
        const bool bBoss = Rank == EEnemyLootRank::Boss;
        const int32 Min = FMath::Clamp(bBoss ? Rules.BossConsumableMin : Rules.EliteConsumableMin, 0, 100);
        const int32 Max = FMath::Clamp(bBoss ? Rules.BossConsumableMax : Rules.EliteConsumableMax, Min, 100);
        const int32 Count = Random.RandRange(Min, Max);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (Random.RandRange(0, 1) == 0)
            {
                ++Result.HealthPotions;
            }
            else
            {
                ++Result.ManaPotions;
            }
        }
        Result.Equipment = FMath::Clamp(bBoss ? Rules.BossEquipmentCount : Rules.EliteEquipmentCount, 0, 100);
    }
    return Result;
}

bool USharedLootSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

TStatId USharedLootSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USharedLootSubsystem, STATGROUP_Tickables);
}

// 死亡位置与等级立即快照，不保存可能被对象池复用的敌人指针。
void USharedLootSubsystem::QueueEnemyLoot(const AEnemyCharacter& Enemy)
{
    if (GetWorld()->GetNetMode() == NM_Client || !Enemy.HasAuthority())
    {
        return;
    }
    FRandomStream Random(FMath::Rand());
    Enqueue(FSharedLootCounts::Roll(*GetDefault<USharedLootSettings>(), Enemy.GetLootRank(), Random), Enemy.Level,
            Enemy.GetActorLocation());
}

// 房间账本与死亡账本分开，因此 Boss 掉落和 Boss 房清理奖励可以同时发放。
void USharedLootSubsystem::QueueRoomLoot(const FGuid& RunId, FName RoomId, int32 Level, const FVector& Location)
{
    if (GetWorld()->GetNetMode() == NM_Client || !RunId.IsValid() || RoomId.IsNone())
    {
        return;
    }
    const FString Key = RunId.ToString() + TEXT(":") + RoomId.ToString();
    if (RewardedRooms.Contains(Key))
    {
        return;
    }
    RewardedRooms.Add(Key);
    FSharedLootCounts Counts;
    Counts.Equipment = FMath::Clamp(GetDefault<USharedLootSettings>()->RoomEquipmentCount, 0, 100);
    Enqueue(Counts, Level, Location);
}

// 每件物品独立排队；随机种子及物品等级在请求时固定，失败不会增加奖励数量。
void USharedLootSubsystem::Enqueue(const FSharedLootCounts& Counts, int32 Level, const FVector& Location)
{
    const int32 Count = Counts.HealthPotions + Counts.ManaPotions + Counts.Equipment;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        auto& Item = Pending.AddDefaulted_GetRef();
        Item.bEquipment = Index >= Counts.HealthPotions + Counts.ManaPotions;
        if (!Item.bEquipment)
        {
            Item.ItemId = FPrimaryAssetId(TEXT("BackpackItem"),
                                          Index < Counts.HealthPotions ? TEXT("HealthPotion") : TEXT("ManaPotion"));
        }
        Item.ItemLevel =
            FMath::Clamp(Level, 1, FMath::Clamp(GetDefault<USharedLootSettings>()->MaximumEquipmentLevel, 1, 10000));
        Item.Seed = FMath::Rand();
        const float Angle = 2.f * PI * Index / FMath::Max(1, Count);
        Item.Location = Location + FVector(FMath::Cos(Angle) * 100.f, FMath::Sin(Angle) * 100.f, 0.f);
    }
}

// 装备候选来自现有目录；所有实例均经同一生成器验证后才落地。
bool USharedLootSubsystem::SpawnOne(FPendingSharedLoot& Item)
{
    if (!Catalog)
    {
        Catalog = Cast<UBackpackItemDefinitions>(GetDefault<USharedLootSettings>()->ItemCatalog.TryLoad());
    }
    if (!Catalog)
    {
        return false;
    }
    if (Item.bEquipment && !Item.ItemId.IsValid())
    {
        TArray<FPrimaryAssetId> Choices;
        auto AddChoice = [&Choices](const FBackpackItemDefEntry& Def)
        {
            if (Def.bEquippable && Def.bRandomizedEquipment && Def.ItemDefinitionId.IsValid())
            {
                Choices.AddUnique(Def.ItemDefinitionId);
            }
        };
        for (const auto& Def : Catalog->Items)
        {
            AddChoice(Def);
        }
        if (Catalog->EquipmentTable)
        {
            TArray<FBackpackItemDefEntry*> Rows;
            Catalog->EquipmentTable->GetAllRows(TEXT("SharedLoot"), Rows);
            for (const auto* Row : Rows)
            {
                AddChoice(*Row);
            }
        }
        Choices.Sort(
            [](const FPrimaryAssetId& A, const FPrimaryAssetId& B)
            {
                return A.ToString() < B.ToString();
            });
        if (Choices.IsEmpty())
        {
            return false;
        }
        FRandomStream Random(Item.Seed);
        Item.ItemId = Choices[Random.RandRange(0, Choices.Num() - 1)];
    }
    const auto* Def = Catalog->FindDefinition(Item.ItemId);
    if (!Def)
    {
        return false;
    }
    if (Item.bEquipment && Item.Roll.GenerationVersion == 0)
    {
        FString Error;
        if (!FEquipmentGenerator::Generate(*Catalog, *Def, Item.ItemLevel, Item.Seed, Item.Roll, Error, true))
        {
            return false;
        }
    }
    UClass* PickupClass = Item.bEquipment ? AEquipmentSoulPickup::StaticClass() : Def->PickupActorClass.Get();
    if (!PickupClass)
    {
        return false;
    }
    FVector Location = Item.Location;
    FHitResult Ground;
    // 从敌人脚下向下找地面；Actor 死亡时胶囊中心高度不能直接作为药水落点。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SharedLootGround));
    Query.bTraceComplex = false;
    if (GetWorld()->LineTraceSingleByObjectType(Ground, Location + FVector(0, 0, 50), Location - FVector(0, 0, 2000),
                                                FCollisionObjectQueryParams(ECC_WorldStatic), Query))
    {
        Location.Z = Ground.ImpactPoint.Z + 15.f;
    }
    const FTransform Transform(FRotator::ZeroRotator, Location);
    auto* Actor = GetWorld()->SpawnActorDeferred<AActor>(PickupClass, Transform, nullptr, nullptr,
                                                         ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Actor)
    {
        return false;
    }
    // 药水的蓝图 SCS 组件在 FinishSpawning 才出现；先关闭碰撞以免默认定义被自动拾取。
    Actor->SetActorEnableCollision(false);
    if (auto* Pickup = Actor->FindComponentByClass<UWorldPickupComponent>())
    {
        Pickup->ItemDefinitions = Catalog;
        Pickup->ConfigurePickup(Item.ItemId, 1, Item.bEquipment, Item.bEquipment ? &Item.Roll : nullptr);
    }
    Actor->FinishSpawning(Transform);
    if (!IsValid(Actor))
    {
        return false;
    }
    auto* Pickup = Actor->FindComponentByClass<UWorldPickupComponent>();
    if (!Pickup)
    {
        Actor->Destroy();
        return false;
    }
    Pickup->ItemDefinitions = Catalog;
    if (Pickup->ItemDefinitionId != Item.ItemId || !Item.bEquipment)
    {
        Pickup->ConfigurePickup(Item.ItemId, 1, Item.bEquipment, Item.bEquipment ? &Item.Roll : nullptr);
    }
    UWorldPickupComponent::ConfigureOwnerForNetworkedPickup(Actor);
    Actor->SetActorEnableCollision(true);
    return true;
}

// 最多每帧处理八件；失败项每秒重试，成功项立即移除防止重复生成。
void USharedLootSubsystem::Tick(float DeltaTime)
{
    if (GetWorld()->GetNetMode() == NM_Client || Pending.IsEmpty())
    {
        return;
    }
    RetryDelay -= DeltaTime;
    if (RetryDelay > 0.f)
    {
        return;
    }
    int32 Budget = FMath::Min(8, Pending.Num());
    while (!Pending.IsEmpty() && Budget-- > 0)
    {
        // 环形游标防止一批配置错误的物品阻塞所有后续奖励。
        NextPendingIndex %= Pending.Num();
        const int32 Index = NextPendingIndex;
        if (SpawnOne(Pending[Index]))
        {
            Pending.RemoveAt(Index);
        }
        else
        {
            if (!Pending[Index].bReportedFailure)
            {
                UE_LOG(
                    LogTemp, Error,
                    TEXT("SharedLoot: reward pending, cannot spawn %s (level %d). Check catalog/affixes/pickup class."),
                    *Pending[Index].ItemId.ToString(), Pending[Index].ItemLevel);
                Pending[Index].bReportedFailure = true;
            }
            RetryDelay = 1.f;
            ++NextPendingIndex;
        }
    }
}

// 世界销毁时清理本局账本与待生成项；不把旧地图奖励投放到新世界。
void USharedLootSubsystem::Deinitialize()
{
    Pending.Reset();
    RewardedRooms.Reset();
    Catalog = nullptr;
    Super::Deinitialize();
}
