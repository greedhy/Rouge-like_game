// 测试链：原生技能默认图 -> 类型化配方编辑 -> GraphInstance -> 实际地表资产和来源租约。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopSurfaceConversionAbilities.h"
#include "AbilitySystem/Unified/WorkshopTerrainAbilities.h"
#include "EngineUtils.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    /** 只记录同步地表端口；运行结束或失败时立即移除观察者。 */
    struct FConversionSignals
    {
        TOptional<FVector> Transformed;
        int32 Completed = 0;
    };

    /** 使用真实图和种类配置编译，保留原生默认图以核对草稿隔离。 */
    FAbilityRuntimeRecipePatch SetParameter(const UAbilityGraphDefinition& Graph, EAbilityConfigParameter Parameter,
                                            float Value)
    {
        FAbilityRuntimeRecipePatch Patch;
        Patch.TargetNodeId = Graph.EntryNodeId;
        Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        Patch.Parameter = Parameter;
        Patch.Scalar = Value;
        return Patch;
    }

    /** 启动工作台实际执行快照，到达终态后收集用于后续逻辑的 Origin 输出。 */
    bool CastGround(UWorld& World, UAbilityGraphDefinition* Default, const FAbilitySourceToken& Source,
                    const FVector& Point, const TArray<FAbilityRuntimeRecipeContribution>& Changes, FString& Error,
                    FConversionSignals* Signals = nullptr)
    {
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!AbilityRuntimeRecipes::Compile(&World, Default, Changes, Compiled, Error))
        {
            return false;
        }

        auto* Execution = NewObject<UAbilityGraphInstance>(&World);
        FDelegateHandle Observer;
        if (Signals)
        {
            *Signals = {};
            Observer = Execution->OnEvent.AddLambda(
                [Signals](const FAbilityGraphEvent& Event)
                {
                    if (Event.OutputPort == TEXT("Transformed"))
                    {
                        Signals->Transformed = Event.Input.Origin;
                    }
                    if (Event.OutputPort == TEXT("Completed"))
                    {
                        ++Signals->Completed;
                    }
                });
        }
        FAbilityNodeInput Input;
        Input.SourceToken = Source;
        Input.Origin = Input.EventCenter = Point;
        const bool bCompleted = Execution->StartWithInput(Compiled, Input, {}, Error) && Execution->IsFinished() &&
                                Execution->GetEndReason() == EAbilityGraphEndReason::Completed;
        if (Observer.IsValid())
        {
            Execution->OnEvent.Remove(Observer);
        }
        return bCompleted;
    }

    /** 查找具体地块对象，而不是只检查图声称已完成。 */
    ATerrainSurface* FindPatch(UWorld& World, const AActor* Owner, ETerrainSurfaceType Kind, const FVector& Point)
    {
        for (TActorIterator<ATerrainSurface> It(&World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Owner && It->Config &&
                It->Config->SurfaceType == Kind && It->GetActorLocation().Equals(Point) &&
                It->FindComponentByClass<UAbilityWorkshopTerrainLease>())
            {
                return *It;
            }
        }
        return nullptr;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopSurfaceConversionChain, "test_25d.UnifiedGA.WorkshopTerrain.ConversionChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 在同一个真实地块上验证两个缺省配方、可编辑参数与跨 Run 的租约限制。 */
bool FWorkshopSurfaceConversionChain::RunTest(const FString&)
{
    FAbilityGraphTestFixture Fixture;
    UWorld& World = *Fixture.World;
    auto* Element = GetDefault<UWorkshopVenomMire>()->Graph.Get();
    auto* Freeze = GetDefault<UWorkshopLavaFreeze>()->Graph.Get();
    auto* Corrode = GetDefault<UWorkshopFrostCorrode>()->Graph.Get();
    const FVector Aim(1000.f, 250.f, 0.f);
    const FVector OwnedPoint = Aim + FVector(120.f, 0.f, 0.f);
    const FVector OtherPoint = Aim + FVector(40.f, 0.f, 0.f);
    FString Error;

    FAbilitySourceToken OtherSource;
    if (!TestTrue(TEXT("A foreign caster has its own current source"),
                  AbilityExecutionIdentity::CaptureSource(Fixture.Target, OtherSource, Error)))
    {
        AddError(Error);
        return false;
    }
    auto* MapSurface = World.SpawnActor<ATerrainSurface>(Aim, FRotator::ZeroRotator);
    MapSurface->SetOwner(Fixture.Source);
    MapSurface->Config = NewObject<UTerrainSurfaceConfig>(MapSurface);
    MapSurface->Config->SurfaceType = ETerrainSurfaceType::Lava;

    FAbilityRuntimeRecipeContribution SpawnLava;
    SpawnLava.SourceId = TEXT("SurfaceConversion.TestLava");
    SpawnLava.Patches = {SetParameter(*Element, EAbilityConfigParameter::TerrainSurfaceKind,
                                      static_cast<float>(ETerrainSurfaceType::Lava)),
                         SetParameter(*Element, EAbilityConfigParameter::Radius, 175.f)};
    if (!TestTrue(TEXT("Both casts create real Lava ground via the existing terrain effect"),
                  CastGround(World, Element, Fixture.SourceToken, OwnedPoint, {SpawnLava}, Error) &&
                      CastGround(World, Element, OtherSource, OtherPoint, {SpawnLava}, Error)))
    {
        AddError(Error);
        return false;
    }
    auto* Lava = FindPatch(World, Fixture.Source, ETerrainSurfaceType::Lava, OwnedPoint);
    auto* Foreign = FindPatch(World, Fixture.Target, ETerrainSurfaceType::Lava, OtherPoint);
    if (!TestNotNull(TEXT("The owned Lava ground has a workshop lease"), Lava) ||
        !TestNotNull(TEXT("The foreign Lava ground has a different lease"), Foreign))
    {
        return false;
    }
    Lava->SetActorRotation(FRotator(0.f, 37.f, 0.f));
    const FTransform OriginalTransform = Lava->GetActorTransform();
    const FVector2D OriginalSize = Lava->SurfaceSize;
    const double OriginalExpiry = Lava->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry();

    FAbilityRuntimeRecipeContribution NarrowSearch;
    NarrowSearch.SourceId = TEXT("SurfaceConversion.NarrowSearch");
    NarrowSearch.Patches = {SetParameter(*Freeze, EAbilityConfigParameter::Radius, 70.f)};
    FConversionSignals Signals;
    TestTrue(TEXT("Neither nearer foreign Lava nor a map-owned tile enter the edited radius"),
             CastGround(World, Freeze, Fixture.SourceToken, Aim, {NarrowSearch}, Error, &Signals) &&
                 !Signals.Transformed.IsSet() && Signals.Completed == 1 &&
                 FindPatch(World, Fixture.Source, ETerrainSurfaceType::Lava, OwnedPoint) == Lava);
    TestTrue(TEXT("A cast from another floor completes without a false success position"),
             CastGround(World, Freeze, Fixture.SourceToken, Aim + FVector(0.f, 0.f, 250.f), {}, Error, &Signals) &&
                 !Signals.Transformed.IsSet() && Signals.Completed == 1);

    ++GFrameCounter;
    World.Tick(LEVELTICK_TimeOnly, .2f);
    if (!TestTrue(TEXT("LavaFreeze publishes the actual tile center, distinct from the aim point"),
                  CastGround(World, Freeze, Fixture.SourceToken, Aim, {}, Error, &Signals) &&
                      Signals.Transformed.IsSet() && Signals.Transformed->Equals(OwnedPoint) && Signals.Completed == 1))
    {
        AddError(Error);
        return false;
    }
    const auto* Frost = FindPatch(World, Fixture.Source, ETerrainSurfaceType::Frost, OwnedPoint);
    TestTrue(TEXT("Lava is replaced by the real Frost terrain with exact transform, dimensions and expiry"),
             Frost && Frost->GetActorTransform().Equals(OriginalTransform) && Frost->SurfaceSize.Equals(OriginalSize) &&
                 Frost->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry() == OriginalExpiry &&
                 !FindPatch(World, Fixture.Source, ETerrainSurfaceType::Lava, OwnedPoint));
    TestTrue(TEXT("FrostCorrode carries that lease deadline into its actual Toxic terrain"),
             CastGround(World, Corrode, Fixture.SourceToken, Aim, {}, Error, &Signals) && Signals.Transformed.IsSet() &&
                 Signals.Transformed->Equals(OwnedPoint) && Signals.Completed == 1);
    const auto* Toxic = FindPatch(World, Fixture.Source, ETerrainSurfaceType::Toxic, OwnedPoint);
    TestTrue(TEXT("Repeated conversion never resets the deadline, and map and foreign ground are untouched"),
             Toxic && Toxic->GetActorTransform().Equals(OriginalTransform) && Toxic->SurfaceSize.Equals(OriginalSize) &&
                 Toxic->FindComponentByClass<UAbilityWorkshopTerrainLease>()->GetExpiry() == OriginalExpiry &&
                 !FindPatch(World, Fixture.Source, ETerrainSurfaceType::Frost, OwnedPoint) && IsValid(MapSurface) &&
                 MapSurface->Config->SurfaceType == ETerrainSurfaceType::Lava &&
                 FindPatch(World, Fixture.Target, ETerrainSurfaceType::Lava, OtherPoint) == Foreign);

    FAbilityRuntimeRecipeContribution Retarget;
    Retarget.SourceId = TEXT("SurfaceConversion.IndependentKinds");
    Retarget.Patches = {SetParameter(*Corrode, EAbilityConfigParameter::TerrainSurfaceKind,
                                     static_cast<float>(ETerrainSurfaceType::Lava)),
                        SetParameter(*Corrode, EAbilityConfigParameter::TerrainInputKind,
                                     static_cast<float>(ETerrainSurfaceType::Toxic))};
    TestTrue(TEXT("Both kind axes may be edited independently to convert Toxic back into Lava"),
             CastGround(World, Corrode, Fixture.SourceToken, Aim, {Retarget}, Error, &Signals) &&
                 Signals.Transformed.IsSet() && Signals.Completed == 1);
    const auto* RestoredLava = FindPatch(World, Fixture.Source, ETerrainSurfaceType::Lava, OwnedPoint);
    if (!TestNotNull(TEXT("The edited default graph created a real Lava patch with the original footprint"),
                     RestoredLava))
    {
        return false;
    }
    FAbilityRuntimeRecipeContribution SameKind;
    SameKind.SourceId = TEXT("SurfaceConversion.SameKind");
    SameKind.Patches = {SetParameter(*Freeze, EAbilityConfigParameter::TerrainInputKind,
                                     static_cast<float>(ETerrainSurfaceType::Frost))};
    TestFalse(TEXT("The recipe compiler refuses a conversion whose result equals its source kind"),
              CastGround(World, Freeze, Fixture.SourceToken, Aim, {SameKind}, Error));

    auto* Director = World.SpawnActor<ADungeonRunDirector>();
    Director->RunId = FGuid::NewGuid();
    FAbilitySourceToken CurrentRunSource;
    if (!TestTrue(TEXT("The same Avatar can capture a new Run source"),
                  AbilityExecutionIdentity::CaptureSource(Fixture.Source, CurrentRunSource, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("The identical Avatar in a new Run does not inherit the old terrain lease"),
             CastGround(World, Freeze, CurrentRunSource, Aim, {}, Error, &Signals) && !Signals.Transformed.IsSet() &&
                 Signals.Completed == 1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
