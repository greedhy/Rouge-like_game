// 自动化链：真实临时世界/ASC -> 编译或类型化查询图 -> Mass 伤害服务 -> 实际生命值及来源失效断言。
#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityLegacyGraphCompiler.h"
#include "AbilitySystem/Unified/AbilityDamageService.h"
#include "AbilitySystem/Unified/Nodes/AbilityCombatNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Data/OrbitingFireBoltConfig.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassTypes.h"
#include "Actor/MyOrbitingFireBolt.h"
#include "Terrain/TerrainSurface.h"
#include "MyGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace AbilityCombatMigrationTests
{
    /** 构造新制作路线的“范围查询 -> 每个目标伤害”，根输入只需要权威来源与位置。 */
    UAbilityGraphDefinition* QueryDamage(UWorld* World)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        FAbilityGraphNode Query;
        Query.NodeId = FGuid::NewGuid();
        Query.TypeId = TEXT("Query.Shape");
        auto* QueryConfig = NewObject<UAbilityQueryNodeConfig>(Graph);
        QueryConfig->Query.Radius = 600.f;
        QueryConfig->Query.bRequireVisibility = false;
        QueryConfig->Query.bIncludeActors = false;
        Query.Config = QueryConfig;
        FAbilityGraphNode Damage;
        Damage.NodeId = FGuid::NewGuid();
        Damage.TypeId = TEXT("Effect.Damage");
        auto* DamageConfig = NewObject<UAbilityDamageNodeConfig>(Graph);
        DamageConfig->Damage = 25.f;
        DamageConfig->DamagePerLevel = 0.f;
        DamageConfig->DamageType = FMyGameplayTags::Get().Damage_Magic;
        Damage.Config = DamageConfig;
        Graph->EntryNodeId = Query.NodeId;
        Graph->Nodes = {Query, Damage};
        FAbilityGraphEdge Edge;
        Edge.From = Query.NodeId;
        Edge.To = Damage.NodeId;
        Edge.OutputPort = TEXT("TargetFound");
        Graph->Edges.Add(Edge);
        return Graph;
    }
} // namespace AbilityCombatMigrationTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityQueryDamageMigration, "test_25d.UnifiedGA.Combat.QueryAppliesMassDamageOnce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAbilityQueryDamageMigration::RunTest(const FString& Parameters)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 First = Mass->SpawnMonster(Type, FVector(300, 100, 0));
    const int32 Second = Mass->SpawnMonster(Type, FVector(400, 100, 0));
    TestTrue(TEXT("Both live targets exist"), First > 0 && Second > 0);
    auto* Graph = AbilityCombatMigrationTests::QueryDamage(F.World);
    auto* Instance = NewObject<UAbilityGraphInstance>(F.World);
    FAbilityNodeInput Input;
    Input.SourceToken = F.SourceToken;
    Input.Origin = F.Source->GetActorLocation();
    FAbilityExecutionOptions Options;
    FString Error;
    TestTrue(*Error, Instance->StartWithInput(Graph, Input, Options, Error));
    Instance->Advance(.01f);
    FPureMassView FirstView, SecondView;
    TestTrue(TEXT("First still live"), Mass->Resolve(First, FirstView));
    TestTrue(TEXT("Second still live"), Mass->Resolve(Second, SecondView));
    TestEqual(TEXT("First resolved damage once"), FirstView.Health, 75.f);
    TestEqual(TEXT("Second resolved damage once"), SecondView.Health, 75.f);
    TestTrue(TEXT("Graph released all invocations"), Instance->IsFinished());
    Instance->Advance(1.f);
    Mass->Resolve(First, FirstView);
    TestEqual(TEXT("Finished execution cannot hit again"), FirstView.Health, 75.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityDamageLifeMigration,
                                 "test_25d.UnifiedGA.Combat.DamageRejectsStaleSourceAndReportsKill",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAbilityDamageLifeMigration::RunTest(const FString& Parameters)
{
    FAbilityGraphTestFixture F;
    auto* Mass = F.World->GetSubsystem<UPureMassSubsystem>();
    Mass->Enable();
    auto* Type = NewObject<UPureMassMonsterType>(F.World);
    Type->Health = 100.f;
    Type->XPReward = 0;
    const int32 Id = Mass->SpawnMonster(Type, FVector(400, 100, 0));
    FAbilityTargetToken Target;
    FString Error;
    TestTrue(TEXT("Capture real entity"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Id), Target, Error));
    FDamageEffectParams Damage;
    Damage.BaseDamage = 1000.f;
    Damage.DamageType = FMyGameplayTags::Get().Damage_Magic;
    const auto Result = AbilityDamageService::Apply(F.SourceToken, Target, Damage);
    TestTrue(TEXT("Kill confirmed by removal reason"), Result.bAttempted && Result.bKilled);
    TestTrue(TEXT("Actual damage is observed"), Result.ActualDamage.IsSet() && Result.ActualDamage.GetValue() > 0.f);
    TestFalse(TEXT("Old entity token cannot apply again"),
              AbilityDamageService::Apply(F.SourceToken, Target, Damage).bAttempted);
    const int32 Next = Mass->SpawnMonster(Type, FVector(450, 100, 0));
    TestTrue(TEXT("Capture next entity"),
             AbilityExecutionIdentity::CaptureTarget(F.SourceToken, FPureMassCombatTarget(Next), Target, Error));
    F.ASC->ClearActorInfo();
    TestFalse(TEXT("Rebound source cannot charge old cast damage"),
              AbilityDamageService::Apply(F.SourceToken, Target, Damage).bAttempted);
    FPureMassView View;
    Mass->Resolve(Next, View);
    TestEqual(TEXT("Rejected damage preserves health"), View.Health, 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityLegacyCompilerCoverage,
                                 "test_25d.UnifiedGA.Migration.AllKindsCompileAndCyclesUnroll",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAbilityLegacyCompilerCoverage::RunTest(const FString& Parameters)
{
    auto* Child = NewObject<UAbilityPrototype>();
    Child->Kind = EAbilityPrototypeKind::Burst;
    Child->DamageType = FMyGameplayTags::Get().Damage_Magic;
    Child->Cooldown = 0.f;
    FString Error;
    for (int64 Kind = 0; Kind <= static_cast<int64>(EAbilityPrototypeKind::Line); ++Kind)
    {
        auto* Config = DuplicateObject<UAbilityPrototype>(Child, GetTransientPackage());
        Config->Kind = static_cast<EAbilityPrototypeKind>(Kind);
        if (Config->Kind == EAbilityPrototypeKind::Emitter)
        {
            Config->OnPeriod = Child;
        }
        if (Config->Kind == EAbilityPrototypeKind::Delay)
        {
            Config->OnExpire = Child;
        }
        if (Config->Kind == EAbilityPrototypeKind::Surface)
        {
            Config->SurfaceClass = ATerrainSurface::StaticClass();
        }
        if (Config->Kind == EAbilityPrototypeKind::Orbit)
        {
            Config->OrbitSettings = NewObject<UOrbitingFireBoltConfig>(Config);
            Config->OrbitSettings->FireBoltClass = AMyOrbitingFireBolt::StaticClass();
        }
        for (FName Profile : {FName(TEXT("LegacyActor.v1")), FName(TEXT("LegacyMixed.v1"))})
        {
            auto* Graph = UAbilityLegacyGraphCompiler::Compile(GetTransientPackage(), Config, Profile, Error);
            TestNotNull(*FString::Printf(TEXT("Kind %lld %s: %s"), Kind, *Profile.ToString(), *Error), Graph);
        }
    }
    Child->OnHit = Child;
    Child->OnExpire = Child;
    auto* Cyclic = UAbilityLegacyGraphCompiler::Compile(GetTransientPackage(), Child, TEXT("LegacyMixed.v1"), Error);
    TestNotNull(*Error, Cyclic);
    if (Cyclic)
    {
        TestEqual(TEXT("Depth-truncated cycle has four definitions"), Cyclic->Nodes.Num(), 4);
        Cyclic->Nodes[0].TypeId = TEXT("Legacy.Mixed.Delay");
        TArray<FString> Errors;
        TestFalse(TEXT("Shared config class cannot disguise a different legacy kind"),
                  Cyclic->ValidateForAuthoring(Errors));
        TestFalse(TEXT("Kind mismatch carries a diagnostic"), Errors.IsEmpty());
    }
    return true;
}
#endif
