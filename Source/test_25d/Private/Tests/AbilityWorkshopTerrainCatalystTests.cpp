// 测试链：真实地表租约 → 默认图/类型化补丁 → 逐次查找 → Mass 伤害及元素状态。
#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    /** 从已注册原生默认图读取配置，使缺少技能定义时直接暴露测试失败。 */
    UAbilityGraphDefinition* FindCatalystGraph(const TCHAR* ClassPath)
    {
        const auto* AbilityClass = FindObject<UClass>(nullptr, ClassPath);
        const auto* Ability = AbilityClass ? AbilityClass->GetDefaultObject<UWorkshopUtilityAbility>() : nullptr;
        return Ability ? Ability->Graph.Get() : nullptr;
    }

    /** 通过原地表节点布置真实租约，不绕过 Actor 类型与搜索服务。 */
    TWeakObjectPtr<ATerrainSurface> CreateCatalystTerrain(const GraphGameplayExpansionTests::FFixture& Fixture,
                                                          ETerrainSurfaceType Kind, const FVector& Point,
                                                          FString& Error)
    {
        auto* Graph = NewObject<UAbilityGraphDefinition>(Fixture.World);
        Graph->SchemaVersion = 2;
        Graph->CompatibilityProfile = TEXT("UnifiedV2");
        auto* Config = NewObject<UAbilityElementSurfaceNodeConfig>(Graph);
        Config->Kind = Kind;
        Config->Radius = 35.f;
        Config->Duration = 8.f;
        Graph->EntryNodeId = Fixture.Add(*Graph, TEXT("World.ElementSurface"), Config);

        FAbilityNodeInput Input;
        Input.SourceToken = Fixture.SourceToken;
        Input.Origin = Input.EventCenter = Point;
        auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
        if (!Run->StartWithInput(Graph, Input, {}, Error))
        {
            return {};
        }
        const auto Tiles = AbilityWorkshopTerrainNodes::FindOwnedSurfaces(*Fixture.Source, Point, 25.f, Kind, 1);
        return Tiles.IsEmpty() ? TWeakObjectPtr<ATerrainSurface>() : Tiles[0];
    }

    /** 测试用独立帧，避免执行器的同帧推进防重入影响节拍断言。 */
    void AdvanceCatalystPulse(UAbilityGraphInstance& Run, float Seconds)
    {
        ++GFrameCounter;
        Run.Advance(Seconds);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopStormCatalystLeases, "test_25d.UnifiedGA.WorkshopTerrainCatalyst.StormLeases",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 验证从真实地面而非瞄准点结算。移除一块后才重新选择另一块，两块都消失则无后继。 */
bool FWorkshopStormCatalystLeases::RunTest(const FString&)
{
    auto* Graph = FindCatalystGraph(TEXT("/Script/test_25d.WorkshopStormCatalyst"));
    if (!TestNotNull(TEXT("Storm Catalyst native definition exists"), Graph))
    {
        return false;
    }
    GraphGameplayExpansionTests::FFixture Fixture;
    const FVector Aim(100, 0, 0);
    const FVector First(470, 0, 0);
    const FVector Second(100, 410, 0);
    FString Error;
    const auto Near = CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Conductive, First, Error);
    const auto Far = CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Conductive, Second, Error);
    const auto WrongKind = CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Frost, Aim, Error);
    const auto AnotherFloor =
        CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Conductive, Aim + FVector(0, 0, 300), Error);
    if (!TestTrue(TEXT("Actual ground leases exist for each search case"),
                  Near.IsValid() && Far.IsValid() && WrongKind.IsValid() && AnotherFloor.IsValid()))
    {
        AddInfo(Error);
        return false;
    }
    const int32 AtFirst = Fixture.Spawn(First);
    const int32 AtSecond = Fixture.Spawn(Second);
    const int32 AtAim = Fixture.Spawn(Aim);
    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    Input.Origin = Input.EventCenter = Aim;
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("The authority executes the default storm graph"), Run->StartWithInput(Graph, Input, {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("The first pulse uses the nearest qualified ground center"), Fixture.Health(AtFirst), 188.f);
    TestEqual(TEXT("No damage to nearby aim or different-kind ground"), Fixture.Health(AtAim), 200.f);
    TestEqual(TEXT("The second tile is untouched while the nearer one still exists"), Fixture.Health(AtSecond), 200.f);
    TestTrue(TEXT("Relay leaves the original terrain active"), Near.IsValid() && !Near->IsActorBeingDestroyed());
    if (!TestTrue(TEXT("Retire the original terrain between pulses"),
                  Near->FindComponentByClass<UAbilityWorkshopTerrainLease>()->TryDispelFor(*Fixture.Source)))
    {
        return false;
    }
    AdvanceCatalystPulse(*Run, .61f);
    TestEqual(TEXT("The next pulse performs a fresh same-floor lookup"), Fixture.Health(AtSecond), 188.f);
    TestEqual(TEXT("The retired lease cannot continue dealing damage"), Fixture.Health(AtFirst), 188.f);
    if (!TestTrue(TEXT("Retire the only other qualified lease"),
                  Far->FindComponentByClass<UAbilityWorkshopTerrainLease>()->TryDispelFor(*Fixture.Source)))
    {
        return false;
    }
    AdvanceCatalystPulse(*Run, .61f);
    TestEqual(TEXT("Without a matching active tile the last pulse has no effect"), Fixture.Health(AtAim), 200.f);
    TestEqual(TEXT("The second tile cannot continue dealing damage after retirement"), Fixture.Health(AtSecond), 188.f);
    TestTrue(TEXT("The finite cadence still finishes normally without a tile"),
             Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopFrostCatalystRetune, "test_25d.UnifiedGA.WorkshopTerrainCatalyst.FrostRetune",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 改装来源类型与搜索范围后，转至水地中继并由原冰元素反应累计冻结。 */
bool FWorkshopFrostCatalystRetune::RunTest(const FString&)
{
    const auto* Base = FindCatalystGraph(TEXT("/Script/test_25d.WorkshopFrostCatalyst"));
    if (!TestNotNull(TEXT("Frost Catalyst native definition exists"), Base))
    {
        return false;
    }
    const auto* Selector = Base->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("World.FindOwnedSurface");
        });
    if (!TestNotNull(TEXT("Frost uses the independent terrain selector"), Selector))
    {
        return false;
    }
    GraphGameplayExpansionTests::FFixture Fixture;
    const FVector Aim(100, 0, 0);
    const FVector WaterPoint(620, 0, 0);
    FString Error;
    const auto Water = CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Water, WaterPoint, Error);
    const auto Frost = CreateCatalystTerrain(Fixture, ETerrainSurfaceType::Frost, Aim, Error);
    if (!TestTrue(TEXT("Both real surface types are present"), Water.IsValid() && Frost.IsValid()))
    {
        AddInfo(Error);
        return false;
    }
    const int32 AtWater = Fixture.Spawn(WaterPoint);
    const int32 AtFrost = Fixture.Spawn(Aim);
    FAbilityRuntimeRecipeContribution Edit;
    Edit.SourceId = TEXT("Test.FrostCatalyst.Terrain");
    const TPair<EAbilityConfigParameter, float> Parameters[] = {
        {EAbilityConfigParameter::TerrainInputKind, static_cast<float>(ETerrainSurfaceType::Water)},
        {EAbilityConfigParameter::Radius, 630.f}};
    for (const auto& Parameter : Parameters)
    {
        auto& Patch = Edit.Patches.AddDefaulted_GetRef();
        Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        Patch.TargetNodeId = Selector->NodeId;
        Patch.Parameter = Parameter.Key;
        Patch.Scalar = Parameter.Value;
    }
    UAbilityGraphDefinition* Graph = nullptr;
    if (!TestTrue(TEXT("Changing the relay input kind and search separately compiles"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Edit}, Graph, Error)))
    {
        AddInfo(Error);
        return false;
    }

    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    Input.Origin = Input.EventCenter = Aim;
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("The authority executes the retuned frost graph"), Run->StartWithInput(Graph, Input, {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Ice exposure is centered at the newly valid Water tile"),
              Fixture.Mass->GetElementalExposure(AtWater), EAbilityExposure::Chilled);
    TestEqual(TEXT("The original Frost center is not an allowed fallback"), Fixture.Mass->GetElementalExposure(AtFrost),
              EAbilityExposure::None);
    AdvanceCatalystPulse(*Run, .61f);
    TestEqual(TEXT("The existing ice status reaction freezes after the next pulse"),
              Fixture.Mass->GetElementalExposure(AtWater), EAbilityExposure::Frozen);
    TestEqual(TEXT("The frost relay adds control without dealing damage"), Fixture.Health(AtWater), 200.f);
    TestTrue(TEXT("The relay never consumes either terrain lease"),
             Water.IsValid() && !Water->IsActorBeingDestroyed() && Frost.IsValid() && !Frost->IsActorBeingDestroyed());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
