// 测试链：三个真实目录 -> 实际Extension导出图中的root/child Scope -> 原开发事务 -> 版本/参数原子性。
// 能力表按Behavior和single/area真实消费者定义，禁止借父类Kind或孩子的伤害宣称父片段支持某个轴。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "Misc/AutomationTest.h"

namespace AbilityExtensionScopeScalarTests
{
    enum : uint8
    {
        Damage = 1,
        Radius = 2,
        Duration = 4
    };

    struct FBehavior
    {
        const TCHAR* Name;
        EExtensionKind Kind;
        bool bSingle;
        uint8 Supported;
    };

    // Duration表示本动作实际时钟；Delay用ArmDelay，Stack用StackLifetime，Burst不持有Duration。
    const FBehavior Behaviors[] = {
        {TEXT("Attach"), EExtensionKind::Attach, false, Duration},
        {TEXT("Stack"), EExtensionKind::Stack, false, 0},
        {TEXT("DisplaceArea"), EExtensionKind::Displace, false, Radius | Duration},
        {TEXT("DisplaceSingle"), EExtensionKind::Displace, true, Duration},
        {TEXT("Projectile"), EExtensionKind::Projectile, false, Damage | Duration},
        {TEXT("Chain"), EExtensionKind::Chain, false, Damage | Radius | Duration},
        {TEXT("Burst"), EExtensionKind::Burst, false, Damage | Radius},
        {TEXT("Delay"), EExtensionKind::Delay, false, 0},
        {TEXT("Surface"), EExtensionKind::Surface, false, Radius | Duration},
    };

    struct FAxis
    {
        const TCHAR* Name;
        const TCHAR* Modifier;
        uint8 Bit;
    };
    const FAxis Axes[] = {{TEXT("Damage"), TEXT("Scale.Damage"), Damage},
                          {TEXT("Radius"), TEXT("Scale.Radius"), Radius},
                          {TEXT("Duration"), TEXT("Scale.Duration"), Duration}};

