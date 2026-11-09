// 调用链：Editor自动化 -> AssetRegistry全部配置资产 -> 原生Validate/真实Montage上下文 -> 独立逐项JSON证据。
// 本测试只加载和校验，不保存资产；MonsterCombat无Validate，明确记录其现有角色运行引用契约。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Ability/GruxEnemyAbility.h"
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "AbilitySystem/Ability/ShroomEnemyAbility.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Character/ShamanEnemyCharacter.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Player/StanderCombatConfig.h"
#include "Player/StanderWeaponActionConfig.h"
#include "Serialization/JsonSerializer.h"
#include "StoneGolem/StoneGolemConfig.h"
#include "UObject/StrongObjectPtr.h"

namespace AbilityCatalogConfigurationTests
{
    /** 与制作目录同一组配置基类；派生资产由AssetRegistry递归发现，不能只查固定的303个路径。 */
    TArray<FAssetData> Discover()
    {
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Registry.SearchAllAssets(true);
        FARFilter Filter;
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.bRecursivePaths = true;
        Filter.bRecursiveClasses = true;
        for (const auto* Class : {UAbilityPrototype::StaticClass(), UAbilityTimelineDefinition::StaticClass(),
                                  USwordShieldConfig::StaticClass(), UStanderWeaponActionConfig::StaticClass(),
                                  UStoneGolemAttackConfig::StaticClass(), UMonsterCombatConfig::StaticClass(),
                                  UOrbitingFireBoltConfig::StaticClass(), UAbilityGraphDefinition::StaticClass()})
        {
            Filter.ClassPaths.Add(Class->GetClassPathName());
        }
        TArray<FAssetData> Assets;
        Registry.GetAssets(Filter, Assets);
        Assets.Sort(
            [](const FAssetData& A, const FAssetData& B)
            {
                return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
            });
        return Assets;
    }

    /** 不伪造MonsterCombat的Validate；只验证ApplyConfig/GrantEnemyAbilities实际消费的引用及有限数值。 */
    bool ValidateMonsterReferences(const UMonsterCombatConfig& Config, FString& Error)
    {
        bool bValid = Config.Mesh && Config.AnimClass && Config.Behavior && Config.DamageEffect &&
                      !Config.Attacks.IsEmpty() && FMath::IsFinite(Config.BaseDamage) && Config.BaseDamage >= 0.f &&
                      FMath::IsFinite(Config.WalkSpeed) && Config.WalkSpeed > 0.f;
        for (const auto& Attack : Config.Attacks)
        {
            bValid &= Attack != nullptr;
        }
        if (const auto* Golem = Cast<UStoneGolemConfig>(&Config))
        {
            bValid &= Golem->Hammer && Golem->Shield;
        }
        if (!bValid)
        {
            Error = TEXT("Monster runtime references or finite damage/movement values are invalid.");
        }
        return bValid;
    }

