// 测试链：实际三目录并集 -> 每个真实类的运行时默认定义 -> 宏残留清单；本测试不替代逐资产玩法验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeCatalog.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "Abilities/GameplayAbility.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeFineCatalogCensus,
                                 "test_25d.DynamicMage.Catalog.EveryGameplayVariantHasFineDefaultDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeFineCatalogCensus::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    TArray<FAbilityRuntimeBaseDefinition> Definitions;
    TArray<FString> Diagnostics;
    if (!TestTrue(TEXT("Actual three mage catalogs load independently of documentation"),
                  AbilityRuntimeRecipeCatalog::CollectBaseDefinitions(F.World, nullptr, Definitions, Diagnostics)))
    {
        AddInfo(FString::Join(Diagnostics, TEXT("\n")));
        return false;
    }
    TestEqual(TEXT("Reviewed class-variant denominator has no silently skipped identities"), Definitions.Num(), 86);
    const TSet<FName> WholeActions = {
        TEXT("Shape.ArcaneNova"),          TEXT("Shape.ArcaneShards"),        TEXT("Channel.Electrocute"),
        TEXT("Channel.OrbitingFireBolts"), TEXT("Delivery.ActorProjectiles"), TEXT("Delivery.ForkedChain"),
        TEXT("Delivery.ForkedVolley"),     TEXT("Delivery.SequentialChain"),  TEXT("Status.PetrifyArea")};
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Protected = 0;
    int32 Missing = 0;
    int32 StillCoupled = 0;
    int32 FineDefinitions = 0;
    for (const auto& Definition : Definitions)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("class"), GetPathNameSafe(Definition.AbilityClass.Get()));
        Row->SetStringField(TEXT("identity"), Definition.AbilityTag.ToString());
        Row->SetStringField(TEXT("frontend"), StaticEnum<EAbilityRuntimeFrontendKind>()->GetNameStringByValue(
                                                  static_cast<int64>(Definition.FrontendKind)));
        TArray<TSharedPtr<FJsonValue>> Types;
        TArray<TSharedPtr<FJsonValue>> Macros;
        const FName Identity = Definition.AbilityTag.GetTagName();
        const bool bProtected = Definition.FrontendKind == EAbilityRuntimeFrontendKind::Infrastructure ||
                                Identity == TEXT("Abilities.Passive.HaloOfProtection") ||
                                Identity == TEXT("Abilities.Passive.LifeSiphon") ||
                                Identity == TEXT("Abilities.Passive.ManaSiphon");
        if (bProtected)
        {
            ++Protected;
            Row->SetStringField(TEXT("definitionStatus"), TEXT("ProtectedOriginalContract"));
        }
        else if (!Definition.DefaultGraph)
        {
            ++Missing;
            Row->SetStringField(TEXT("definitionStatus"), TEXT("Missing"));
        }
        else
        {
            for (const auto& Node : Definition.DefaultGraph->Nodes)
            {
                Types.Add(MakeShared<FJsonValueString>(Node.TypeId.ToString()));
                if (Node.TypeId.ToString().StartsWith(TEXT("Legacy.")) || WholeActions.Contains(Node.TypeId))
                {
                    Macros.Add(MakeShared<FJsonValueString>(Node.TypeId.ToString()));
                }
            }
            Row->SetStringField(TEXT("definitionHash"), AbilityRuntimeRecipes::Fingerprint(*Definition.DefaultGraph));
            Row->SetStringField(TEXT("definitionStatus"),
                                Macros.IsEmpty() ? TEXT("FineDefinitionOnly") : TEXT("NeedsBehaviorExpansion"));
            StillCoupled += !Macros.IsEmpty() ? 1 : 0;
            FineDefinitions += Macros.IsEmpty() ? 1 : 0;
        }
        Row->SetArrayField(TEXT("actualTypes"), Types);
        Row->SetArrayField(TEXT("coupledBehaviors"), Macros);
        Row->SetStringField(TEXT("gameplayParity"),
                            bProtected ? TEXT("ProtectedSeparateTests") : TEXT("RequiresPerClassBehaviorEvidence"));
        Row->SetStringField(TEXT("diagnostic"), Definition.Diagnostic);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("capturedUtc"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("scope"),
                           TEXT("Definition decomposition census only; never a gameplay/network/visual pass."));
    Report->SetNumberField(TEXT("classes"), Definitions.Num());
    Report->SetNumberField(TEXT("protected"), Protected);
    Report->SetNumberField(TEXT("fineDefinitions"), FineDefinitions);
    Report->SetNumberField(TEXT("coupled"), StillCoupled);
    Report->SetNumberField(TEXT("missing"), Missing);
    Report->SetArrayField(TEXT("rows"), Rows);
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GAAuthoring/DynamicMage/Census"),
                                              FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("definitions.json")));
    TestTrue(TEXT("Per-class observed definition report is saved"), FFileHelper::SaveStringToFile(Text, *Path));
    AddInfo(FString::Printf(TEXT("Actual classes=%d protected=%d fine-definitions=%d coupled=%d missing=%d; %s"),
                            Definitions.Num(), Protected, FineDefinitions, StillCoupled, Missing, *Path));
    TestEqual(TEXT("No current gameplay variant lacks an actual exported default"), Missing, 0);
    TestEqual(TEXT("All current gameplay variants expose decomposed defaults, rather than a renamed whole macro"),
              StillCoupled, 0);
    return true;
}
#endif
