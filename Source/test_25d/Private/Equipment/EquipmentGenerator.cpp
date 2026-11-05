#include "Equipment/EquipmentGenerator.h"
#include "Equipment/EquipmentStats.h"
#include "Backpack/ItemDefinition.h"
namespace
{
bool Fail(FString *E, const TCHAR *S)
{
    if (E)
        *E = S;
    return false;
}
bool Ranges(const TArray<FEquipmentStatRange> &R, int32 L, FRandomStream &RNG,
            TArray<FEquipmentRolledStat> &Out)
{
    if (R.Num() > EquipmentStats::All().Num())
        return false;
    TSet<FGameplayTag> Tags;
    for (const auto &V : R)
    {
        if (Tags.Contains(V.StatTag))
            return false;
        Tags.Add(V.StatTag);
        const float A = V.MinValue.GetValueAtLevel(L), B = V.MaxValue.GetValueAtLevel(L);
        if (!FEquipmentGenerator::IsSupportedStat(V.StatTag) || !FMath::IsFinite(A) || !FMath::IsFinite(B) ||
            A < 0 || B < A || B > 1000000 || V.DecimalPlaces < 0 || V.DecimalPlaces > 3)
            return false;
        const double Scale = FMath::Pow(10.0, V.DecimalPlaces);
        const double Low = FMath::CeilToDouble(A * Scale), High = FMath::FloorToDouble(B * Scale);
        if (Low > High)
            return false;
        FEquipmentRolledStat S;
        S.StatTag = V.StatTag;
        S.Value = static_cast<float>(
            (FMath::Min(High, Low + FMath::FloorToDouble(RNG.GetFraction() * (High - Low + 1)))) / Scale);
        Out.Add(S);
    }
    return true;
}
} // namespace
bool FEquipmentGenerator::IsSupportedStat(FGameplayTag T)
{
    return EquipmentStats::Find(T) != nullptr;
}
bool FEquipmentGenerator::ValidateRoll(const FEquipmentRollData &R, FString *E)
{
    if (R.GenerationVersion == 0)
        return (R.BaseStats.IsEmpty() && R.Affixes.IsEmpty() && R.ProfileId.IsNone() && R.RarityId.IsNone() &&
                R.RarityName.IsEmpty() && R.ItemLevel == 0 && R.RandomSeed == 0) ||
               Fail(E, TEXT("Malformed legacy roll"));
    if (R.GenerationVersion != 1 || R.ItemLevel < 1 || R.ItemLevel > 10000 || R.ProfileId.IsNone() ||
        R.RarityId.IsNone() || R.Affixes.Num() > 6 || R.BaseStats.Num() > EquipmentStats::All().Num())
        return Fail(E, TEXT("Invalid roll schema"));
    auto Valid = [](const TArray<FEquipmentRolledStat> &S) {
        if (S.Num() > EquipmentStats::All().Num())
            return false;
        TSet<FGameplayTag> Tags;
        for (const auto &V : S)
        {
            if (Tags.Contains(V.StatTag) || !IsSupportedStat(V.StatTag) || !FMath::IsFinite(V.Value) ||
                V.Value < 0 || V.Value > 1000000)
                return false;
            Tags.Add(V.StatTag);
        }
        return true;
    };
    if (!Valid(R.BaseStats))
        return Fail(E, TEXT("Invalid base stats"));
    TSet<FName> IDs, Groups;
    TMap<FGameplayTag, double> Totals;
    for (const auto &S : R.BaseStats)
        Totals.FindOrAdd(S.StatTag) += S.Value;
    for (const auto &A : R.Affixes)
    {
        if (A.AffixId.IsNone() || IDs.Contains(A.AffixId) || A.Tier < 1 || A.Tier > 10000 || !Valid(A.Stats))
            return Fail(E, TEXT("Invalid affix"));
        IDs.Add(A.AffixId);
        if (!A.ExclusiveGroup.IsNone())
        {
            if (Groups.Contains(A.ExclusiveGroup))
                return Fail(E, TEXT("Duplicate exclusive group"));
            Groups.Add(A.ExclusiveGroup);
        }
        for (const auto &S : A.Stats)
            Totals.FindOrAdd(S.StatTag) += S.Value;
    }
    for (const auto &Total : Totals)
        if (!FMath::IsFinite(Total.Value) || Total.Value > 1000000)
            return Fail(E, TEXT("Aggregate stat exceeds cap"));
    return true;
}
bool FEquipmentGenerator::Generate(const UBackpackItemDefinitions &C, const FBackpackItemDefEntry &D, int32 L,
                                   int32 Seed, FEquipmentRollData &Out, FString &E)
{
    E.Reset();
    Out = {};
    auto Bad = [&](const TCHAR *S) {
        UE_LOG(LogTemp, Warning, TEXT("Equipment generation: %s"), S);
        return Fail(&E, S);
    };
    if (!D.bEquippable || !D.bRandomizedEquipment || L < 0 || L > 10000 || !C.GenerationProfiles ||
        !C.Rarities || !C.Affixes ||
        C.GenerationProfiles->GetRowStruct() != FEquipmentGenerationProfileRow::StaticStruct() ||
        C.Rarities->GetRowStruct() != FEquipmentRarityRow::StaticStruct() ||
        C.Affixes->GetRowStruct() != FEquipmentAffixRow::StaticStruct())
        return Bad(TEXT("Invalid definition or tables"));
    const auto *P =
        C.GenerationProfiles->FindRow<FEquipmentGenerationProfileRow>(D.GenerationProfile, TEXT("Generate"));
    if (!P)
        return Bad(TEXT("Missing profile"));
    if (L == 0)
        L = P->DefaultItemLevel;
    if (L < 1 || L > 10000)
        return Bad(TEXT("Invalid default level"));
    FRandomStream RNG(Seed);
    FEquipmentRollData R;
    R.ProfileId = D.GenerationProfile;
    R.GenerationVersion = 1;
    R.ItemLevel = L;
    R.RandomSeed = Seed;
    if (!Ranges(P->BaseStats, L, RNG, R.BaseStats))
        return Bad(TEXT("Invalid base range"));
    TArray<FName> Names = P->RarityIds;
    Names.Sort(FNameLexicalLess());
    TSet<FName> Seen;
    double Total = 0;
    for (FName N : Names)
    {
        const auto *V = C.Rarities->FindRow<FEquipmentRarityRow>(N, TEXT("Generate"));
        if (!V || Seen.Contains(N) || !FMath::IsFinite(V->Weight) || V->Weight < 0 || V->MinAffixes < 0 ||
            V->MaxAffixes < V->MinAffixes || V->MaxAffixes > 6 || V->MaxPrefixes < 0 || V->MaxPrefixes > 6 ||
            V->MaxSuffixes < 0 || V->MaxSuffixes > 6)
            return Bad(TEXT("Invalid rarity"));
        Seen.Add(N);
        Total += V->Weight;
    }
    if (Total <= 0)
        return Bad(TEXT("No weighted rarity"));
    double Pick = RNG.GetFraction() * Total;
    const FEquipmentRarityRow *Q = nullptr;
    for (FName N : Names)
    {
        auto *V = C.Rarities->FindRow<FEquipmentRarityRow>(N, TEXT("Generate"));
        if (V->Weight <= 0)
            continue;
        Q = V;
        R.RarityId = N;
        Pick -= V->Weight;
        if (Pick < 0)
            break;
    }
    R.RarityName = Q->DisplayName;
    struct Candidate
    {
        FName Id;
        const FEquipmentAffixRow *Row;
        const FEquipmentAffixTier *Tier;
    };
    TArray<Candidate> Candidates;
    Names = C.Affixes->GetRowNames();
    Names.Sort(FNameLexicalLess());
    for (FName N : Names)
    {
        const auto *A = C.Affixes->FindRow<FEquipmentAffixRow>(N, TEXT("Generate"));
        if (!A->Profiles.IsEmpty() && !A->Profiles.Contains(D.GenerationProfile))
            continue;
        if (!FMath::IsFinite(A->Weight) || A->Weight < 0)
            return Bad(TEXT("Invalid affix weight"));
        const FEquipmentAffixTier *Best = nullptr;
        TSet<int32> Tiers;
        for (const auto &T : A->Tiers)
        {
            if (T.Tier < 1 || T.Tier > 10000 || Tiers.Contains(T.Tier) || T.MinItemLevel < 1 ||
                T.MaxItemLevel < T.MinItemLevel || T.MaxItemLevel > 10000)
                return Bad(TEXT("Invalid affix tier"));
            Tiers.Add(T.Tier);
            if (L >= T.MinItemLevel && L <= T.MaxItemLevel && (!Best || T.Tier > Best->Tier))
                Best = &T;
        }
        if (Best && A->Weight > 0)
        {
            TArray<FEquipmentRolledStat> Check;
            FRandomStream Dummy(0);
            if (!Ranges(Best->Stats, L, Dummy, Check))
                return Bad(TEXT("Invalid affix range"));
            Candidates.Add({N, A, Best});
        }
    }
    const int32 Count = RNG.RandRange(Q->MinAffixes, Q->MaxAffixes);
    int32 Prefixes = 0, Suffixes = 0;
    TSet<FName> Groups;
    for (int32 I = 0; I < Count; ++I)
    {
        Candidates.RemoveAll([&](const Candidate &V) {
            return (V.Row->bPrefix ? Prefixes >= Q->MaxPrefixes : Suffixes >= Q->MaxSuffixes) ||
                   (!V.Row->ExclusiveGroup.IsNone() && Groups.Contains(V.Row->ExclusiveGroup));
        });
        Total = 0;
        for (const auto &V : Candidates)
            Total += V.Row->Weight;
        if (Total <= 0)
            break;
        Pick = RNG.GetFraction() * Total;
        int32 Index = Candidates.Num() - 1;
        for (int32 J = 0; J < Candidates.Num(); ++J)
        {
            Pick -= Candidates[J].Row->Weight;
            if (Pick < 0)
            {
                Index = J;
                break;
            }
        }
        const Candidate V = Candidates[Index];
        FEquipmentRolledAffix A;
        A.AffixId = V.Id;
        A.ExclusiveGroup = V.Row->ExclusiveGroup;
        A.Tier = V.Tier->Tier;
        A.DisplayName = V.Row->DisplayName;
        A.bPrefix = V.Row->bPrefix;
        if (!Ranges(V.Tier->Stats, L, RNG, A.Stats))
            return Bad(TEXT("Invalid affix range"));
        R.Affixes.Add(A);
        if (A.bPrefix)
            ++Prefixes;
        else
            ++Suffixes;
        if (!V.Row->ExclusiveGroup.IsNone())
            Groups.Add(V.Row->ExclusiveGroup);
        Candidates.RemoveAt(Index);
    }
    if (!ValidateRoll(R, &E))
        return false;
    Out = MoveTemp(R);
    return true;
}
TMap<FGameplayTag, float> FEquipmentGenerator::GetTotals(const FEquipmentRollData &R)
{
    TMap<FGameplayTag, float> T;
    if (!ValidateRoll(R))
        return T;
    for (const auto &S : R.BaseStats)
        T.FindOrAdd(S.StatTag) += S.Value;
    for (const auto &A : R.Affixes)
        for (const auto &S : A.Stats)
            T.FindOrAdd(S.StatTag) += S.Value;
    return T;
}
FText FEquipmentGenerator::MakeDisplayName(const FText &B, const FEquipmentRollData &R)
{
    FString S;
    for (const auto &A : R.Affixes)
        if (A.bPrefix)
            S += A.DisplayName.ToString() + TEXT(" ");
    S += B.ToString();
    for (const auto &A : R.Affixes)
        if (!A.bPrefix)
            S += TEXT(" ") + A.DisplayName.ToString();
    return FText::FromString(S);
}
FText FEquipmentGenerator::MakeDescription(const FEquipmentRollData &R)
{
    if (R.GenerationVersion == 0)
        return FText::GetEmpty();
    FString S = FString::Printf(TEXT("%s · 等级 %d"), *R.RarityName.ToString(), R.ItemLevel);
    auto AppendStats = [&S](const TArray<FEquipmentRolledStat> &Stats) {
        for (const auto &Stat : Stats)
        {
            const auto* Definition = EquipmentStats::Find(Stat.StatTag);
            FString Label = Definition ? Definition->Label : Stat.StatTag.ToString();
            S += FString::Printf(TEXT("\n  %s +%g"), *Label, Stat.Value);
        }
    };
    if (!R.BaseStats.IsEmpty())
    {
        S += TEXT("\n基础属性");
        AppendStats(R.BaseStats);
    }
    for (const auto &Affix : R.Affixes)
    {
        S += FString::Printf(TEXT("\n%s · 阶级 %d"), *Affix.DisplayName.ToString(), Affix.Tier);
        AppendStats(Affix.Stats);
    }
    return FText::FromString(S);
}

bool FEquipmentGenerator::ValidateForDefinition(const FBackpackItemDefEntry* Def, const FEquipmentRollData& Roll)
{
 if (!ValidateRoll(Roll)) return false;
 if (!Def) return Roll.GenerationVersion == 0;
 return Def->bRandomizedEquipment == (Roll.GenerationVersion > 0) &&
  (!Def->bRandomizedEquipment || (Def->bEquippable && Roll.ProfileId == Def->GenerationProfile));
}
