// 测试链：霜锋潮默认图 -> 正数上限补丁 -> Mass 波前伤害 -> 实际元素状态与图结束。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/Nodes/AbilityTemporalNodes.h"
#include "AbilitySystem/Unified/WorkshopPropagationAbilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopFrostSurgeTargetLimit,
                                 "test_25d.UnifiedGA.DynamicMage.Temporal.WorkshopFrostSurgeTargetLimit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopFrostSurgeTargetLimit::RunTest(const FString&)
{
    using namespace GraphGameplayExpansionTests;
    FFixture Fixture;
    const int32 First = Fixture.Spawn(FVector(100, 0, 0));
    const int32 Second = Fixture.Spawn(FVector(200, 0, 0));
    const int32 Third = Fixture.Spawn(FVector(300, 0, 0));
    const UWorkshopFrostSurge* Ability = GetDefault<UWorkshopFrostSurge>();
    FString Error;
    const auto* Base = AbilityRuntimeRecipes::BuildDefaultGraph(Fixture.World, Ability, Error);
    if (!TestNotNull(TEXT("Export the authored Frost Surge graph"), Base))
    {
        AddInfo(Error);
        return false;
    }

    FAbilityRuntimeRecipePatch Limit;
    Limit.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Limit.TargetNodeId = Base->EntryNodeId;
    Limit.Parameter = EAbilityConfigParameter::Count;
    Limit.Scalar = 2.f;
    FAbilityRuntimeRecipeContribution Card;
    Card.SourceId = TEXT("Test.LimitFrostSurge");
    Card.Patches.Add(Limit);
    UAbilityGraphDefinition* Graph = nullptr;
    if (!TestTrue(TEXT("Apply target limit through the real typed recipe operation"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Base, {Card}, Graph, Error)))
    {
        AddInfo(Error);
        return false;
    }

    FAbilityNodeInput Input;
    Input.SourceToken = Fixture.SourceToken;
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("Start the modified graph"), Run->StartWithInput(Graph, Input, {}, Fixture.Registry(), Error)))
    {
        AddInfo(Error);
        return false;
    }
    Run->Advance(.12f);
    TestEqual(TEXT("First wave step damages only the nearest enemy"), Fixture.Health(First), 175.f);
    TestEqual(TEXT("Further enemies wait for the actual wave"), Fixture.Health(Second), 200.f);

    ++GFrameCounter;
    Run->Advance(.12f);
    TestEqual(TEXT("Previously hit enemy does not take duplicate damage"), Fixture.Health(First), 175.f);
    TestEqual(TEXT("Second step reaches one further enemy"), Fixture.Health(Second), 175.f);
    TestTrue(TEXT("Fixed ice damage applies the existing chilled state"),
             Fixture.Mass->GetElementalExposure(Second) == EAbilityExposure::Chilled);
    TestTrue(TEXT("Reaching the limit completes normally"),
             Run->IsFinished() && Run->GetEndReason() == EAbilityGraphEndReason::Completed);

    ++GFrameCounter;
    Run->Advance(.5f);
    TestEqual(TEXT("An extra tick cannot start another hit past the configured limit"), Fixture.Health(Third), 200.f);
    return true;
}
#endif
