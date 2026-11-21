// 调用链：Automation -> 运行时配方编译/来源事务 -> 内存 Schema2 图 -> 真实 Mass 伤害或配置快照。
// 测试不保存资产、不授予新 GA；验证动态生成定义、失败回滚和旧施法快照，而不是检查类存在。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeComponent.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/UnifiedGraphAbility.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "PureMass/PureMassSubsystem.h"
#include "MyGameplayTags.h"

namespace AbilityRuntimeRecipeTests
{
    /** 构造带真实注册伤害配置的内存图；不是预制组合资产。 */
    UAbilityGraphDefinition* DamageGraph(UObject* Outer, float Damage)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Outer);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        auto* Config = NewObject<UAbilityDamageNodeConfig>(Graph);
        Config->Damage = Damage;
        Config->DamagePerLevel = 0.f;
        Config->DamageType = FMyGameplayTags::Get().Damage_Magic;
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Effect.Damage");
        Node.Config = Config;
        Graph->Nodes.Add(Node);
        Graph->EntryNodeId = Node.NodeId;
        return Graph;
    }

    /** 原技能带一个半径查询和实际伤害；追加时用于验证片段拓扑与参数均可独立生成。 */
    UAbilityGraphDefinition* AreaGraph(UObject* Outer, float Damage)
    {
        auto* Graph = DamageGraph(Outer, Damage);
        auto* Query = NewObject<UAbilityQueryNodeConfig>(Graph);
        Query->Query.Radius = 250.f;
        Query->Query.bRequireVisibility = false;
        FAbilityGraphNode Node;
        Node.NodeId = FGuid::NewGuid();
        Node.TypeId = TEXT("Query.Shape");
        Node.Config = Query;
        FAbilityGraphEdge Edge;
        Edge.From = Node.NodeId;
        Edge.To = Graph->EntryNodeId;
        Edge.OutputPort = TEXT("TargetFound");
        Graph->EntryNodeId = Node.NodeId;
        Graph->Nodes.Insert(Node, 0);
        Graph->Edges.Add(Edge);
        return Graph;
    }

    /** 按来源创建一条参数修正；新图应从该配置复制，不持有调用者可变数组。 */
    FAbilityRuntimeRecipeContribution Scale(FName SourceId, float Value)
    {
        FAbilityRuntimeRecipeContribution Contribution;
        Contribution.SourceId = SourceId;
        FAbilityRuntimeRecipePatch Patch;
        Patch.TargetTypeId = TEXT("Effect.Damage");
        Patch.Operation = EAbilityRecipePatchOperation::DamageScale;
        Patch.Scalar = Value;
        Contribution.Patches.Add(Patch);
        return Contribution;
    }

    /** 组件由临时世界 Actor 拥有，遵循正式组件生命周期；不写入玩家数据。 */
    UAbilityRuntimeRecipeComponent* Component(AActor* Owner)
    {
        auto* Result = NewObject<UAbilityRuntimeRecipeComponent>(Owner);
        Owner->AddInstanceComponent(Result);
        Result->RegisterComponent();
        return Result;
    }

    /** 读取实际编译配置，验证旧快照仍独立；需要实际行为的用例另走 GraphInstance。 */
    float DamageOf(const UAbilityGraphDefinition* Graph)
    {
        if (!Graph)
        {
            return -1.f;
        }
        for (const auto& Node : Graph->Nodes)
        {
            if (const auto* Damage = Cast<UAbilityDamageNodeConfig>(Node.Config))
            {
                return Damage->Damage;
            }
        }
        return -1.f;
    }
} // namespace AbilityRuntimeRecipeTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeCompilesDamage,
                                 "test_25d.UnifiedGA.DynamicRecipe.TypedCompilationChangesRealDamage",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeCompilesDamage::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeTests;
    FAbilityGraphTestFixture F;
    F.Target->Destroy();
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 500.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(150, 0, 0));
    FString Error;
    FAbilityTargetToken Target;
    if (!TestTrue(TEXT("Original target life is captured"),
                  AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), Target, Error)))
    {
        return false;
    }
    auto* Base = DamageGraph(F.World, 25.f);
    UAbilityGraphDefinition* Compiled = nullptr;
    if (!TestTrue(TEXT("Runtime contribution compiles a new graph"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Scale(TEXT("Rune.Triple"), 3.f)}, Compiled, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("Compiled definition is a distinct transient object"), Compiled != Base);
    TestEqual(TEXT("Original graph damage remains untouched"), DamageOf(Base), 25.f);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.LiveTargetToken = Target;
    Input.Level = 1.f;
    auto* Run = NewObject<UAbilityGraphInstance>(F.World);
    TestTrue(TEXT("Generated graph executes through the existing executor"),
             Run->StartWithInput(Compiled, Input, {}, Error));
    FPureMassView View;
    if (TestTrue(TEXT("Nonlethal target remains present"), Mass->Resolve(Id, View)))
    {
        TestEqual(TEXT("Compiled damage removes 75 actual health"), View.Health, 425.f);
    }
    TestTrue(TEXT("Instant generated graph finishes"), Run->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeTransactions,
                                 "test_25d.UnifiedGA.DynamicRecipe.TransactionsIsolationAndSnapshots",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeTransactions::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeTests;
    FAbilityGraphTestFixture F;
    auto* One = Component(F.Source);
    auto* OtherOwner = F.World->SpawnActor<AActor>();
    auto* Two = Component(OtherOwner);
    const auto AbilityTag = FMyGameplayTags::Get().Abilities_Fire_FireBolt;
    auto* Base = DamageGraph(F.World, 25.f);
    FString Error;
    if (!TestTrue(TEXT("First player registers original definition"), One->RegisterBase(AbilityTag, Base, Error)) ||
        !TestTrue(TEXT("Second player registers same original definition"), Two->RegisterBase(AbilityTag, Base, Error)))
    {
        return false;
    }
    TestFalse(TEXT("Base registration is not an active gameplay modifier"), One->HasContributions(AbilityTag));
    TestTrue(TEXT("First source applies transactionally"),
             One->SetContribution(AbilityTag, Scale(TEXT("Rune.A"), 2.f), Error));
    const int32 Revision = One->GetRevision(AbilityTag);
    FAbilityRuntimeRecipeSnapshot OldCast;
    if (!TestTrue(TEXT("First cast receives independently owned definition"),
                  One->CreateCastSnapshot(AbilityTag, F.World, OldCast, Error)))
    {
        return false;
    }
    TestEqual(TEXT("First cast damage is 50"), DamageOf(OldCast.Graph), 50.f);
    TestEqual(TEXT("Snapshot records exact accepted revision"), OldCast.Revision, Revision);
    FAbilityRuntimeRecipeSnapshot OtherCast;
    TestTrue(TEXT("Second player remains on original definition"),
             Two->CreateCastSnapshot(AbilityTag, F.World, OtherCast, Error));
    TestEqual(TEXT("Other owner is not contaminated"), DamageOf(OtherCast.Graph), 25.f);
    TestFalse(TEXT("Invalid edit is rejected"), One->SetContribution(AbilityTag, Scale(TEXT("Rune.A"), -2.f), Error));
    TestFalse(TEXT("Invalid edit supplies readable error"), Error.IsEmpty());
    TestEqual(TEXT("Failed edit preserves current revision"), One->GetRevision(AbilityTag), Revision);
    TestTrue(TEXT("Same source replaces its contribution"),
             One->SetContribution(AbilityTag, Scale(TEXT("Rune.A"), 3.f), Error));
    FAbilityRuntimeRecipeSnapshot NewCast;
    TestTrue(TEXT("Later cast uses accepted replacement"),
             One->CreateCastSnapshot(AbilityTag, F.World, NewCast, Error));
    TestEqual(TEXT("Replacement does not multiply old source again"), DamageOf(NewCast.Graph), 75.f);
    TestEqual(TEXT("In-flight old cast keeps original snapshot"), DamageOf(OldCast.Graph), 50.f);
    TestTrue(TEXT("Deleting one source succeeds"), One->RemoveContribution(AbilityTag, TEXT("Rune.A"), Error));
    TestFalse(TEXT("No contribution restores the compatibility route"), One->HasContributions(AbilityTag));
    const int32 ClearedRevision = One->GetRevision(AbilityTag);
    TestTrue(TEXT("Deleting absent source is idempotent"), One->RemoveContribution(AbilityTag, TEXT("Rune.A"), Error));
    TestEqual(TEXT("Idempotent delete creates no spurious version"), One->GetRevision(AbilityTag), ClearedRevision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDeterministicBranches,
                                 "test_25d.UnifiedGA.DynamicRecipe.StructuralEditsAndDeterministicHash",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDeterministicBranches::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeTests;
    FAbilityGraphTestFixture F;
    auto* Base = AreaGraph(F.World, 25.f);
    auto* Branch = AreaGraph(F.World, 5.f);
    FAbilityRuntimeRecipeContribution Extra;
    Extra.SourceId = TEXT("Rune.AdditionalArea");
    FAbilityRuntimeRecipePatch Append;
    Append.Operation = EAbilityRecipePatchOperation::AppendBranch;
    Append.OutputPort = TEXT("Completed");
    Append.BranchGraph = Branch;
    Extra.Patches.Add(Append);
    UAbilityGraphDefinition* First = nullptr;
    UAbilityGraphDefinition* Second = nullptr;
    FString Error;
    const auto ScalePatch = Scale(TEXT("Rune.Scale"), 2.f);
    if (!TestTrue(TEXT("Graph fragments compile in runtime memory"),
                  AbilityRuntimeRecipes::Compile(F.World, Base, {Extra, ScalePatch}, First, Error)))
    {
        return false;
    }
    TestTrue(TEXT("Contribution input ordering does not alter the result"),
             AbilityRuntimeRecipes::Compile(F.World, Base, {ScalePatch, Extra}, Second, Error));
    TestEqual(TEXT("New branch contains its own nodes"), First->Nodes.Num(), 4);
    TestEqual(TEXT("Original branch has no new nodes"), Base->Nodes.Num(), 2);
    TestFalse(TEXT("Semantic configuration digest is populated"), AbilityRuntimeRecipes::Fingerprint(*First).IsEmpty());
    if (Second)
    {
        TestEqual(TEXT("Equivalent graph snapshots have identical semantic hashes"),
                  AbilityRuntimeRecipes::Fingerprint(*First), AbilityRuntimeRecipes::Fingerprint(*Second));
    }
    FAbilityRuntimeRecipeContribution Invalid = Extra;
    Invalid.Patches[0].OutputPort = TEXT("NeverRegistered");
    UAbilityGraphDefinition* Rejected = First;
    TestFalse(TEXT("Unknown output ports cannot schedule fragments"),
              AbilityRuntimeRecipes::Compile(F.World, Base, {Invalid}, Rejected, Error));
    TestNull(TEXT("Compile failure does not leak partially modified graph"), Rejected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipeDefaultsAndConflicts,
                                 "test_25d.UnifiedGA.DynamicRecipe.DefaultDefinitionsAndReplacementConflicts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipeDefaultsAndConflicts::RunTest(const FString&)
{
    using namespace AbilityRuntimeRecipeTests;
    FAbilityGraphTestFixture F;
    auto* Ability = NewObject<UUnifiedGraphAbility>(F.World);
    Ability->Graph = AreaGraph(F.World, 25.f);
    FString Error;
    auto* Default = AbilityRuntimeRecipes::BuildDefaultGraph(F.World, Ability, Error);
    if (!TestNotNull(TEXT("Existing graph GA supplies a default runtime definition"), Default))
    {
        return false;
    }
    TestTrue(TEXT("Default runtime graph is copied from original"), Default != Ability->Graph);
    FAbilityRuntimeRecipeContribution One;
    One.SourceId = TEXT("Rune.ReplaceOne");
    FAbilityRuntimeRecipePatch Replacement;
    Replacement.Operation = EAbilityRecipePatchOperation::ReplaceNode;
    Replacement.ReplacementTypeId = TEXT("Query.Shape");
    Replacement.ReplacementConfig = NewObject<UAbilityQueryNodeConfig>(F.World);
    One.Patches.Add(Replacement);
    FAbilityRuntimeRecipeContribution Two = One;
    Two.SourceId = TEXT("Rune.ReplaceTwo");
    UAbilityGraphDefinition* Rejected = nullptr;
    TestFalse(TEXT("Competing replacement rules cannot depend on traversal order"),
              AbilityRuntimeRecipes::Compile(F.World, Default, {One, Two}, Rejected, Error));
    TestFalse(TEXT("Replacement conflict has explicit diagnosis"), Error.IsEmpty());
    auto Missing = Scale(TEXT("Rune.NoTarget"), 2.f);
    Missing.Patches[0].TargetTypeId = TEXT("NoSuch.Type");
    TestFalse(TEXT("Unknown selector cannot silently become a no-op"),
              AbilityRuntimeRecipes::Compile(F.World, Default, {Missing}, Rejected, Error));
    return true;
}
#endif
