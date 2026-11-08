/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Equipment.Random.Generation
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/ItemDefinition.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentGenerationTest, "test_25d.Equipment.Random.Generation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Equipment.Random.Generation 回归，失败信息由下方断言交给自动化框架收集。 */
bool FEquipmentGenerationTest::RunTest(const FString&)
{
    auto* C = NewObject<UBackpackItemDefinitions>();
    C->GenerationProfiles = NewObject<UDataTable>(C);
    C->GenerationProfiles->RowStruct = FEquipmentGenerationProfileRow::StaticStruct();
    C->Rarities = NewObject<UDataTable>(C);
    C->Rarities->RowStruct = FEquipmentRarityRow::StaticStruct();
    C->Affixes = NewObject<UDataTable>(C);
    C->Affixes->RowStruct = FEquipmentAffixRow::StaticStruct();
    FBackpackItemDefEntry D;
    D.bEquippable = true;
    D.bRandomizedEquipment = true;
    D.GenerationProfile = TEXT("Test");
    FEquipmentStatRange S;
    S.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Stat.Armor"));
    S.MinValue = FScalableFloat(2);
    S.MaxValue = FScalableFloat(8);
    FEquipmentGenerationProfileRow P;
    P.DefaultItemLevel = 5;
    P.RarityIds.Add(TEXT("Rare"));
    P.BaseStats.Add(S);
    C->GenerationProfiles->AddRow(TEXT("Test"), P);
    FEquipmentRarityRow Q;
    Q.MinAffixes = 3;
    Q.MaxAffixes = 3;
    C->Rarities->AddRow(TEXT("Rare"), Q);
    FEquipmentAffixRow A;
    A.ExclusiveGroup = TEXT("Exclusive");
    FEquipmentAffixTier T;
    T.Stats.Add(S);
    A.Tiers.Add(T);
    T.Tier = 2;
    T.MinItemLevel = 5;
    A.Tiers.Add(T);
    C->Affixes->AddRow(TEXT("One"), A);
    C->Affixes->AddRow(TEXT("Two"), A);
    FString Error;
    FEquipmentRollData R, R2;
    for (int32 Seed = 0; Seed < 64; ++Seed)
    {
        if (!FEquipmentGenerator::Generate(*C, D, 0, Seed, R, Error))
        {
            AddError(FString::Printf(TEXT("Generate seed %d failed: %s"), Seed, *Error));
            return false;
        }
        if (!FEquipmentGenerator::Generate(*C, D, 0, Seed, R2, Error))
        {
            AddError(FString::Printf(TEXT("Repeat seed %d failed: %s"), Seed, *Error));
            return false;
        }
        TestTrue(TEXT("Deterministic"), R == R2);
        TestEqual(TEXT("Default level"), R.ItemLevel, 5);
        TestEqual(TEXT("Exclusive exhaustion"), R.Affixes.Num(), 1);
        if (!R.BaseStats.IsEmpty())
            TestTrue(TEXT("Range"), R.BaseStats[0].Value >= 2 && R.BaseStats[0].Value <= 8);
        if (!R.Affixes.IsEmpty())
            TestEqual(TEXT("Highest eligible tier"), R.Affixes[0].Tier, 2);
    }
    if (!TestEqual(TEXT("Base stat count"), R.BaseStats.Num(), 1) ||
        !TestEqual(TEXT("Affix count"), R.Affixes.Num(), 1))
        return false;
    FEquipmentRollData Broken = R;
    const FEquipmentRolledStat Duplicate = Broken.BaseStats[0];
    Broken.BaseStats.Add(Duplicate);
    TestFalse(TEXT("Duplicate stat rejected"), FEquipmentGenerator::ValidateRoll(Broken));
    Broken = R;
    auto Copy = Broken.Affixes[0];
    Copy.AffixId = TEXT("Different");
    Broken.Affixes.Add(Copy);
    TestFalse(TEXT("Saved exclusive group rejected"), FEquipmentGenerator::ValidateRoll(Broken));
    Broken = R;
    Broken.BaseStats[0].Value = 1000000;
    TestFalse(TEXT("Aggregate cap"), FEquipmentGenerator::ValidateRoll(Broken));
    P.BaseStats[0].MinValue = FScalableFloat(1.2f);
    P.BaseStats[0].MaxValue = FScalableFloat(1.8f);
    C->GenerationProfiles->AddRow(TEXT("Test"), P);
    AddExpectedError(TEXT("Invalid base range"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("No representable integer"), FEquipmentGenerator::Generate(*C, D, 5, 1, R2, Error));
    P.BaseStats[0].MinValue = FScalableFloat(100);
    P.BaseStats[0].MaxValue = FScalableFloat(200);
    C->GenerationProfiles->AddRow(TEXT("Test"), P);
    TestTrue(TEXT("Saved values survive tuning"), FEquipmentGenerator::ValidateRoll(R));
    Q.Weight = -1;
    C->Rarities->AddRow(TEXT("Rare"), Q);
    AddExpectedError(TEXT("Invalid rarity"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("Negative weight rejected"), FEquipmentGenerator::Generate(*C, D, 5, 0, R2, Error));
    R.BaseStats[0].Value = -1;
    TestFalse(TEXT("Invalid saved value"), FEquipmentGenerator::ValidateRoll(R));
    FEquipmentRollData Legacy;
    TestTrue(TEXT("Legacy empty"), FEquipmentGenerator::ValidateRoll(Legacy));
    Legacy.ItemLevel = 1;
    TestFalse(TEXT("Legacy hidden payload"), FEquipmentGenerator::ValidateRoll(Legacy));
    return true;
}
#endif
