// 审计链：正式注册表/实际法师目录 → 只读GraphView能力投影 → Saved机器表；不授予技能或保存资产。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeGraphView.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Unified/AbilityNodeExecution.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphCapabilityAudit,
                                 "test_25d.GraphExpansion.Capabilities.ActualRegistryAndCatalogAudit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGraphCapabilityAudit::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    if (!TestTrue(TEXT("Read the actual immutable Mage catalog"), F.Developer->InitializeCatalog(nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityNodeRegistry Registry;
    Registry.RegisterBuiltins();
    TMap<FName, TSet<FName>> ModifiersByType;
    TMap<FName, int32> ObservedByType;
    TArray<TSharedPtr<FJsonValue>> Definitions;
    for (const auto& Row : F.Developer->GetDefinitions())
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("abilityClass"), GetPathNameSafe(Row.Base.AbilityClass.Get()));
        Item->SetStringField(TEXT("abilityTag"), Row.Base.AbilityTag.ToString());
        Item->SetBoolField(TEXT("selectable"), Row.Base.bSelectable);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        if (Row.Base.DefaultGraph)
        {
            FAbilityRuntimeRecipeSnapshot Snapshot;
            Snapshot.Graph = Row.Base.DefaultGraph;
            Snapshot.AbilityTag = Row.Base.AbilityTag;
            FAbilityRuntimeGraphView View;
            if (!AbilityRuntimeGraphViews::Build(Snapshot, View, Error))
            {
                AddError(TEXT("Configured graph view failed: ") + Item->GetStringField(TEXT("abilityClass")) +
                         TEXT(" ") + Error);
                return false;
            }
            for (const auto& Node : View.Nodes)
            {
                auto Value = MakeShared<FJsonObject>();
                Value->SetStringField(TEXT("id"), Node.NodeId.ToString());
                Value->SetStringField(TEXT("type"), Node.TypeId.ToString());
                Value->SetStringField(TEXT("parameters"), Node.ParameterSummary);
                TArray<TSharedPtr<FJsonValue>> Mods;
                for (FName Modifier : Node.SupportedModifiers)
                {
                    Mods.Add(MakeShared<FJsonValueString>(Modifier.ToString()));
                    ModifiersByType.FindOrAdd(Node.TypeId).Add(Modifier);
                }
                ++ObservedByType.FindOrAdd(Node.TypeId);
                Value->SetArrayField(TEXT("modifiers"), Mods);
                Nodes.Add(MakeShared<FJsonValueObject>(Value));
            }
        }
        Item->SetArrayField(TEXT("actualNodes"), Nodes);
        Definitions.Add(MakeShared<FJsonValueObject>(Item));
    }
    TArray<TSharedPtr<FJsonValue>> Types;
    for (FName Type : Registry.GetRegisteredTypeIds())
    {
        const auto& D = *Registry.Find(Type);
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("type"), Type.ToString());
        Item->SetStringField(TEXT("configClass"), GetPathNameSafe(D.ConfigClass.Get()));
        Item->SetStringField(TEXT("executionClass"), GetPathNameSafe(D.ExecutionClass.Get()));
        Item->SetBoolField(TEXT("actor"), D.bSupportsActorTargets);
        Item->SetBoolField(TEXT("mass"), D.bSupportsMassTargets);
        Item->SetBoolField(TEXT("async"), D.bIsAsync);
        Item->SetBoolField(TEXT("runtimeFinite"), D.bRuntimeFiniteSafe);
        Item->SetBoolField(TEXT("runtimePure"), D.bRuntimePurePayload);
        Item->SetNumberField(TEXT("observedCatalogInstances"), ObservedByType.FindRef(Type));
        Item->SetStringField(
            TEXT("input"), StaticEnum<EAbilityPortPayload>()->GetNameStringByValue(static_cast<int64>(D.InputPayload)));
        TArray<TSharedPtr<FJsonValue>> Ports;
        for (const auto& Port : D.Ports)
        {
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("name"), Port.Name.ToString());
            Value->SetStringField(TEXT("payload"), StaticEnum<EAbilityPortPayload>()->GetNameStringByValue(
                                                       static_cast<int64>(Port.Payload)));
            Value->SetBoolField(TEXT("terminal"), Port.bTerminal);
            Ports.Add(MakeShared<FJsonValueObject>(Value));
        }
        Item->SetArrayField(TEXT("declaredPorts"), Ports);
        TArray<FName> Mods = ModifiersByType.FindOrAdd(Type).Array();
        Mods.Sort(FNameLexicalLess());
        TArray<TSharedPtr<FJsonValue>> ModifierValues;
        for (FName Modifier : Mods)
        {
            ModifierValues.Add(MakeShared<FJsonValueString>(Modifier.ToString()));
        }
        Item->SetArrayField(TEXT("observedModifiers"), ModifierValues);
        Types.Add(MakeShared<FJsonValueObject>(Item));
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("generatedUtc"), FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(
        TEXT("scope"),
        TEXT(
            "Actual registry + configured Mage catalog; declarations do not prove every port publishes, and absence from catalog does not prove a strategy is unusable."));
    Root->SetArrayField(TEXT("types"), Types);
    Root->SetArrayField(TEXT("definitions"), Definitions);
    TArray<TSharedPtr<FJsonValue>> Fragments;
    for (FName Id : AbilityRuntimeModifierTemplates::GetFragmentIds())
    {
        Fragments.Add(MakeShared<FJsonValueString>(Id.ToString()));
    }
    Root->SetArrayField(TEXT("appendFragments"), Fragments);
    FString Text;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
    const FString Output = FPaths::ProjectSavedDir() / TEXT("GAAuthoring/GraphExpansion/20261122/capabilities.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
    TestTrue(TEXT("Write the bounded read-only capability audit"),
             FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Actual registry and configured catalog are nonempty"), Types.Num() > 0 && Definitions.Num() > 0);
    return true;
}
#endif
