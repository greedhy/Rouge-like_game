// 测试链：实际默认图 -> 原生命状态筛选与消耗 -> 局部查询或短时传播 -> Mass 伤害和状态。
#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityGraphGameplayExpansionFixture.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipe.h"
#include "AbilitySystem/Unified/WorkshopUtilityAbilities.h"

namespace
{
    /** 从真实原生类取配方，未注册时直接暴露测试失败。 */
    UAbilityGraphDefinition* FindComboGraph(const TCHAR* ClassPath)
    {
        auto* AbilityClass = FindObject<UClass>(nullptr, ClassPath);
        const auto* Ability = AbilityClass ? AbilityClass->GetDefaultObject<UWorkshopUtilityAbility>() : nullptr;
        return Ability ? Ability->Graph.Get() : nullptr;
    }

    /** 使用服务器目标数据的等价位置载荷，不自行施加技能的伤害或状态。 */
    FAbilityNodeInput PrepareComboInput(const GraphGameplayExpansionTests::FFixture& Fixture)
    {
        FAbilityNodeInput Input;
        Input.SourceToken = Fixture.SourceToken;
        Input.Origin = Input.EventCenter = FVector(100, 0, 0);
        return Input;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopEmberRuptureConsumes,
                                 "test_25d.UnifiedGA.DynamicMage.ExposureCombo.EmberRuptureConsumes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopEmberRuptureConsumes::RunTest(const FString&)
{
    GraphGameplayExpansionTests::FFixture Fixture;
    const int32 Carrier = Fixture.Spawn(FVector(350, 0, 0));
    const int32 Neighbor = Fixture.Spawn(FVector(500, 0, 0));
    const int32 Outside = Fixture.Spawn(FVector(650, 0, 0));
    auto* Graph = FindComboGraph(TEXT("/Script/test_25d.WorkshopEmberRupture"));
    if (!TestNotNull(TEXT("The native rupture has an actual graph"), Graph))
    {
        return false;
    }

    FString Error;
    auto* NoMark = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("An unmarked cast finishes normally"),
                  NoMark->StartWithInput(Graph, PrepareComboInput(Fixture), {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("There is no secondary hit without a confirmed status spend"), Fixture.Health(Neighbor), 200.f);
    TestTrue(TEXT("Prepare an actual Burning carrier"),
             Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Fire, 1.f));

    // 独立改掉消耗条件而保留原筛选，必须选到种子后照样停止后续。
    const auto* Consume = Graph->Nodes.FindByPredicate(
        [](const FAbilityGraphNode& Node)
        {
            return Node.TypeId == TEXT("Status.ConsumeExposure");
        });
    if (!TestNotNull(TEXT("The native graph contains a separate consumption point"), Consume))
    {
        return false;
    }
    FAbilityRuntimeRecipeContribution Mismatch;
    Mismatch.SourceId = TEXT("Test.Combo.MismatchedSpend");
    auto& Patch = Mismatch.Patches.AddDefaulted_GetRef();
    Patch.Operation = EAbilityRecipePatchOperation::ConfigureParameter;
    Patch.TargetNodeId = Consume->NodeId;
    Patch.Parameter = EAbilityConfigParameter::ElementalStatus;
    Patch.Scalar = static_cast<float>(EAbilityExposure::Chilled);
    UAbilityGraphDefinition* Edited = nullptr;
    auto* RejectedSpend = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("Compile and run a recipe with an independent spending condition"),
                  AbilityRuntimeRecipes::Compile(Fixture.World, Graph, {Mismatch}, Edited, Error) &&
                      RejectedSpend->StartWithInput(Edited, PrepareComboInput(Fixture), {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Selecting a target is insufficient without a successful exact spend"), Fixture.Health(Neighbor),
              200.f);
    TestEqual(TEXT("A mismatched spend leaves the carrier's Burning intact"),
              Fixture.Mass->GetElementalExposure(Carrier), EAbilityExposure::Burning);

    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("Execute the native rupture against Burning targets"),
                  Run->StartWithInput(Graph, PrepareComboInput(Fixture), {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Removing Burning precedes the Arcane multiplier check"), Fixture.Health(Carrier), 168.f);
    TestEqual(TEXT("The shared Arcane damage replaces the consumed mark"), Fixture.Mass->GetElementalExposure(Carrier),
              EAbilityExposure::ArcaneMark);
    TestEqual(TEXT("Center is the carrier, not the original cursor point"), Fixture.Health(Neighbor), 168.f);
    TestEqual(TEXT("An enemy outside the carrier radius is untouched"), Fixture.Health(Outside), 200.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkshopConductiveRelaySpreads,
                                 "test_25d.UnifiedGA.DynamicMage.ExposureCombo.ConductiveRelaySpreads",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorkshopConductiveRelaySpreads::RunTest(const FString&)
{
    GraphGameplayExpansionTests::FFixture Fixture;
    const int32 Carrier = Fixture.Spawn(FVector(320, 0, 0));
    const int32 First = Fixture.Spawn(FVector(360, 0, 0));
    const int32 Second = Fixture.Spawn(FVector(420, 0, 0));
    const int32 Third = Fixture.Spawn(FVector(460, 0, 0));
    const int32 Fourth = Fixture.Spawn(FVector(480, 0, 0));
    TestTrue(TEXT("Apply Water to the relay carrier"),
             Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Water, 1.f));
    TestTrue(TEXT("Create a real Conductive reaction"),
             Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Thunder, 1.f));

    auto* Graph = FindComboGraph(TEXT("/Script/test_25d.WorkshopConductiveRelay"));
    if (!TestNotNull(TEXT("The native relay has an actual graph"), Graph))
    {
        return false;
    }
    auto* Run = NewObject<UAbilityGraphInstance>(Fixture.World);
    FString Error;
    if (!TestTrue(TEXT("The relay begins from the selected mark"),
                  Run->StartWithInput(Graph, PrepareComboInput(Fixture), {}, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestEqual(TEXT("Conductive is spent before the relay waits"), Fixture.Mass->GetElementalExposure(Carrier),
              EAbilityExposure::None);
    ++GFrameCounter;
    Run->Advance(.20f);
    TestEqual(TEXT("The relay does not strike early"), Fixture.Health(First), 200.f);
    ++GFrameCounter;
    Run->Advance(.03f);
    TestEqual(TEXT("The original carrier is excluded from both damage and selection budget"), Fixture.Health(Carrier),
              200.f);
    TestEqual(TEXT("The first neighbor receives Thunder damage"), Fixture.Health(First), 179.f);
    TestEqual(TEXT("The second neighbor receives Thunder damage"), Fixture.Health(Second), 179.f);
    TestEqual(TEXT("The third neighbor receives Thunder damage"), Fixture.Health(Third), 179.f);
    TestEqual(TEXT("The nearest-three cap excludes the fourth neighbor"), Fixture.Health(Fourth), 200.f);

    TestTrue(TEXT("Restore the carrier to Conductive for the original-location test"),
             Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Water, 1.f) &&
                 Fixture.Mass->ApplyElementalExposure(Carrier, Fixture.Source, EAbilityElement::Thunder, 1.f));
    auto* Historical = NewObject<UAbilityGraphInstance>(Fixture.World);
    if (!TestTrue(TEXT("Start a second relay before removing its carrier"),
                  Historical->StartWithInput(Graph, PrepareComboInput(Fixture), {}, Error) &&
                      Fixture.Mass->RemoveMonster(Carrier)))
    {
        AddInfo(Error);
        return false;
    }
    ++GFrameCounter;
    Historical->Advance(.23f);
    TestEqual(TEXT("A successfully paid relay continues from the captured location after the carrier is gone"),
              Fixture.Health(First), 158.f);
    return true;
}

#endif
