// 调用链：真实原生/蓝图法师 CDO -> 自身导出行为定义 -> 原注册表校验；不调用 K2 激活或保存包。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Ability/MyGameplayAbility.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeMageNativeDefinitions,
                                 "test_25d.UnifiedGA.DynamicMage.NativeFrontends.ExportOwnDefinition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeMageNativeDefinitions::RunTest(const FString&)
{
    struct FCase
    {
        const TCHAR* ClassPath;
        const TCHAR* Entry;
    };
    const FCase Cases[] = {
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"),
         TEXT("Prepare.NativeFireBolt")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C"),
         TEXT("Query.NovaTargets")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C"),
         TEXT("Control.ShardPoints")},
        {TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute.GA_Electrocute_C"),
         TEXT("Channel.ElectrocuteHost")},
        {TEXT(
             "/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C"),
         TEXT("Inventory.NativeOrbit")}};
    for (const auto& Case : Cases)
    {
        auto* Class = LoadClass<UMyGameplayAbility>(nullptr, Case.ClassPath);
        if (!TestNotNull(Case.ClassPath, Class))
        {
            continue;
        }
        FString Error;
        const auto* Ability = Class->GetDefaultObject<UMyGameplayAbility>();
        const TStrongObjectPtr<UAbilityGraphDefinition> Definition(
            Ability->BuildRuntimeBaseGraph(GetTransientPackage(), Error));
        if (!TestNotNull(FString(Case.ClassPath) + TEXT(" exports actual frontend behavior: ") + Error,
                         Definition.Get()))
        {
            continue;
        }
        TArray<FString> Errors;
        TestTrue(FString(Case.ClassPath) + TEXT(" definition validates"), Definition->ValidateForAuthoring(Errors));
        const auto* Entry = Definition->Nodes.FindByPredicate(
            [&](const FAbilityGraphNode& Node)
            {
                return Node.NodeId == Definition->EntryNodeId;
            });
        if (TestNotNull(TEXT("Exported definition has its actual entry"), Entry))
        {
            TestEqual(FString(Case.ClassPath) + TEXT(" preserves own strategy"), Entry->TypeId, FName(Case.Entry));
        }
    }
    return true;
}
#endif
