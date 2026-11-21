// 调用链：三个实际目录Mage过滤 -> 九个Unified CDO实际Graph -> 私有规范化 -> 原预算/入口/寿命proof。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/AbilityAuthoredGraphFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FActualMageNineNormalizationProof,
    "test_25d.UnifiedGA.DynamicMage.OriginalProjection.ActualMageNineKeepSourceProofAndBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FActualMageNineNormalizationProof::RunTest(const FString&)
{
    TSet<UClass*> Classes;
    for (const TCHAR* Path : {TEXT("/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo.DA_AbilityInfo"),
                              TEXT("/Game/PureMass/DA_MassAbilityInfo.DA_MassAbilityInfo"),
                              TEXT("/Game/PrototypeExtensions/Batch5/DA_AbilityInfo.DA_AbilityInfo")})
    {
        const auto* Catalog = LoadObject<UAbilityInfo>(nullptr, Path);
        if (!TestNotNull(TEXT("Actual catalog is available"), Catalog))
        {
            return false;
        }
        for (const auto& Row : Catalog->AbilityInformation)
        {
            if (Row.RequiredProfession == EPlayerProfession::Mage && Row.Ability &&
                Row.Ability->IsChildOf(UUnifiedGraphAbility::StaticClass()))
            {
                Classes.Add(Row.Ability.Get());
            }
        }
    }
    TestEqual(TEXT("Only actual Mage classes are selected; Stander Art and related sample stay outside"), Classes.Num(),
              9);
    int32 Expanded = 0;
    int32 Native = 0;
    for (auto* Class : Classes)
    {
        const auto* Ability = Class->GetDefaultObject<UUnifiedGraphAbility>();
        const auto* Original = Ability ? Ability->Graph.Get() : nullptr;
        if (!TestNotNull(TEXT("Actual class has its own configured Graph: ") + Class->GetPathName(), Original))
        {
            return false;
        }
        FString Error;
        auto* Result = AbilityAuthoredGraphFineCompiler::Compile(GetTransientPackage(), Original, Error);
        if (!TestNotNull(TEXT("Actual Mage graph normalizes: ") + Class->GetName() + TEXT(": ") + Error, Result))
        {
            continue;
        }
        const bool bMacro = Original->Nodes.ContainsByPredicate(
            [](const auto& Node)
            {
                return Node.Config->IsA<UAbilityLegacyNodeConfig>();
            });
        bMacro ? ++Expanded : ++Native;
        TestEqual(TEXT("Source active32 is kept verbatim"), Result->Budget.MaxActiveNodes,
                  Original->Budget.MaxActiveNodes);
        TestEqual(TEXT("Source invocation64 is kept verbatim"), Result->Budget.MaxInvocations,
                  Original->Budget.MaxInvocations);
        TestEqual(TEXT("Source accumulated event256 is kept verbatim"), Result->Budget.MaxEvents,
                  Original->Budget.MaxEvents);
        TestEqual(TEXT("Source peractualFrame64 is kept verbatim"), Result->Budget.MaxEventsPerTick,
                  Original->Budget.MaxEventsPerTick);
        TestEqual(TEXT("Source original depth8 is kept verbatim"), Result->Budget.MaxDepth, Original->Budget.MaxDepth);
        for (const auto& Node : Result->Nodes)
        {
            TestFalse(TEXT("Whole old macro is absent from the actual result"),
                      Node.Config->IsA<UAbilityLegacyNodeConfig>());
        }
        if (bMacro)
        {
            const auto& Proof = Result->OriginalProjection;
            TestTrue(TEXT("Derived fine profile does not erase original budget policy"), Proof.bEnabled);
            TestEqual(TEXT("Original Schema retained"), Proof.SchemaVersion, Original->SchemaVersion);
            TestEqual(TEXT("Original UnifiedV2 retained"), Proof.CompatibilityProfile, Original->CompatibilityProfile);
            TestEqual(TEXT("Original Bounded policy retained"), Proof.BudgetPolicy, Original->BudgetPolicy);
            TestEqual(TEXT("Every actual original node has a distinct manifest ordinal"), Proof.Definitions.Num(),
                      Original->Nodes.Num());
            TArray<FString> Errors;
            TestTrue(TEXT("Registered parameter/main-lifetime/output proof validates"),
                     Result->ValidateForAuthoring(Errors));
            auto* Invalid = DuplicateObject(Result, GetTransientPackage());
            const int32 Entry = Invalid->Nodes.IndexOfByPredicate(
                [&](const auto& Node)
                {
                    return Node.NodeId == Invalid->EntryNodeId;
                });
            const auto Hash = AbilityRuntimeRecipes::Fingerprint(*Result);
            Invalid->Nodes[Entry].OriginalRole = EAbilityOriginalNodeRole::Helper;
            TestTrue(TEXT("Admission metadata is part of the semantic fingerprint"),
                     Hash != AbilityRuntimeRecipes::Fingerprint(*Invalid));
            TestFalse(TEXT("Helper cannot impersonate or erase the original entry"),
                      Invalid->ValidateForAuthoring(Errors));
        }
        else
        {
            TestEqual(TEXT("Native utility remains under its exact original policy"), Result->BudgetPolicy,
                      Original->BudgetPolicy);
            TestEqual(TEXT("Native utility keeps its original profile"), Result->CompatibilityProfile,
                      Original->CompatibilityProfile);
        }
    }
    TestEqual(TEXT("Seven actual coupled Mage graphs require macro expansion"), Expanded, 7);
    TestEqual(TEXT("Two native OwnedEffect graphs remain exact clones"), Native, 2);
    return true;
}
#endif
