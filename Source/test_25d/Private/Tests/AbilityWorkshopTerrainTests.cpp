// 测试链：原生技能默认图 -> 运行时配方编译 -> GraphInstance -> 真实复制地表和服务器租约。
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityRuntimeModifierTemplates.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "AbilitySystem/Unified/WorkshopTerrainAbilities.h"
#include "EngineUtils.h"
#include "Terrain/TerrainSurface.h"

namespace
{
    /** 启动真实默认图，可先提交来自工作台同型的参数补丁。 */
    bool CastTerrainGraph(UWorld& World, UAbilityGraphDefinition* Base, const FAbilitySourceToken& Source,
                          FVector Location, const TArray<FAbilityRuntimeRecipeContribution>& Contributions,
                          FString& Error)
    {
        UAbilityGraphDefinition* Compiled = nullptr;
        if (!AbilityRuntimeRecipes::Compile(&World, Base, Contributions, Compiled, Error))
        {
            return false;
        }
        auto* Execution = NewObject<UAbilityGraphInstance>(&World);
        FAbilityNodeInput Input;
        Input.SourceToken = Source;
        Input.Origin = Input.EventCenter = Location;
        return Execution->StartWithInput(Compiled, Input, {}, Error) && Execution->IsFinished() &&
               Execution->GetEndReason() == EAbilityGraphEndReason::Completed;
    }

    /** 从实际世界读取活动地表，排除正在销毁的旧对象。 */
    ATerrainSurface* FindPatch(UWorld& World, AActor* Owner, ETerrainSurfaceType Kind)
    {
        for (TActorIterator<ATerrainSurface> It(&World); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && It->GetOwner() == Owner && It->Config &&
                It->Config->SurfaceType == Kind)
            {
                return *It;
            }
        }
        return nullptr;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTerrainOwnedConversion, "test_25d.UnifiedGA.WorkshopTerrain.OwnedConversion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopTerrainOwnedConversion::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* SurfaceGraph = GetDefault<UWorkshopVenomMire>()->Graph.Get();
    auto* IgnitionGraph = GetDefault<UWorkshopMireIgnition>()->Graph.Get();
    const FVector Origin(100.f, 250.f, 0.f);
    FString Error;

    auto* MapSurface = F.World->SpawnActor<ATerrainSurface>(Origin + FVector(20.f, 0.f, 0.f), FRotator::ZeroRotator);
    MapSurface->SetOwner(F.Source);
    MapSurface->Config = NewObject<UTerrainSurfaceConfig>(MapSurface);
    MapSurface->Config->SurfaceType = ETerrainSurfaceType::Toxic;

    FAbilityRuntimeRecipeContribution Edit;
    Edit.SourceId = TEXT("Terrain.Test.ShapeAndDuration");
    auto& Size = Edit.Patches.AddDefaulted_GetRef();
    Size.TargetNodeId = SurfaceGraph->EntryNodeId;
    Size.Operation = EAbilityRecipePatchOperation::RadiusScale;
    Size.Scalar = 1.25f;
    auto& Duration = Edit.Patches.AddDefaulted_GetRef();
    Duration.TargetNodeId = SurfaceGraph->EntryNodeId;
    Duration.Operation = EAbilityRecipePatchOperation::DurationScale;
    Duration.Scalar = 1.5f;
    if (!TestTrue(TEXT("The original terrain class can spawn after graph parameters are edited"),
                  CastTerrainGraph(*F.World, SurfaceGraph, F.SourceToken, Origin, {Edit}, Error)))
    {
        AddError(Error);
        return false;
    }
    ATerrainSurface* Poison = nullptr;
    for (TActorIterator<ATerrainSurface> It(F.World); It; ++It)
    {
        if (It->FindComponentByClass<UAbilityWorkshopTerrainLease>())
        {
            Poison = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("The completed cast leases the actual Toxic terrain"), Poison))
    {
        return false;
    }
    const auto* OriginalLease = Poison->FindComponentByClass<UAbilityWorkshopTerrainLease>();
    const double Deadline = OriginalLease->GetExpiry();
    TestTrue(TEXT("A finished graph leaves an active, finite square"), OriginalLease->IsActiveFor(*F.Source));
    TestTrue(TEXT("Radius editing changes the actual tile geometry"), Poison->SurfaceSize.Equals(FVector2D(525.f)));
    TestTrue(TEXT("Duration editing changes the actual world deadline"),
             FMath::IsNearlyEqual(Deadline - F.World->GetTimeSeconds(), 12.));

