// 调用链：奖励/拾取/背包生成 → Generate → 固定种子实例；装备与展示消费 ValidateRoll/GetTotals。
#pragma once
#include "CoreMinimal.h"
#include "Equipment/EquipmentRollTypes.h"
class UBackpackItemDefinitions;
struct FBackpackItemDefEntry;
struct TEST_25D_API FEquipmentGenerator
{
    // bRequireAffix 用于保底奖励：排除零词缀品质，无合格词缀时失败，不修改现有物品结果。
    static bool Generate(const UBackpackItemDefinitions& Catalog, const FBackpackItemDefEntry& Def, int32 ItemLevel,
                         int32 Seed, FEquipmentRollData& Out, FString& Error, bool bRequireAffix = false);
    static bool ValidateRoll(const FEquipmentRollData& Roll, FString* Error = nullptr);
    static bool ValidateForDefinition(const FBackpackItemDefEntry* Def, const FEquipmentRollData& Roll);
    static bool IsSupportedStat(FGameplayTag Tag);
    static TMap<FGameplayTag, float> GetTotals(const FEquipmentRollData& Roll);
    static FText MakeDisplayName(const FText& Base, const FEquipmentRollData& Roll);
    static FText MakeDescription(const FEquipmentRollData& Roll);
};
