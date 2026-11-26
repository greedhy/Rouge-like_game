// 测试链：原生默认图与工作台参数补丁 -> 类型化位置 -> 传统图时钟和真实 Mass 伤害。
#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/WorkshopSpatialStormAbilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopThunderMarkBurstLocksOrigin,
                                 "test_25d.UnifiedGA.DynamicMage.SpatialStorm.ThunderMarkBurstLocksOrigin",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopThunderMarkBurstLocksOrigin::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture Fixture;
    const FVector MarkLocation(280, 0, 0);
    const int32 Carrier = Fixture.Spawn(MarkLocation);
    const int32 NearMark = Fixture.Spawn(MarkLocation + FVector(70, 0, 0));
    const int32 Unmarked = Fixture.Spawn(FVector(-80, 0, 0));
    if (!TestTrue(TEXT("Apply actual elemental exposure to the selected Mass target"),
                  Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Thunder, 1.f)) ||
        !TestEqual(TEXT("SelectTargets should observe a Shocked carrier"), Fixture.Mass->GetElementalExposure(Carrier),
                   EAbilityExposure::Shocked))
    {
        return false;
    }

    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    Input.Origin = Input.EventCenter = FVector(100, 0, 0);
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    FString Error;
    if (!TestTrue(TEXT("Start the default mark graph"),
                  Run->StartWithInput(GetDefault<UWorkshopThunderMarkBurst>()->Graph, Input, {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestTrue(TEXT("The mark waits to burst"), !Run->IsFinished());
    TestEqual(TEXT("Capturing a mark deals no immediate damage"), Fixture.Health(NearMark), 200.f);

    // 等待期间回收原生命，迟爆仍在锁定点结算，也不会把附近未带电的敌人当成新中心。
    TestTrue(TEXT("Remove the original carrier before the delayed hit"), Fixture.Mass->RemoveMonster(Carrier));
    ++GFrameCounter;
    Run->Advance(.4f);
    TestEqual(TEXT("An incomplete fuse leaves the nearby enemy unharmed"), Fixture.Health(NearMark), 200.f);
    ++GFrameCounter;
    Run->Advance(.06f);
    TestEqual(TEXT("The historical mark still deals damage to a nearby enemy"), Fixture.Health(NearMark), 176.f);
    TestEqual(TEXT("Thunder damage applies Shocked through the production damage service"),
              Fixture.Mass->GetElementalExposure(NearMark), EAbilityExposure::Shocked);
    TestEqual(TEXT("An unmarked target outside the historical burst stays unharmed"), Fixture.Health(Unmarked), 200.f);
    TestTrue(TEXT("The graph completes after the burst"),
             Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopFrostShowerRuntimeTiming,
                                 "test_25d.UnifiedGA.DynamicMage.SpatialStorm.FrostShowerRuntimeTiming",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopFrostShowerRuntimeTiming::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture Fixture;
    const FVector Origin(240, 0, 0);
    const int32 InOverlap = Fixture.Spawn(Origin);
    const int32 Farther = Fixture.Spawn(Origin + FVector(180, 0, 0));
    const auto* Base = GetDefault<UWorkshopFrostShower>()->Graph.Get();
    FAbilityRuntimeRecipeContribution Card;
    Card.SourceId = TEXT("Test.FrostShower.Timing");
    const TPair<EAbilityConfigParameter, float> Parameters[] = {{EAbilityConfigParameter::Count, 3.f},
                                                                {EAbilityConfigParameter::Radius, 180.f},
                                                                {EAbilityConfigParameter::Period, .3f}};
    for (const auto& Parameter : Parameters)
    {
        auto& Patch = Card.Patches.AddDefaulted_GetRef();
        Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
        Patch.TargetNodeId = Base->EntryNodeId;
        Patch.Parameter = Parameter.Key;
        Patch.Scalar = Parameter.Value;
    }

    UAbilityGraphDefinition* Graph = nullptr;
    FString Error;
    if (!TestTrue(TEXT("Compile the actual point count, radius and period parameters"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Card}, Graph, Error)))
    {
        AddInfo(Error);
        return false;
    }
    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    Input.Origin = Input.EventCenter = Origin;
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    TArray<FVector> Points;
    const FDelegateHandle Observation = Run->OnEvent.AddLambda(
        [&Points](const FAbilityGraphEvent& Event)
        {
            if (Event.NodeType == TEXT("Control.StaggeredPoints") && Event.OutputPort == TEXT("Point"))
            {
                Points.Add(Event.Input.Origin);
            }
        });
    if (!TestTrue(TEXT("Start the recipe with modified storm points"), Run->StartWithInput(Graph, Input, {}, Error)))
    {
        AddInfo(Error);
        Run->OnEvent.Remove(Observation);
        return false;
    }
    TestTrue(TEXT("The first point is the original cursor location"), Points.Num() == 1 && Points[0].Equals(Origin));
    TestEqual(TEXT("The first pulse damages only its own radius"), Fixture.Health(InOverlap), 183.f);
    TestEqual(TEXT("The wider distributed region is still untouched"), Fixture.Health(Farther), 200.f);

    ++GFrameCounter;
    Run->Advance(.2f);
    TestEqual(TEXT("The default cadence is replaced by the patched period"), Points.Num(), 1);
    ++GFrameCounter;
    Run->Advance(.11f);
    TestEqual(TEXT("The second point moves damage to its own radius"), Fixture.Health(Farther), 183.f);
    TestEqual(TEXT("Overlapping ice hits trigger the project's freeze reaction"),
              Fixture.Mass->GetElementalExposure(InOverlap), EAbilityExposure::Frozen);

    ++GFrameCounter;
    Run->Advance(.3f);
    TestTrue(TEXT("Three points complete normally"),
             Points.Num() == 3 && Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Completed);
    if (Points.Num() == 3)
    {
        TestTrue(TEXT("The last point uses the edited distribution radius"),
                 FMath::IsNearlyEqual(FVector::Dist2D(Points.Last(), Origin), 180.0));
    }
    Run->OnEvent.Remove(Observation);
    return true;
}

#endif