    /** 按最具体派生类型分派；派生专属条件不能被父类通用Tuning校验吞掉。 */
    bool Validate(UObject* Asset, const TArray<TStrongObjectPtr<UObject>>& Loaded, FString& Validator,
                  TArray<FString>& Contexts, FString& Error)
    {
        if (const auto* Extension = Cast<UExtensionNode>(Asset))
        {
            Validator = TEXT("UExtensionNode::ValidateGraph");
            return Extension->ValidateGraph();
        }
        if (const auto* Prototype = Cast<UAbilityPrototype>(Asset))
        {
            Validator = TEXT("UAbilityPrototype::IsValidConfiguration");
            return Prototype->IsValidConfiguration();
        }
        if (const auto* Grux = Cast<UGruxAttackConfig>(Asset))
        {
            Validator = TEXT("UGruxAttackConfig::ValidateGrux");
            return Grux->ValidateGrux(Error);
        }
        if (const auto* Spore = Cast<UShroomSporeConfig>(Asset))
        {
            Validator = TEXT("UShroomSporeConfig::ValidateSpore");
            return Spore->ValidateSpore(Error);
        }
        if (const auto* Sling = Cast<USlingAttackConfig>(Asset))
        {
            Validator = TEXT("FStoneGolemAttackTuning::Validate + USlingAttackConfig::ValidateProjectile");
            return Sling->Tuning.Validate(Error) && Sling->ValidateProjectile();
        }
        if (const auto* Summon = Cast<UShamanSummonConfig>(Asset))
        {
            Validator = TEXT("FStoneGolemAttackTuning::Validate + UShamanSummonConfig::ValidateSummon");
            return Summon->Tuning.Validate(Error) && Summon->ValidateSummon();
        }
        if (const auto* Attack = Cast<UStoneGolemAttackConfig>(Asset))
        {
            Validator = TEXT("FStoneGolemAttackTuning::Validate");
            return Attack->Tuning.Validate(Error);
        }
        if (const auto* Stander = Cast<UStanderCombatConfig>(Asset))
        {
            Validator = TEXT("UStanderCombatConfig::ValidateStander");
            return Stander->ValidateStander();
        }
        if (const auto* Sword = Cast<USwordShieldConfig>(Asset))
        {
            Validator = TEXT("USwordShieldConfig::Validate");
            return Sword->Validate();
        }
        if (const auto* Weapon = Cast<UStanderWeaponActionConfig>(Asset))
        {
            Validator = TEXT("UStanderWeaponActionConfig::Validate");
            if (Weapon->Montage)
            {
                Contexts.Add(Weapon->Montage->GetPathName());
            }
            return Weapon->Validate();
        }
        if (const auto* Timeline = Cast<UAbilityTimelineDefinition>(Asset))
        {
            Validator = TEXT("UAbilityTimelineDefinition::ResolveMontage using actual WeaponAction references");
            bool bValid = true;
            for (const auto& Item : Loaded)
            {
                const auto* Weapon = Cast<UStanderWeaponActionConfig>(Item.Get());
                if (!Weapon || Weapon->Timeline != Timeline)
                {
                    continue;
                }
                FAbilityTimelineSnapshot Snapshot;
                Contexts.Add(Weapon->GetPathName() + TEXT(" -> ") + GetPathNameSafe(Weapon->Montage));
                FString Reason;
                if (!Timeline->ResolveMontage(Weapon->Montage, Snapshot, Reason))
                {
                    Error += Reason + TEXT("; ");
                    bValid = false;
                }
            }
            if (Contexts.IsEmpty())
            {
                Error = TEXT("Timeline has no real authoring montage reference; cannot invent validation length.");
            }
            return bValid && !Contexts.IsEmpty();
        }
        if (const auto* Orbit = Cast<UOrbitingFireBoltConfig>(Asset))
        {
            Validator = TEXT("UOrbitingFireBoltConfig::IsValidConfiguration");
            return Orbit->IsValidConfiguration();
        }
        if (const auto* Graph = Cast<UAbilityGraphDefinition>(Asset))
        {
            Validator = TEXT("UAbilityGraphDefinition::ValidateForAuthoring");
            TArray<FString> Errors;
            const bool bValid = Graph->ValidateForAuthoring(Errors);
            Error = FString::Join(Errors, TEXT("; "));
            return bValid;
        }
        if (const auto* Monster = Cast<UMonsterCombatConfig>(Asset))
        {
            Validator = TEXT("ExistingRuntimeReferenceContract (no native Validate declared)");
            return ValidateMonsterReferences(*Monster, Error);
        }
        Error = TEXT("Discovered configuration has no explicit validator mapping.");
        return false;
    }
} // namespace AbilityCatalogConfigurationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnifiedCatalogConfigurations, "test_25d.UnifiedGA.Catalog.AllConfigurationAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnifiedCatalogConfigurations::RunTest(const FString&)
{
    const auto Assets = AbilityCatalogConfigurationTests::Discover();
    TestTrue(TEXT("Fresh configuration inventory is nonempty"), !Assets.IsEmpty());
    TArray<TStrongObjectPtr<UObject>> Loaded;
    for (const auto& Data : Assets)
    {
        Loaded.Emplace(Data.GetAsset());
    }
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("testName"), TEXT("test_25d.UnifiedGA.Catalog.AllConfigurationAssets"));
    Report->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
    TArray<TSharedPtr<FJsonValue>> Rows;
    bool bAllValid = !Assets.IsEmpty();
    for (int32 Index = 0; Index < Assets.Num(); ++Index)
    {
        const auto& Data = Assets[Index];
        FString Validator, Error;
        TArray<FString> Contexts;
        const FString Path = Data.GetSoftObjectPath().ToString();
        const FString File = FPackageName::LongPackageNameToFilename(Data.PackageName.ToString(),
                                                                     FPackageName::GetAssetPackageExtension());
        const FString BeforeHash = LexToString(FMD5Hash::HashFile(*File));
        bool bValid = Loaded[Index].IsValid() && AbilityCatalogConfigurationTests::Validate(Loaded[Index].Get(), Loaded,
                                                                                            Validator, Contexts, Error);
        bValid &= BeforeHash == LexToString(FMD5Hash::HashFile(*File));
        if (!bValid && Error.IsEmpty())
        {
            Error = TEXT("Native validator returned false, asset could not load, or disk package changed.");
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("assetPath"), Path);
        Row->SetStringField(TEXT("class"), Data.AssetClassPath.ToString());
        Row->SetStringField(TEXT("validator"), Validator);
        Row->SetStringField(TEXT("packageMd5"), BeforeHash);
        Row->SetBoolField(TEXT("success"), bValid);
        TArray<TSharedPtr<FJsonValue>> Errors, MontageContexts;
        if (!Error.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(Error));
        }
        for (const auto& Context : Contexts)
        {
            MontageContexts.Add(MakeShared<FJsonValueString>(Context));
        }
        Row->SetArrayField(TEXT("errors"), Errors);
        Row->SetArrayField(TEXT("contextMontages"), MontageContexts);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        TestTrue(Path + TEXT(" : ") + Error, bValid);
        AddInfo(Path + TEXT(" -> ") + Validator);
        bAllValid &= bValid;
    }
    Report->SetArrayField(TEXT("rows"), Rows);
    Report->SetBoolField(TEXT("success"), bAllValid);
    Report->SetNumberField(TEXT("count"), Rows.Num());
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("GAAuthoring/UnifiedGA/ConfigurationValidation") /
                              (FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")) + TEXT("-") +
                               FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    const FString Output = Directory / TEXT("configuration-report.json");
    TestTrue(TEXT("Per-asset validation report saved"), FFileHelper::SaveStringToFile(Json, *Output));
    AddInfo(TEXT("Configuration validation report: ") + FPaths::ConvertRelativePathToFull(Output));
    return true;
}
#endif
