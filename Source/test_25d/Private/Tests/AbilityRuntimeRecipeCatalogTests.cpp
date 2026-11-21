// 调用链：Automation -> 真实职业目录/测试内存目录 -> 运行时基础配方目录 -> 专用 GA 的实际默认图。
// 只读现有包，不以迁移矩阵或目录名称猜职业；同身份的 Actor/Mixed 类分别验证。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeCatalog.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "AbilitySystem/Batch6/Batch6Petrify.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "PureMass/PureMassAbility.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimeRecipeCatalogTests
{
    const TCHAR* CatalogPaths[] = {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                                   TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                                   TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")};

    /** 使用真实标签与明确职业，测试合并规则而不修改任何原目录/CDO。 */
    FMyAbilityInfo Row(FGameplayTag Tag, TSubclassOf<UGameplayAbility> AbilityClass, EPlayerProfession Profession)
    {
        FMyAbilityInfo Result;
        Result.AbilityTag = Tag;
        Result.Ability = AbilityClass;
        Result.RequiredProfession = Profession;
        return Result;
    }

    /** 将节点 GUID 解析成实际 TypeId，避免“有图”掩盖派生 GA 走错原型默认实现。 */
    FName EntryType(const UAbilityGraphDefinition* Graph)
    {
        if (!Graph)
        {
            return NAME_None;
        }
        const auto* Node = Graph->Nodes.FindByPredicate(
            [Graph](const FAbilityGraphNode& Candidate)
            {
                return Candidate.NodeId == Graph->EntryNodeId;
            });
        return Node ? Node->TypeId : NAME_None;
    }
} // namespace AbilityRuntimeRecipeCatalogTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeMageCatalogCoverage,
                                 "test_25d.UnifiedGA.DynamicRecipe.CatalogRealMageCoverage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeMageCatalogCoverage::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeCatalogTests;
    FAbilityGraphTestFixture F;
    TSet<FString> ExpectedVariants;
    TSet<FGameplayTag> ExpectedIdentities;
    UAbilityInfo* Preferred = nullptr;
    for (const TCHAR* Path : CatalogPaths)
    {
        auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!TestNotNull(TEXT("Reviewed source catalog loads"), Catalog))
        {
            return false;
        }
        if (!Preferred)
        {
            Preferred = Catalog;
        }
        for (const auto& Item : Catalog->AbilityInformation)
        {
            if (Item.RequiredProfession == EPlayerProfession::Mage && Item.Ability)
            {
                ExpectedVariants.Add(Item.AbilityTag.ToString() + TEXT("|") + Item.Ability->GetPathName());
                ExpectedIdentities.Add(Item.AbilityTag);
            }
        }
    }
    TArray<FAbilityRuntimeBaseDefinition> Definitions;
    TArray<FString> Diagnostics;
    if (!TestTrue(TEXT("Runtime catalog reads actual complete mage union"),
                  AbilityRuntimeRecipeCatalog::CollectBaseDefinitions(F.World, Preferred, Definitions, Diagnostics)))
    {
        return false;
    }
    TestEqual(TEXT("Every actual Tag/Class variant is retained"), Definitions.Num(), ExpectedVariants.Num());
    TSet<FGameplayTag> ActualIdentities;
    TMap<FGameplayTag, int32> PreferredCounts;
    int32 Protected = 0;
    for (const auto& Item : Definitions)
    {
        const FString Key = Item.AbilityTag.ToString() + TEXT("|") + GetPathNameSafe(Item.AbilityClass.Get());
        TestTrue(TEXT("No inferred Stander/unknown entry enters mage union"), ExpectedVariants.Contains(Key));
        ActualIdentities.Add(Item.AbilityTag);
        if (Item.bPreferredForWorld)
        {
            ++PreferredCounts.FindOrAdd(Item.AbilityTag);
        }
        if (!Item.bSelectable)
        {
            ++Protected;
            TestFalse(TEXT("Protected entries explain their restriction"), Item.Diagnostic.IsEmpty());
        }
    }
    TestEqual(TEXT("Every actual mage identity is represented"), ActualIdentities.Num(), ExpectedIdentities.Num());
    for (const auto Tag : ExpectedIdentities)
    {
        TestEqual(TEXT("Each identity has one world-preferred class"), PreferredCounts.FindRef(Tag), 1);
        TestNotNull(TEXT("Preferred lookup resolves each identity"),
                    AbilityRuntimeRecipeCatalog::FindPreferred(Definitions, Tag));
    }
    TestEqual(TEXT("Only internal listener and three existing placeholders are protected"), Protected, 4);
    AddInfo(FString::Printf(TEXT("Observed complete runtime mage catalog: %d identities, %d class variants."),
                            ExpectedIdentities.Num(), ExpectedVariants.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeMageCatalogPreference,
                                 "test_25d.UnifiedGA.DynamicRecipe.CatalogWorldPreferenceAndProfessionFilter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeMageCatalogPreference::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeCatalogTests;
    FAbilityGraphTestFixture F;
    auto* Actor = NewObject<UAbilityInfo>(F.World);
    auto* Mixed = NewObject<UAbilityInfo>(F.World);
    const auto Tag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    Actor->AbilityInformation.Add(Row(Tag, UPrototypeAbility::StaticClass(), EPlayerProfession::Mage));
    Mixed->AbilityInformation.Add(Row(Tag, UPureMassAbility::StaticClass(), EPlayerProfession::Mage));
    Mixed->AbilityInformation.Add(Row(FMyGameplayTags::Get().Abilities_Arcane_ArcaneShards,
                                      UPrototypeAbility::StaticClass(), EPlayerProfession::Stander));
    TArray<FAbilityRuntimeBaseDefinition> Definitions;
    TArray<FString> Diagnostics;
    if (!TestTrue(
            TEXT("Memory catalogs merge without mutating their assets"),
            AbilityRuntimeRecipeCatalog::CollectFromCatalogs(F.World, {Actor, Mixed}, Mixed, Definitions, Diagnostics)))
    {
        return false;
    }
    TestEqual(TEXT("Both concrete variants remain distinct and Stander is filtered"), Definitions.Num(), 2);
    const auto* Preferred = AbilityRuntimeRecipeCatalog::FindPreferred(Definitions, Tag);
    if (TestNotNull(TEXT("Current world variant is found"), Preferred))
    {
        TestEqual(TEXT("Mixed world chooses Mixed class even when Actor catalog appears first"),
                  Preferred->AbilityClass.Get(), UPureMassAbility::StaticClass());
        TestTrue(TEXT("Mixed version explicitly supports virtual targets"), Preferred->bSupportsMassTargets);
        TestEqual(TEXT("Frontend classification follows actual inheritance"), Preferred->FrontendKind,
                  EAbilityRuntimeFrontendKind::MixedPrototype);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeMageSpecializedDefaults,
                                 "test_25d.UnifiedGA.DynamicRecipe.CatalogSpecializedDefaultsPreserveActualStrategy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeMageSpecializedDefaults::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeCatalogTests;
    FAbilityGraphTestFixture F;
    auto* SequentialClass = LoadClass<UBatch6ChainAbility>(
        nullptr, TEXT("/Game/CompositionBatch6/GA_SequentialChain.GA_SequentialChain_C"));
    auto* PetrifyClass = LoadClass<UBatch6PetrifyAbility>(
        nullptr, TEXT("/Game/CompositionBatch6/GA_PetrifyingGaze.GA_PetrifyingGaze_C"));
    if (!TestNotNull(TEXT("Actual sequential-chain GA is included despite old matrix omission"), SequentialClass) ||
        !TestNotNull(TEXT("Actual petrification GA loads"), PetrifyClass))
    {
        return false;
    }
    FString Error;
    auto* Chain = AbilityRuntimeRecipes::BuildDefaultGraph(
        F.World, SequentialClass->GetDefaultObject<UBatch6ChainAbility>(), Error);
    auto* Petrify = AbilityRuntimeRecipes::BuildDefaultGraph(
        F.World, PetrifyClass->GetDefaultObject<UBatch6PetrifyAbility>(), Error);
    TestEqual(TEXT("Derived chain cannot silently compile as Legacy.Actor.Chain"), EntryType(Chain),
              FName(TEXT("Control.LightningSteps")));
    TestEqual(TEXT("Derived petrify cannot silently compile as its inherited damage prototype"), EntryType(Petrify),
              FName(TEXT("Query.PetrifyArea")));
    return true;
}
#endif