    /** 有些行为只存在原生组合的孩子中，扫描实际导出节点而非伪造一个父类Kind匹配的DA。 */
    const FAbilityRuntimeDeveloperDefinition* FindActualScope(FAbilityActualMageFrontendFixture& F,
                                                              const FBehavior& Behavior, int32& OutIndex,
                                                              FString& Error)
    {
        OutIndex = INDEX_NONE;
        if (!F.Developer->InitializeCatalog(nullptr, Error))
        {
            return nullptr;
        }
        for (const auto& Row : F.Developer->GetDefinitions())
        {
            if (!Row.Base.bSelectable || !Row.Base.DefaultGraph)
            {
                continue;
            }
            const int32 Index = Row.Base.DefaultGraph->Nodes.IndexOfByPredicate(
                [&](const FAbilityGraphNode& Node)
                {
                    const auto* Scope = Cast<UAbilityPrototypeScopeNodeConfig>(Node.Config);
                    const auto* Extension = Scope ? Cast<UExtensionNode>(Scope->Parameters) : nullptr;
                    return Extension && Scope->Profile == TEXT("PrototypeFine.Extension.v1") &&
                           Extension->Behavior == Behavior.Kind &&
                           (Behavior.Kind != EExtensionKind::Displace || Extension->bSingleTarget == Behavior.bSingle);
                });
            if (Index != INDEX_NONE)
            {
                OutIndex = Index;
                return &Row;
            }
        }
        Error = TEXT("Actual current catalogs contain no exported Scope for the requested Extension behavior.");
        return nullptr;
    }
} // namespace AbilityExtensionScopeScalarTests

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FExtensionScopeAxisContract,
                                  "test_25d.DynamicMage.ScopeScalars.ExtensionBehaviorMatrix",
                                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FExtensionScopeAxisContract::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    using namespace AbilityExtensionScopeScalarTests;
    for (const auto& Behavior : Behaviors)
    {
        for (const auto& Axis : Axes)
        {
            const FString Name = FString(Behavior.Name) + TEXT(".") + Axis.Name;
            Names.Add(Name);
            Commands.Add(Name);
        }
    }
}
bool FExtensionScopeAxisContract::RunTest(const FString& Parameters)
{
    using namespace AbilityExtensionScopeScalarTests;
    for (const auto& Behavior : Behaviors)
    {
        for (const auto& Axis : Axes)
        {
            if (Parameters != FString(Behavior.Name) + TEXT(".") + Axis.Name)
            {
                continue;
            }
            FAbilityActualMageFrontendFixture F;
            FString Error;
            int32 Index = INDEX_NONE;
            const auto* Row = FindActualScope(F, Behavior, Index, Error);
            if (!TestNotNull(TEXT("Real current graph contains the exact requested Extension root/child Scope"), Row) ||
                !TestTrue(TEXT("Actual real directory starts its own zero-edit preview"),
                          F.Developer->BeginPreview(Row->DefinitionId, Error)))
            {
                AddInfo(Error);
                return false;
            }
            const auto* Scope =
                CastChecked<UAbilityPrototypeScopeNodeConfig>(Row->Base.DefaultGraph->Nodes[Index].Config);
            const auto* Extension = CastChecked<UExtensionNode>(Scope->Parameters);
            AddInfo(TEXT("ActualFrontendClass=") + GetPathNameSafe(Row->Base.AbilityClass.Get()) + TEXT(" | Scope=") +
                    FString::FromInt(Index) + TEXT(" | OriginalDA=") + GetPathNameSafe(Scope->StableReference) +
                    TEXT(" | BehaviorAxis=") + Parameters +
                    TEXT(" | Gate=real ApplyEdit transaction, not frontend input"));
            const auto Before = F.Developer->GetDeveloperState();
            const FString BaseHash = AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph);
            const float OriginalDamage = Extension->Damage;
            const float OriginalRadius = Extension->Radius;
            const float OriginalDuration = Extension->Duration;
            FAbilityRuntimeRecipeSnapshot Initial;
            if (!TestTrue(TEXT("Original accepted snapshot is available before the attempted edit"),
                          F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, Initial, Error)))
            {
                return false;
            }
            FAbilityRuntimeDeveloperEdit Edit;
            Edit.DefinitionId = Row->DefinitionId;
            Edit.ModifierId = Axis.Modifier;
            Edit.NodeIndex = Index;
            Edit.Value = 2.f;
            Edit.ExpectedRevision = Before.Revision;
            Edit.ExpectedBaseHash = Row->BaseHash;
            const bool bAccepted = F.Developer->ApplyEdit(Edit, Error);
            const bool bSupported = (Behavior.Supported & Axis.Bit) != 0;
            TestEqual(TEXT("Only axes consumed by this exact Extension behavior are accepted"), bAccepted, bSupported);
            if (bSupported)
            {
                TestEqual(TEXT("Supported axis commits exactly one recipe revision"),
                          F.Developer->GetDeveloperState().Revision, Before.Revision + 1);
                TestTrue(TEXT("Supported axis changes the accepted snapshot hash"),
                         F.Developer->GetDeveloperState().ContentHash != Before.ContentHash);
                TestTrue(TEXT("Supported axis creates its one owned modifier source"),
                         F.Developer->HasContributions(Row->Base.AbilityTag));
            }
            else
            {
                TestEqual(TEXT("Rejected unsupported Extension axis preserves accepted revision"),
                          F.Developer->GetDeveloperState().Revision, Before.Revision);
                TestEqual(TEXT("Rejected unsupported Extension axis preserves accepted content"),
                          F.Developer->GetDeveloperState().ContentHash, Before.ContentHash);
                TestFalse(TEXT("Rejected unsupported Extension axis creates no hidden modifier source"),
                          F.Developer->HasContributions(Row->Base.AbilityTag));
                FAbilityRuntimeRecipeSnapshot After;
                if (TestTrue(TEXT("Original accepted snapshot survives unsupported edit rejection"),
                             F.Developer->CreateCastSnapshot(Row->Base.AbilityTag, F.World, After, Error)))
                {
                    TestEqual(TEXT("Rejected edit preserves all accepted Extension/child configuration"),
                              AbilityRuntimeRecipes::Fingerprint(*After.Graph),
                              AbilityRuntimeRecipes::Fingerprint(*Initial.Graph));
                }
            }
            TestEqual(TEXT("Neither accepted nor rejected edits modify actual original base identity"),
                      F.Developer->GetDeveloperState().BaseHash, Before.BaseHash);
            TestEqual(TEXT("Actual original Damage is immutable"), Extension->Damage, OriginalDamage);
            TestEqual(TEXT("Actual original Radius is immutable"), Extension->Radius, OriginalRadius);
            TestEqual(TEXT("Actual original Duration is immutable"), Extension->Duration, OriginalDuration);
            TestEqual(TEXT("Actual original graph and all child parameters stay immutable"),
                      AbilityRuntimeRecipes::Fingerprint(*Row->Base.DefaultGraph), BaseHash);
            F.Developer->EndPreview(Error);
            return true;
        }
    }
    return false;
}
#endif
