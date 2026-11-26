// 测试链：原生地表/驱散图 -> 运行时参数补丁 -> 真实租约与 Removed 位置事件。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/WorkshopDispelAbilities.h"
#include "AbilitySystem/Unified/WorkshopTerrainAbilities.h"
#include "EngineUtils.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    /** 用实际默认图和配方补丁执行同步地表动作，可检查分支发布的实际地块中心。 */
    bool RunSurfaceRecipe(UWorld* World, UAbilityGraphDefinition* Base, const FAbilitySourceToken& Source,
                          FVector Origin, const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                          TArray<FVector>* RemovedLocations, FString& Error)
    {
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!AbilityRuntimeRecipes::Compile(World, Base, Contributions, Compiled, Error))
        {
            return false;
        }
        auto* Execution = NewObject<UAbilityGraphInstance>(World);
        if (RemovedLocations)
        {
            RemovedLocations->Reset();
            Execution->OnEvent.AddLambda(
                [RemovedLocations](const FAbilityGraphEvent& Event)
                {
                    if (Event.NodeType == TEXT("World.DispelSurface") && Event.OutputPort == TEXT("Removed"))
                    {
                        RemovedLocations->Add(Event.Input.Origin);
                    }
                });
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Source;
        Input.Origin = Input.EventCenter = Origin;
        const bool bCompleted = Execution->StartWithInput(Compiled, Input, {}, Error) && Execution->IsFinished() &&
                                Execution->GetEndReason() == EAbilityGraphEndReason::Completed;
        Execution->OnEvent.Clear();
        return bCompleted;
    }

    /** 测试通过和工作台一致的参数操作设置节点，编译器继续验证同一轴的覆盖。 */
    FAbilityRuntimeRecipePatch SetTerrainParameter(const UAbilityGraphDefinition& Base,
                                                   EAbilityConfigParameter Parameter, float Value)
    {
        FAbilityRuntimeRecipePatch Patch;
        Patch.TargetNodeId = Base.EntryNodeId;
        Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        Patch.Parameter = Parameter;
        Patch.Scalar = Value;
        return Patch;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTerrainDispelSelection, "test_25d.UnifiedGA.WorkshopTerrain.DispelSelection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopTerrainDispelSelection::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    auto* Surface = GetDefault<UWorkshopVenomMire>()->Graph.Get();
    auto* Dispel = GetDefault<UWorkshopTerrainDispel>()->Graph.Get();
    const FVector Origin(100.f, 250.f, 0.f);
    const FVector Near = Origin + FVector(90.f, 0.f, 0.f);
    const FVector Far = Origin + FVector(450.f, 0.f, 0.f);
    const FVector Frost = Origin + FVector(0.f, 120.f, 0.f);
    const FVector Above = Origin + FVector(-100.f, 0.f, 200.f);
    FString Error;

    auto* MapTile = Fixture.World->SpawnActor<ATerrainSurface>(Origin, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Same-owner map terrain without a workshop lease"), MapTile))
    {
        return false;
    }
    MapTile->SetOwner(Fixture.Source);
    MapTile->Config = NewObject<UTerrainSurfaceConfig>(MapTile);

    FAbilitySourceToken OtherSource;
    if (!TestTrue(TEXT("A different caster has a valid life"),
                  AbilityExecutionIdentity::CaptureSource(Fixture.Target, OtherSource, Error)) ||
        !TestTrue(TEXT("The different caster places terrain at the same ground point"),
                  RunSurfaceRecipe(Fixture.World, Surface, OtherSource, Origin, {}, nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }
    for (const FVector Position : {Near, Far, Above})
    {
        if (!TestTrue(TEXT("The caster places owned toxic terrain"),
                      RunSurfaceRecipe(Fixture.World, Surface, Fixture.SourceToken, Position, {}, nullptr, Error)))
        {
            AddInfo(Error);
            return false;
        }
    }

    FAbilityRuntimeRecipeContribution FrostPatch;
    FrostPatch.SourceId = TEXT("Terrain.Test.Frost");
    FrostPatch.Patches.Add(SetTerrainParameter(*Surface, EAbilityConfigParameter::TerrainSurfaceKind, 1.f));
    if (!TestTrue(TEXT("Kind editing places a real frost patch"),
                  RunSurfaceRecipe(Fixture.World, Surface, Fixture.SourceToken, Frost, {FrostPatch}, nullptr, Error)))
    {
        AddInfo(Error);
        return false;
    }

    TArray<FVector> Removed;
    FAbilityRuntimeRecipeContribution FrostOnly;
    FrostOnly.SourceId = TEXT("Terrain.Test.DispelFrost");
    FrostOnly.Patches.Add(SetTerrainParameter(*Dispel, EAbilityConfigParameter::TerrainSurfaceKind, 1.f));
    if (TestTrue(TEXT("The edited dispel selects by kind"),
                 RunSurfaceRecipe(Fixture.World, Dispel, Fixture.SourceToken, Origin, {FrostOnly}, &Removed, Error)))
    {
        TestTrue(TEXT("The closer toxic patch was skipped and the frost center was published"),
                 Removed.Num() == 1 && Removed[0].Equals(Frost));
    }

    FAbilityRuntimeRecipeContribution LongSingle;
    LongSingle.SourceId = TEXT("Terrain.Test.DispelLongSingle");
    LongSingle.Patches.Add(SetTerrainParameter(*Dispel, EAbilityConfigParameter::Count, 1.f));
    auto& Range = LongSingle.Patches.AddDefaulted_GetRef();
    Range.TargetNodeId = Dispel->EntryNodeId;
    Range.Operation = EAbilityRecipePatchOperation::RadiusScale;
    Range.Scalar = 1.5f;
    if (TestTrue(TEXT("Radius and limit can be edited independently"),
                 RunSurfaceRecipe(Fixture.World, Dispel, Fixture.SourceToken, Origin, {LongSingle}, &Removed, Error)))
    {
        TestTrue(TEXT("Limit one removes the nearer patch and publishes its center"),
                 Removed.Num() == 1 && Removed[0].Equals(Near));
    }
    if (TestTrue(TEXT("Default dispel completes with no eligible owned patch in range"),
                 RunSurfaceRecipe(Fixture.World, Dispel, Fixture.SourceToken, Origin, {}, &Removed, Error)))
    {
        TestTrue(TEXT("Out-of-range and different-floor patches publish no removal"), Removed.IsEmpty());
    }
    if (TestTrue(TEXT("The extended map query reaches the remaining eligible patch"),
                 RunSurfaceRecipe(Fixture.World, Dispel, Fixture.SourceToken, Origin, {LongSingle}, &Removed, Error)))
    {
        TestTrue(TEXT("Only the distant owned patch was removed"), Removed.Num() == 1 && Removed[0].Equals(Far));
    }
    bool bOtherCasterTileAlive = false;
    bool bUpperFloorTileAlive = false;
    for (TActorIterator<ATerrainSurface> It(Fixture.World); It; ++It)
    {
        if (!It->IsActorBeingDestroyed() && It->bEnabled)
        {
            bOtherCasterTileAlive |= It->GetOwner() == Fixture.Target && It->GetActorLocation().Equals(Origin);
            bUpperFloorTileAlive |= It->GetOwner() == Fixture.Source && It->GetActorLocation().Equals(Above);
        }
    }
    TestTrue(TEXT("Another player's patch remains alive and enabled"), bOtherCasterTileAlive);
    TestTrue(TEXT("The same caster's higher-floor patch remains alive and enabled"), bUpperFloorTileAlive);
    TestTrue(TEXT("Authored terrain remains alive and enabled"), IsValid(MapTile) && MapTile->bEnabled);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
