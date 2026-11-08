/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Roguelike.Model.WavesAndMixedTargets
 * - test_25d.Roguelike.Model.SummonsAndAbnormalRemoval
 * - test_25d.Roguelike.Model.GatheringAndDoorPolicy
 * - test_25d.Roguelike.Model.ClosedDoorTeleportAndInvalidGraph
 * - test_25d.Roguelike.Run.PendingSpawnReservations
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "Roguelike/DungeonRunModel.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    FDungeonLayout RunFixture()
    {
        FDungeonLayout L;
        const EDungeonRoomRole Roles[] = {EDungeonRoomRole::Entrance, EDungeonRoomRole::Combat, EDungeonRoomRole::Rest,
                                          EDungeonRoomRole::Boss, EDungeonRoomRole::Treasure};
        for (int32 I = 0; I < 5; ++I)
        {
            auto& R = L.Rooms.AddDefaulted_GetRef();
            R.RoomId = FName(*FString::Printf(TEXT("R%02d"), I));
            R.Role = Roles[I];
            R.Cell = I == 4 ? FIntPoint(1, 1) : FIntPoint(I, 0);
        }
        for (const FIntPoint Edge : {FIntPoint(0, 1), FIntPoint(1, 2), FIntPoint(2, 3), FIntPoint(1, 4)})
        {
            auto& E = L.Connections.AddDefaulted_GetRef();
            E.From = Edge.X;
            E.To = Edge.Y;
        }
        return L;
    }
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonWavesTest, "test_25d.Roguelike.Model.WavesAndMixedTargets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Model.WavesAndMixedTargets 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonWavesTest::RunTest(const FString&)
{
    FDungeonEncounterLedger L;
    TestFalse(TEXT("Empty ledger cannot clear"), L.IsClear());
    if (!TestTrue(TEXT("Start two waves"), L.Start(2)))
        return false;
    TestFalse(TEXT("Cannot register before opening first spawn window"), L.RegisterTarget("Early", true));
    TestTrue(TEXT("First wave"), L.BeginWave());
    TestTrue(TEXT("Actor registered"), L.RegisterTarget("Actor", true));
    TestTrue(TEXT("Mass registered"), L.RegisterTarget("Mass1", true));
    TestFalse(TEXT("Duplicate registration rejected"), L.RegisterTarget("Actor", true));
    TestFalse(TEXT("Spawning prevents completion"), L.IsClear());
    TestTrue(TEXT("End first wave"), L.EndWave(true));
    TestTrue(TEXT("Actor killed"), L.ResolveTarget("Actor", EDungeonTargetResolution::Killed));
    TestFalse(TEXT("Duplicate death ignored"), L.ResolveTarget("Actor", EDungeonTargetResolution::Killed));
    TestTrue(TEXT("Mass killed"), L.ResolveTarget("Mass1", EDungeonTargetResolution::Killed));
    TestFalse(TEXT("Pending wave prevents premature clear"), L.IsClear());
    TestTrue(TEXT("Next wave available"), L.CanSpawnNextWave());
    TestTrue(TEXT("Start final wave"), L.BeginWave());
    TestTrue(TEXT("New target"), L.RegisterTarget("Mass2", true));
    TestTrue(TEXT("End final wave"), L.EndWave(true));
    TestTrue(TEXT("Final target death"), L.ResolveTarget("Mass2", EDungeonTargetResolution::Killed));
    TestTrue(TEXT("Now cleared"), L.IsClear());
    TestFalse(TEXT("No further waves"), L.BeginWave());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSummonTest, "test_25d.Roguelike.Model.SummonsAndAbnormalRemoval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Model.SummonsAndAbnormalRemoval 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonSummonTest::RunTest(const FString&)
{
    FDungeonEncounterLedger L;
    if (!TestTrue(TEXT("Start boss encounter"), L.Start(1)))
        return false;
    L.BeginWave();
    L.RegisterTarget("Boss", true);
    L.EndWave(true);
    TestTrue(TEXT("Required summon joins active encounter"), L.RegisterTarget("RequiredSummon", true));
    TestTrue(TEXT("Optional summon joins"), L.RegisterTarget("OptionalSummon", false));
    L.ResolveTarget("Boss", EDungeonTargetResolution::Killed);
    TestFalse(TEXT("Boss death alone does not clear"), L.IsClear());
    TestTrue(TEXT("Optional recycle accepted"), L.ResolveTarget("OptionalSummon", EDungeonTargetResolution::Recycled));
    TestTrue(TEXT("Required summon death"), L.ResolveTarget("RequiredSummon", EDungeonTargetResolution::Killed));
    TestTrue(TEXT("Boss and required summons clear"), L.IsClear());
    TestFalse(TEXT("No late summon after completion"), L.RegisterTarget("Late", true));
    L.Start(1);
    L.BeginWave();
    L.RegisterTarget("Lost", true);
    L.EndWave(true);
    TestTrue(TEXT("Abnormal resolution consumed"), L.ResolveTarget("Lost", EDungeonTargetResolution::Unexpected));
    TestFalse(TEXT("Missing entity cannot be a successful kill"), L.IsClear());
    TestFalse(TEXT("Explicit failure reason"), L.Error.IsEmpty());
    TestFalse(TEXT("Unknown target cannot change result"),
              L.ResolveTarget("Unknown", EDungeonTargetResolution::Killed));
    L.Start(1);
    L.BeginWave();
    TestFalse(TEXT("Empty successful spawn is still a failure"), L.EndWave(true));
    TestFalse(TEXT("Empty wave does not clear"), L.IsClear());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGatheringTest, "test_25d.Roguelike.Model.GatheringAndDoorPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Model.GatheringAndDoorPolicy 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonGatheringTest::RunTest(const FString&)
{
    FDungeonRunModel M;
    if (!TestTrue(TEXT("Initialize real graph"), M.Initialize(RunFixture())))
        return false;
    TestTrue(TEXT("Entrance open"), M.CanCross(0, 1));
    TestFalse(TEXT("Cannot bypass uncleared combat"), M.CanCross(1, 2));
    TestFalse(TEXT("Branch also held behind combat"), M.CanCross(1, 4));
    TestEqual(TEXT("One outside waits"), M.Observe({1, 0}, {true, true}), INDEX_NONE);
    TestEqual(TEXT("Gathering selected"), M.GatheringRoom, 1);
    TestTrue(TEXT("Retreat remains possible while gathering"), M.CanCross(1, 0));
    M.Observe({0, 0}, {true, true});
    TestEqual(TEXT("Retreat cancels gathering"), M.GatheringRoom, INDEX_NONE);
    TestEqual(TEXT("Door margin is not a safe area"), M.Observe({1, 1}, {true, false}), INDEX_NONE);
    TestFalse(TEXT("Early start rejected"), M.BeginEncounter(1, 2));
    TestEqual(TEXT("All living members safe"), M.Observe({1, 1}, {true, true}), 1);
    TestTrue(TEXT("Start once"), M.BeginEncounter(1, 1));
    TestFalse(TEXT("Second start rejected"), M.BeginEncounter(1, 1));
    TestFalse(TEXT("Combat entrance locks"), M.CanCross(1, 0));
    TestFalse(TEXT("Combat exit locks"), M.CanCross(1, 2));
    M.Encounter.BeginWave();
    M.Encounter.RegisterTarget("Enemy", true);
    M.Encounter.EndWave(true);
    TestEqual(TEXT("Living target blocks completion"), M.FinishEncounter(), INDEX_NONE);
    M.Encounter.ResolveTarget("Enemy", EDungeonTargetResolution::Killed);
    TestEqual(TEXT("Complete returns room for exactly-once reward"), M.FinishEncounter(), 1);
    TestEqual(TEXT("Repeat completion no reward"), M.FinishEncounter(), INDEX_NONE);
    TestTrue(TEXT("Forward opens"), M.CanCross(1, 2));
    TestTrue(TEXT("Return opens"), M.CanCross(1, 0));
    TestTrue(TEXT("Branch opens"), M.CanCross(1, 4));
    TestEqual(TEXT("Revisit does not start new encounter"), M.Observe({1}, {true}), INDEX_NONE);
    TestEqual(TEXT("Safe room visit"), M.Observe({4}, {true}), INDEX_NONE);
    TestTrue(TEXT("Visited shared state recorded"), M.Rooms[4].bVisited);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonMovementTest, "test_25d.Roguelike.Model.ClosedDoorTeleportAndInvalidGraph",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Model.ClosedDoorTeleportAndInvalidGraph 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonMovementTest::RunTest(const FString&)
{
    FDungeonRunModel M;
    if (!TestTrue(TEXT("Initialize"), M.Initialize(RunFixture())))
        return false;
    TestTrue(TEXT("Move through open entrance"), M.CanMove(FVector(0, 0, 90), FVector(2400, 0, 90), 2400));
    TestFalse(TEXT("Dash cannot skip closed intermediate doorway"),
              M.CanMove(FVector(0, 0, 90), FVector(7200, 0, 90), 2400));
    TestFalse(TEXT("No room across wall"), M.CanMove(FVector(0, 0, 90), FVector(0, 2400, 90), 2400));
    TestFalse(TEXT("Nonexistent direct edge"), M.CanCross(0, 3));
    TestFalse(TEXT("Diagonal corner bypass denied"), M.CanMove(FVector(0, 0, 90), FVector(2400, 2400, 90), 2400));
    TestFalse(TEXT("Invalid spacing denied"), M.CanMove(FVector::ZeroVector, FVector::ZeroVector, 0));
    M.Observe({1}, {true});
    M.BeginEncounter(1, 1);
    M.Fail(TEXT("RequiredTargetLost"));
    TestEqual(TEXT("Failed room visible"), M.Rooms[1].Phase, EDungeonRoomPhase::Failed);
    TestEqual(TEXT("Failure cannot restart"), M.Observe({1}, {true}), INDEX_NONE);
    auto Invalid = RunFixture();
    Invalid.Connections[0].To = 99;
    TestFalse(TEXT("Invalid edge refused"), M.Initialize(Invalid));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonPendingSpawnLedgerTest, "test_25d.Roguelike.Run.PendingSpawnReservations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Roguelike.Run.PendingSpawnReservations 回归，失败信息由下方断言交给自动化框架收集。 */
bool FDungeonPendingSpawnLedgerTest::RunTest(const FString& Parameters)
{
    FDungeonEncounterLedger Ledger;
    TestTrue(TEXT("Start"), Ledger.Start(1));
    TestTrue(TEXT("Begin"), Ledger.BeginWave());
    TestTrue(TEXT("Reserve first"), Ledger.RegisterTarget(TEXT("A1"), true));
    TestTrue(TEXT("Reserve not yet spawned"), Ledger.RegisterTarget(TEXT("A2"), true));
    TestTrue(TEXT("Close registration"), Ledger.EndWave(true));
    TestTrue(TEXT("Kill first before second appears"),
             Ledger.ResolveTarget(TEXT("A1"), EDungeonTargetResolution::Killed));
    TestFalse(TEXT("Queued required enemy prevents early clear"), Ledger.IsClear());
    TestTrue(TEXT("Kill second"), Ledger.ResolveTarget(TEXT("A2"), EDungeonTargetResolution::Killed));
    TestTrue(TEXT("Clear only after final reserved enemy dies"), Ledger.IsClear());
    return true;
}
#endif
