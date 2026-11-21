// 调用链：导演初始化校验 Profile，生成每波名单时读取确定性的普通怪与精英选择。
#include "Roguelike/DungeonRunProfile.h"
#include "Character/EnemyCharacter.h"
#include "Roguelike/DungeonGenerationTypes.h"

// 拒绝无法安全抽样的配置，让错误在开局时报告。
bool UDungeonRunProfile::ValidateEliteAffixes(FString& Error) const
{
    Error.Reset();
    if (!bEnableEliteAffixes)
    {
        return true;
    }
    if (!FMath::IsFinite(CombatEliteHealth) || CombatEliteHealth < 1.f || CombatEliteHealth > 1000000.f ||
        !FMath::IsFinite(SecondWaveEliteChance) || SecondWaveEliteChance < 0.f || SecondWaveEliteChance > 1.f ||
        EliteAffixWeights.IsEmpty())
    {
        Error = TEXT("InvalidEliteAffixSettings");
        return false;
    }
    TSet<EDungeonEliteAffix> Seen;
    for (const auto& Entry : EliteAffixWeights)
    {
        if (Entry.Affix <= EDungeonEliteAffix::None || Entry.Affix > EDungeonEliteAffix::Protective ||
            Entry.Weight < 1 || Entry.Weight > 100 || Seen.Contains(Entry.Affix))
        {
            Error = TEXT("InvalidEliteAffixWeight");
            return false;
        }
        Seen.Add(Entry.Affix);
    }
    return true;
}

// 使用与普通名单独立的随机流，队列拆帧和其它抽样不会影响精英词缀。
EDungeonEliteAffix UDungeonRunProfile::RollEliteAffix(int32 Seed, int32 Room, EDungeonRoomRole Role, int32 Wave) const
{
    const bool bCombatWave = Role == EDungeonRoomRole::Combat && (Wave == 1 || Wave == 2);
    const bool bEliteWave = Role == EDungeonRoomRole::Elite && Wave == 1;
    if (!bEnableEliteAffixes || (!bCombatWave && !bEliteWave))
    {
        return EDungeonEliteAffix::None;
    }

    FRandomStream Stream(HashCombineFast(uint32(Seed),
                                         HashCombineFast(0x454C4954u, HashCombineFast(uint32(Room), uint32(Wave)))));
    if (bCombatWave && Wave == 2 && Stream.GetFraction() >= SecondWaveEliteChance)
    {
        return EDungeonEliteAffix::None;
    }

    int32 TotalWeight = 0;
    for (const auto& Entry : EliteAffixWeights)
    {
        TotalWeight += Entry.Weight;
    }
    int32 Remaining = Stream.RandRange(1, TotalWeight);
    for (const auto& Entry : EliteAffixWeights)
    {
        Remaining -= Entry.Weight;
        if (Remaining <= 0)
        {
            return Entry.Affix;
        }
    }
    return EDungeonEliteAffix::None;
}

bool UDungeonRunProfile::ValidateNormalRoster(FString& Error) const
{
    Error.Reset();
    if (NormalRoster.Num() > 16)
    {
        Error = TEXT("NormalRosterTooLarge");
        return false;
    }
    TSet<FName> Ids;
    TSet<FSoftObjectPath> Classes;
    for (const auto& Entry : NormalRoster)
    {
        const auto Path = Entry.EnemyClass.ToSoftObjectPath();
        if (Entry.Id.IsNone() || Ids.Contains(Entry.Id) || !Path.IsValid() || Classes.Contains(Path) ||
            !FMath::IsFinite(Entry.Health) || Entry.Health <= 0)
        {
            Error = FString::Printf(TEXT("InvalidNormalRosterEntry:%s"), *Entry.Id.ToString());
            return false;
        }
        Ids.Add(Entry.Id);
        Classes.Add(Path);
    }
    return true;
}
TArray<int32> UDungeonRunProfile::BuildNormalComposition(int32 Seed, int32 Room, int32 Group, int32 Count) const
{
    TArray<int32> Result;
    FString Error;
    if (Count < 1 || Count > 30 || NormalRoster.IsEmpty() || !ValidateNormalRoster(Error))
        return Result;
    FRandomStream Stream(
        HashCombineFast(uint32(Seed), HashCombineFast(0x524F5354u, HashCombineFast(uint32(Room), uint32(Group)))));
    while (Result.Num() < Count)
    {
        TArray<int32> Bag;
        for (int32 I = 0; I < NormalRoster.Num(); ++I)
            Bag.Add(I);
        for (int32 I = Bag.Num() - 1; I > 0; --I)
            Bag.Swap(I, Stream.RandRange(0, I));
        for (int32 I : Bag)
        {
            Result.Add(I);
            if (Result.Num() == Count)
                break;
        }
    }
    return Result;
}
int32 UDungeonRunProfile::NormalPoolQuota(UClass* Class) const
{
    const int32 Capacity = FMath::Clamp(NormalPoolCapacity, 0, 64);
    if (NormalRoster.IsEmpty())
        return Capacity;
    int32 Eligible = 0, Position = INDEX_NONE;
    for (const auto& Entry : NormalRoster)
        if (Entry.bPoolEligible)
        {
            if (Entry.EnemyClass.Get() == Class)
                Position = Eligible;
            ++Eligible;
        }
    return Position == INDEX_NONE ? 0 : Capacity / Eligible + (Position < Capacity % Eligible ? 1 : 0);
}

int32 UDungeonRunProfile::MassCountForWave(bool bTwoWaves, int32 Wave) const
{
    if (!bUseMassMelee || Wave < 1 || Wave > (bTwoWaves ? 2 : 1))
        return 0;
    const int32 Count = FMath::Clamp(MassPerCombatRoom, 1, 50);
    return bTwoWaves ? (Wave == 1 ? Count / 2 : Count - Count / 2) : Count;
}