    FAbilitySourceToken OtherSource;
    if (!TestTrue(TEXT("The other caster has a different valid source life"),
                  AbilityExecutionIdentity::CaptureSource(F.Target, OtherSource, Error)))
    {
        return false;
    }
    TestTrue(TEXT("An unrelated caster cannot convert this patch"),
             CastTerrainGraph(*F.World, IgnitionGraph, OtherSource, Origin, {}, Error) &&
                 OriginalLease->IsActiveFor(*F.Source));
    TestTrue(TEXT("A different floor cannot convert this patch"),
             CastTerrainGraph(*F.World, IgnitionGraph, F.SourceToken, Origin + FVector(0.f, 0.f, 200.f), {}, Error) &&
                 OriginalLease->IsActiveFor(*F.Source));

    ++GFrameCounter;
    F.World->Tick(LEVELTICK_TimeOnly, .2f);
    if (!TestTrue(TEXT("The owner can ignite the patch after the creating graph finished"),
                  CastTerrainGraph(*F.World, IgnitionGraph, F.SourceToken, Origin, {}, Error)))
    {
        AddError(Error);
        return false;
    }
    auto* Fire = FindPatch(*F.World, F.Source, ETerrainSurfaceType::Lava);
    if (!TestNotNull(TEXT("The original Lava terrain class replaced Toxic"), Fire))
    {
        return false;
    }
    const auto* NewLease = Fire->FindComponentByClass<UAbilityWorkshopTerrainLease>();
    TestTrue(TEXT("Conversion carries the already-adjusted tile size and original expiry"),
             NewLease && FMath::IsNearlyEqual(NewLease->GetExpiry(), Deadline) &&
                 Fire->GetActorLocation().Equals(Origin) && Fire->SurfaceSize.Equals(FVector2D(525.f)));
    TestTrue(TEXT("Map terrain keeps its original kind"),
             IsValid(MapSurface) && MapSurface->Config->SurfaceType == ETerrainSurfaceType::Toxic);
    TestTrue(TEXT("The converted tile cannot be converted a second time"),
             CastTerrainGraph(*F.World, IgnitionGraph, F.SourceToken, Origin, {}, Error) &&
                 FindPatch(*F.World, F.Source, ETerrainSurfaceType::Lava) == Fire);

    F.Source->GetOnDeathDelegate().Broadcast(F.Source);
    TestNull(TEXT("Source death retires the replacement terrain immediately"),
             FindPatch(*F.World, F.Source, ETerrainSurfaceType::Lava));
    TestTrue(TEXT("Source death does not delete map terrain"), IsValid(MapSurface));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopTerrainFragmentPort, "test_25d.UnifiedGA.WorkshopTerrain.FragmentPort",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopTerrainFragmentPort::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto* Base = NewObject<UAbilityGraphDefinition>(F.World);
    Base->SchemaVersion = 2;
    Base->CompatibilityProfile = TEXT("UnifiedV2");
    Base->EntryNodeId = FGuid::NewGuid();
    auto* Delay = NewObject<UAbilityDelayNodeConfig>(Base);
    Base->Nodes = {{Base->EntryNodeId, TEXT("Control.DelayOrigin"), Delay}};

    FString Error;
    for (const FName Fragment : {FName(TEXT("World.ElementSurface")), FName(TEXT("World.IgniteSurface"))})
    {
        FAbilityRuntimeDeveloperEdit Edit;
        Edit.ModifierId = TEXT("Append.Registered");
        Edit.FragmentId = Fragment;
        Edit.OutputPortId = TEXT("Completed");
        Edit.Radius = 210.f;
        Edit.Value = 8.f;
        FAbilityRuntimeRecipePatch Patch;
        if (TestTrue(TEXT("A terrain fragment may be appended to a registered Origin output"),
                     AbilityRuntimeModifierTemplates::BuildPatch(F.World, Base->Nodes[0], Edit, Patch, Error, Base)))
        {
            FAbilityRuntimeRecipeContribution Change;
            Change.SourceId = TEXT("Terrain.Test.Append");
            Change.Patches.Add(Patch);
            UAbilityGraphDefinition* Compiled = nullptr;
            TestTrue(TEXT("The normal recipe compiler validates the terrain operation"),
                     AbilityRuntimeRecipes::Compile(F.World, Base, {Change}, Compiled, Error));
        }
        TestFalse(TEXT("A live target port cannot be used as a terrain position implicitly"),
                  AbilityRuntimeModifierTemplates::BuildPatch(F.World, F.Graph->Nodes[0], Edit, Patch, Error, F.Graph));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
